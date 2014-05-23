#include <iostream>
#include <fstream>
#include <string>
#include <pqxx/pqxx>
#include "UUID.h"

#define GID_AS_NAME
#undef GID_AS_NAME

#define CURSOR_CHUNK 8192
#define LIMIT " "
//#define LIMIT " limit 5 "

typedef unsigned char uchar_t;
typedef short int int16_t;
//typedef long int int32_t;
//typedef unsigned long int uint32_t;

UUID uuid;

void Usage()
{
    std::cout << "Usage: pgr2osrm etable vtable rtable [conn_string]\n";
    std::cout << "   etable is the edge table name to output\n";
    std::cout << "   vtable is the vertices_tmp table name related to etable\n";
    std::cout << "   rtable is the restrictions table name related to etable\n";
    std::cout << "   conn_string is the postgresql connection string\n";
    std::cout << " This will drop and create table <etable>_osrm_names\n";
}

void writeHeader( std::ofstream &out )
{
/*
    char head[] = "OSRMa2b9440913138f6f794c617510b8359c.e3557582313d20e6906c09cbf69283e6.346f499c6aa04e614086450abd0a8a3a.53f79060b00efc253475f663f1a919ed.1a0846c90dd19fd4211b3de10dd81054....................";
    int len = 188;

    // convert '.' to '\0'
    for (int i=0; i<len; i++)
        if (head[i] == '.') head[i] = '\0';
*/
    out.write( (char *) &uuid, sizeof(UUID) );
}


void createNames( pqxx::connection &conn, std::string &etable )
{
    pqxx::work txn(conn);
    txn.exec( "drop table if exists " + etable + "_osrm_names cascade" );
    txn.exec( "create table " + etable + 
              "_osrm_names (  id serial not null primary key, name text)" );
    txn.exec( "insert into " + etable + "_osrm_names (name) " + 
#ifdef GID_AS_NAME
              "select gid::text as name from " + etable +
              " order by gid asc" );
#else
              "select distinct name from " + etable +
              " where nullif(name, '') is not null order by name asc" );
#endif
    txn.exec( "create unique index " + etable + "_osrm_names_name on " +
              etable + "_osrm_names using btree(name asc)" );
    txn.commit();
}

int writeNames( pqxx::connection &conn, std::string &filename, std::string &etable )
{
    std::ofstream out( filename.c_str(), std::ios::out | std::ios::binary );

    pqxx::work txn( conn );

    pqxx::result q = txn.exec(
        "select sum(length(name)) as ccnt from " + etable + "_osrm_names"
    );
    uint32_t ccnt = q[0]["ccnt"].as<uint32_t>();

    pqxx::result names = txn.exec(
        "select name from " + etable + "_osrm_names order by id asc"
    );

    // write out the number of names
    uint32_t cnt = names.size() + 2;
    out.write( (const char *) &cnt, sizeof(uint32_t) );

    // write total number of characters
    out.write( (const char *) &ccnt, sizeof(uint32_t) );

    // the first slot is for null names or name=""
    uint32_t zero = 0;
    out.write( (const char *) &zero, sizeof(uint32_t) );
    out.write( (const char *) &zero, sizeof(uint32_t) );

    uint32_t last = 0;
    std::string buf = "";

    for ( pqxx::result::const_iterator row = names.begin();
          row != names.end();
          ++row
        ) {
        last += row["name"].size();
        buf += row["name"].c_str();
        out.write( (const char *) &last, sizeof(uint32_t) );
    }

    //out.write( (const char *) &last, sizeof(uint32_t) );
    out.write( (const char *) buf.c_str(), buf.size() );

    out.close();
    return cnt;
}

int writeVertices( pqxx::connection &conn, std::ofstream &out, std::string &vtable )
{
    pqxx::work txn( conn );

    // output the count of vertices
    pqxx::result q = txn.exec(
        "select count(*) as cnt from " + vtable
    );
    uint32_t cnt = q[0]["cnt"].as<uint32_t>();
    out.write( (const char *) &cnt, sizeof(uint32_t) );

    pqxx::stateless_cursor<pqxx::cursor_base::read_only,
                           pqxx::cursor_base::owned> cursor(
        txn,
        "select round(st_y(the_geom)*1000000.0)::integer as lat, "
        "       round(st_x(the_geom)*1000000.0)::integer as lon, "
        "       id::integer as id, "
        "       0::integer as flags "
        "   from " + vtable +
        "  order by id asc " + LIMIT,
        "vertices",
        false
    );

    int cnt2 = 0;
    for ( size_t idx = 0; true; idx += CURSOR_CHUNK) {
        pqxx::result result = cursor.retrieve( idx, idx + CURSOR_CHUNK );
        if ( result.empty() ) break;

        for ( pqxx::result::const_iterator row = result.begin();
              row != result.end();
              ++row
            ) {

            int32_t lat = row["lat"].as<int32_t>();
            out.write( (const char *) &lat, sizeof(int32_t) );

            int32_t lon = row["lon"].as<int32_t>();
            out.write( (const char *) &lon, sizeof(int32_t) );

            uint32_t id = row["id"].as<uint32_t>();
            out.write( (const char *) &id, sizeof(uint32_t) );

            id = 0; // we dont have bollards or traffic lights
            out.write( (const char *) &id, sizeof(uint32_t) );

            ++cnt2;
        }
        if ( result.size() < CURSOR_CHUNK ) break;
    }

    //std::cout << "Vertices: cnt: " << cnt << ", cnt2: " << cnt2 << "\n";

    return cnt;
}

