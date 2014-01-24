----------------------------------------------------------------------------
Title: pg2osrm
Author: Stephen Woodbridge
Date: 2013-12-14
----------------------------------------------------------------------------
This document describes the work flow to extract pgRouting topology
into a OSRM normalized file.

Assumptions:
* a table in pgRouting that has been prepared with ``pgr_createTopology()``.
* an optional restrictions table
* pgRouting 2.0.x
* ...

We have to create the files described here:
see: https://github.com/DennisOSRM/Project-OSRM/wiki/OSRM-normalized-file-format

You have to have Project-OSRM checkout, built and referenced in the Makefile. IE: set OSRMDIR= ../Project-OSRM
Make the path appropriate for your locations.

## BUILDING
Assumes you have checkout Project-OSRM and built that. Then you editted the Makefile so OSRMDIR points to it.
```
make
sudo cp pgr2osrm /somewhere/in/your/path/.
```

## TESTING
Assuming you have postgresql 9.2 and Postgis 2.x installed. These are just scripts so read them to see what they do.
```
./mk-testdb2 # to create a small test database "osrm_test"
cd test
./extract-ddnoded2 
...
```

---------------------------------------------------------
Process
---------------------------------------------------------

The following is basically the SQL the is run by the pgr2osrm command. You
do NOT need to run this SQL, it is only here as a reference.

```
create table names (
    id serial not null primary key,
    name text
);
insert into names (name) select distinct name from st order by name nulls first asc;
create unique index names_name on names using btree(name asc nulls first);

-- write <filename>.osrm.names using queries
select count(*) from names;
select (id-1)::integer as id, name from names order by id asc;

-- write <filename>.osrm using queries
-- write vertices --
select count(*) from vertices_tmp;
select round(st_y(the_geom)*1000000.0)::integer as lat,
       round(st_x(the_geom)*1000000.0)::integer as lon,
       (id-1)::integer as id,
       0::integer as flags from vertices_tmp order by id asc;

-- write edges --
select count(*) from st;
select case when dir_travel='T' then target-1 else source-1 end as source,
       case when dir travel='T' then source-1 else target-1 end as target,
       round(st_length_spheriod(st_geometryn(the_geom, 1),
            'SPHEROID["GRS_1980",6378137,298.257222101]'))::integer as length,
       case when dir_travel = 'B' then 0 else 1 end::integer as dir,
       round(case when speed_cat='1' then 130.0
                  when speed_cat='2' then 101.0
                  when speed_cat='3' then  91.0
                  when speed_cat='4' then  71.0
                  when speed_cat='5' then  51.0
                  when speed_cat='6' then  31.0
                  when speed_cat='7' then  11.0
                  when speed_cat='8' then   5.0
                  end * 0.027777778)::integer as weight,
        speed_cat::integer as rank,
        (b.id-1)::integer as nameid,
        case when roundabout='Y' then 1 else 0 end::integer as roundabout,
        case when tunnel='Y' or bridge='Y' then 1 else 0 end::integer as ignoreingrid,
        0::integer as accessrestricted
  from st a, names b
 where a.name=b.name
 order by a.target asc;


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