int writeEdges( pqxx::connection &conn, std::ofstream &out, std::string &etable )
{
    pqxx::work txn( conn );

    // write the count of edges we will output
    pqxx::result q = txn.exec(
        "select count(*) as cnt from " + etable + " where source != target "
    );
    uint32_t cnt = q[0]["cnt"].as<uint32_t>();
    out.write( (const char *) &cnt, sizeof(uint32_t) );

    pqxx::stateless_cursor<pqxx::cursor_base::read_only,
                           pqxx::cursor_base::owned> cursor(
        txn,
        "select case when dir_travel='T' then target "
        "            else source end as source, "
        "       case when dir_travel='T' then source "
        "            else target end as target, "
        "       st_length_spheroid(st_geometryn(the_geom, 1), "
        "           'SPHEROID[\"GRS_1980\",6378137,298.257222101]'::spheroid"
        "           )::integer as length, "
        "       case when dir_travel = 'B' then 0 else 1 end::integer as dir, "
        "       round(st_length_spheroid(st_geometryn(the_geom, 1), "
        "             'SPHEROID[\"GRS_1980\",6378137,298.257222101]'::spheroid"
        "           ) / case when speed_cat='1' then 130.0 "
        "                  when speed_cat='2' then 101.0 "
        "                  when speed_cat='3' then  91.0 "
        "                  when speed_cat='4' then  71.0 "
        "                  when speed_cat='5' then  51.0 "
        "                  when speed_cat='6' then  31.0 "
        "                  when speed_cat='7' then  11.0 "
        "                  when speed_cat='8' then   5.0 "
        "                  else 1.0 "
        "                  end * 36.0)::integer as weight, "
        "       speed_cat::integer as rank, "
        "       case when b.id is null then 0 else b.id end::integer as nameid,"
        "       case when roundabout='Y' then 1 "
        "            else 0 end::integer as roundabout, "
        "       case when tunnel='Y' or bridge='Y' then 1 "
        "            else 0 end::integer as ignoreingrid, "
        "       0::integer as accessrestricted "
        "  from " + etable + " a left outer join " + etable + "_osrm_names b "
#ifdef GID_AS_NAME
        "    on a.gid::text=b.name where source != target "
#else
        "    on a.name=b.name where source != target "
#endif
        " order by a.target asc " + LIMIT,
        "edges",
        false
    );

    for ( size_t idx = 0; true; idx += CURSOR_CHUNK) {
        pqxx::result result = cursor.retrieve( idx, idx + CURSOR_CHUNK );
        if ( result.empty() ) break;

        for ( pqxx::result::const_iterator row = result.begin();
              row != result.end();
              ++row
            ) {

            uint32_t source = row["source"].as<uint32_t>();
            out.write( (const char *) &source, sizeof(uint32_t) );

            uint32_t target = row["target"].as<uint32_t>();
            out.write( (const char *) &target, sizeof(uint32_t) );

            int32_t length = row["length"].as<int32_t>();
            out.write( (const char *) &length, sizeof(int32_t) );

            int16_t dir = row["dir"].as<int16_t>();
            out.write( (const char *) &dir, sizeof(int16_t) );

            int32_t weight = row["weight"].as<int32_t>();
            out.write( (const char *) &weight, sizeof(int32_t) );

            int16_t rank = row["rank"].as<int16_t>();
            out.write( (const char *) &rank, sizeof(int16_t) );

            uint32_t nameid = row["nameid"].as<uint32_t>();
            out.write( (const char *) &nameid, sizeof(int32_t) );

            uchar_t flag = (row["roundabout"].as<int>() == 1) ? '\01' : '\0';
            out.write( (const char *) &flag, sizeof(uchar_t) );

            flag = (row["ignoreingrid"].as<int>() == 1) ? '\01' : '\0';
            out.write( (const char *) &flag, sizeof(uchar_t) );

            flag = (row["accessrestricted"].as<int>() == 1) ? '\01' : '\0';
            out.write( (const char *) &flag, sizeof(uchar_t) );

            // write a dummy byte for word alignment
            flag = '\0';
            out.write( (const char *) &flag, sizeof(uchar_t) );
        }
        if ( result.size() < CURSOR_CHUNK ) break;
    }
    return cnt;
}


/*

-- create osrmrestrictions

create table osrmrestrictions (
   id serial not null primary key,
   n_via integer,
   n_from integer,
   n_to integer,
   is_forbidden boolean,
   to_way_id integer
);

-- write <filename>.osrm.restrictions
select count(*) from osrmrestrictions;
select n_via,
       n_from,
       n_to,
       case when is_forbidden then 0 else 1 end::integer as forbid
  from osrmrestrictions
 order by to_way_id asc;

*/

int writeRestrictions( pqxx::connection &conn, std::string &filename, std::string &rtable )
{
    std::ofstream out( filename.c_str(), std::ios::out | std::ios::binary );
    writeHeader( out );

    pqxx::work txn( conn );

    uint32_t cnt = 0;
    // write the count of edges we will output
    try {
        pqxx::result q = txn.exec(
            "select count(*) as cnt from " + rtable
        );
        cnt = q[0]["cnt"].as<uint32_t>();
    }
    catch (const std::exception &e) {
        // nop
    }
    out.write( (const char *) &cnt, sizeof(uint32_t) );

    if (cnt) {
        pqxx::stateless_cursor<pqxx::cursor_base::read_only,
                               pqxx::cursor_base::owned> cursor(
            txn,
            "select n_via, "
            "       n_from, "
            "       n_to, "
            "       case when is_forbidden then 0 else 1 end::integer as forbid "
            "  from " + rtable +
            " order by to_way_id asc ",
            "restrictions",
            false
        );

        for ( size_t idx = 0; true; idx += CURSOR_CHUNK) {
            pqxx::result result = cursor.retrieve( idx, idx + CURSOR_CHUNK );
            if ( result.empty() ) break;

            for ( pqxx::result::const_iterator row = result.begin();
                  row != result.end();
                  ++row
                ) {

                uint32_t n_via = row["n_via"].as<uint32_t>();
                out.write( (const char *) &n_via, sizeof(uint32_t) );

                uint32_t n_from = row["n_from"].as<uint32_t>();
                out.write( (const char *) &n_from, sizeof(uint32_t) );

                uint32_t n_to = row["n_to"].as<uint32_t>();
                out.write( (const char *) &n_to, sizeof(uint32_t) );

                uchar_t flag = (row["forbid"].as<int>() == 1) ? '\01' : '\0';
                out.write( (const char *) &flag, sizeof(uchar_t) );

                flag = (uchar_t) 0x7f;
                out.write( (const char *) &flag, sizeof(uchar_t) );

                flag = '\0';
                out.write( (const char *) &flag, sizeof(uchar_t) );
                out.write( (const char *) &flag, sizeof(uchar_t) );

            }
            if ( result.size() < CURSOR_CHUNK ) break;
        }
    }
    out.close();

    return cnt;
}

int main (int argc, char **argv)
{
    int cnt;

    if (argc < 4 || argc > 5) {
        Usage();
        return 1;
    }

    std::string etable = argv[1];
    std::string vtable = argv[2];
    std::string rtable = argv[3];

    std::string fnames    = etable + ".osrm.names";
    std::string fosrm     = etable + ".osrm";
    std::string frestrict = etable + ".osrm.restrictions";

    const char *dbconn    = "dbname=osrm_test user=postgres host=localhost";
    if (argc == 5) dbconn = argv[4];


    try {
        pqxx::connection conn( dbconn );
        pqxx::work txn(conn);
        txn.exec( "set client_min_messages to warning" );
        txn.commit();

        std::cout << "  Creating names table ...\n";
        createNames( conn, etable );

        std::cout << "  Writing " << fnames << " file ... ";
        cnt = writeNames( conn, fnames, etable );
        std::cout << cnt << " records\n";

        std::ofstream out( fosrm.c_str(), std::ios::out | std::ios::binary );

        writeHeader( out );

        std::cout << "  Writing vertices to " << fosrm << " file ... ";
        cnt = writeVertices( conn, out, vtable );
        std::cout << cnt << " records\n";

        std::cout << "  Writing edges to " << fosrm << " file ... ";
        cnt = writeEdges( conn, out, etable );
        std::cout << cnt << " records\n";

        out.close();

        std::cout << "  Writing restrictions to " << frestrict << " file ... ";
        cnt = writeRestrictions( conn, frestrict, rtable );
        std::cout << cnt << " records\n";

        std::cout << "  Done!\n";
    }
    catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }
}
