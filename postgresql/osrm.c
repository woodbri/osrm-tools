/*
    osrm-wrapper.c

    Implement plpgsql functions to access ORSM server

    Auther: Stephen Woodbridge
    Date: 2013-11-16

-----------------------------------------------------------------------
    I have included modified code from pgcurl which has the following
    Copyright. For more info see: https://github.com/Ormod/pgcurl

    Copyright (c) 2011 Hannu Valtonen <hannu.valtonen@ohmu.fi>

    Permission is hereby granted, free of charge, to any person
    obtaining a copy of this software and associated documentation files
    (the "Software"), to deal in the Software without restriction,
    including without limitation the rights to use, copy, modify, merge,
    publish, distribute, sublicense, and/or sell copies of the Software,
    and to permit persons to whom the Software is furnished to do so,
    subject to the following conditions:

    The above copyright notice and this permission notice shall be
    included in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
    EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
    NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
    BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
    ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
    CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.
-----------------------------------------------------------------------
*/
#include "osrm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef PG_MODULE_MAGIC
PG_MODULE_MAGIC;
#endif

#undef DEBUG
//#define DEBUG 1

#ifdef DEBUG
#define DBG(format, arg...) elog(NOTICE, format , ## arg)
#else
#define DBG(format, arg...) do { ; } while (0)
#endif

#define DTYPE float8

/* ****************** stuff for curl ************************** */
static struct osrm_curl_global globals;

void _PG_init(void)
{
    globals.pg_ctxt = AllocSetContextCreate(TopMemoryContext,
        "osrm_curl global context",
        ALLOCSET_SMALL_MINSIZE,
        ALLOCSET_SMALL_INITSIZE,
        ALLOCSET_SMALL_MAXSIZE);
    MemoryContextSwitchTo(globals.pg_ctxt);
}

static size_t read_callback(void *ptr, size_t size, size_t nmemb, void *userp)
{
    struct help_struct *pooh = (struct help_struct *)userp;
 
    if(size*nmemb < 1)
        return 0;
 
    if(pooh->size) {
        *(char *)ptr = pooh->string[0]; /* copy one single byte */
        pooh->string++; /* advance pointer */
        pooh->size--; /* less data left */
        return 1; /* we return 1 byte at a time! */
    }
    return 0; /* no more data left to deliver */
}

static size_t write_callback(void *ptr, size_t size, size_t nmemb, void *data)
{
    size_t realsize = size * nmemb;
    struct help_struct *mem = (struct help_struct *)data;
 
    mem->string = repalloc(mem->string, mem->size + realsize + 1);
    if (mem->string == NULL)
        elog(ERROR, "not enough memory to realloc)\n");
 
    memcpy(&(mem->string[mem->size]), ptr, realsize);
    mem->size += realsize;
    mem->string[mem->size] = 0;
 
    return realsize;
}

static char *curl_do_actual_work(int method_type, char *url, char *payload)
{
    CURL *curl;
    CURLcode res;
    StringInfoData buf;
    struct help_struct read_chunk, write_chunk;
 
    read_chunk.string = palloc(1); /* will be grown as needed by the realloc above */
    read_chunk.size = 0; /* no data at this point */

    curl = curl_easy_init();
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 0);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "pg-osrm-agent/1.0");
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L); /* Consider making this configurable */
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L); /* Consider making this configurable */
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&read_chunk);
        if (method_type == OSRMCURL_PUT) {
            curl_easy_setopt(curl, CURLOPT_UPLOAD, 1);

            if (!payload) // prevent segv
                payload = "";
            write_chunk.string = payload;
            write_chunk.size = strlen(payload);

            curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_callback);
            curl_easy_setopt(curl, CURLOPT_READDATA, &write_chunk);
            curl_easy_setopt(curl, CURLOPT_INFILESIZE, write_chunk.size);
        }

        if (method_type == OSRMCURL_POST) {
            curl_easy_setopt(curl, CURLOPT_POST, 1);
        }

        if (method_type == OSRMCURL_DELETE) {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
        }
        res = curl_easy_perform(curl);
        if (res != 0)
            elog(WARNING, "OSRMCurl error: %s, URL: %s\n", curl_easy_strerror(res), url);
        curl_easy_cleanup(curl);
    }

    initStringInfo(&buf);
    if(read_chunk.string) {
        appendStringInfo(&buf, "%s", read_chunk.string);
        pfree(read_chunk.string);
    }
    return buf.data;
}

/* ********************* end of curl stuff ********************** */
/* ********************* start of private functions ************* */

static char *text2char(text *in)
{
    char *out = palloc(VARSIZE(in));
    memcpy(out, VARDATA(in), VARSIZE(in) - VARHDRSZ);
    out[VARSIZE(in) - VARHDRSZ] = '\0';
    return out;
}


/*
    Typical OSRM json response to viaroute

{"version": 0.3,"status":0,"status_message": "Found route between points","route_geometry": "{|zmqA`qjwpC?wyP?o{qBl{qB??m{qBm{qB?o{qB?m{qB?o{qB?m{qB?o{qB?m{qB?o{qB?m{qB??ufN","route_instructions": [["10","N20",5506,0,2915,"5506m","E",90],["3","N60",6542,2,562,"6542m","S",180],["7","N2",4771,3,146,"4771m","E",90],["7","N61",58884,4,1797,"58884m","N",0],["3","N28",627,13,153,"627m","E",90],["15","",0,14,0,"","N",0.0]],"route_summary":{"total_distance":76332,"total_time":4676,"start_point":"N20","end_point":"N28"},"alternative_geometries": [],"alternative_instructions":[],"alternative_summaries":[],"route_name":["N60","N61"],"alternative_names":[["",""]],"via_points":[[43.235294,-76.420897 ],[43.705882,-76.286315 ]],"hint_data": {"checksum":52824373, "locations": ["Dw0AAA4AAADwOAAAdgoAAJqulHdeCOs_3reTAt_ocfs", "MRIAABYAAADkBgAAES0AAEeiWVys-sA_GuaaApX2c_s"]},"transactionId": "OSRM Routing Engine JSON Descriptor (v0.3)"}

    Typical OSRM json responce to nearest
    and response to locate is the same but with out "name"

{"version":0.3,"status":0,"mapped_coordinate":[43.235294,-76.420897],"name":"N20","transactionId":"OSRM Routing Engine JSON Nearest (v0.3)"}

*/


static char *jget_version(char *json)
{
    struct json_object *jtree;
    struct json_object *jobj;
    char *t;

    jtree = json_tokener_parse(json);
    if (!jtree)
        elog(ERROR, "Invalid json document in OSRM response!");

    jobj = json_object_object_get(jtree, "version");
    if (!jobj)
        t = NULL;
    else
        t = pstrdup(json_object_get_string(jobj));

    json_object_put(jtree);

    return t;
}


static char *jget_status(char *json, int *status)
{
    struct json_object *jtree;
    struct json_object *jobj;
    char *t;

    jtree = json_tokener_parse(json);
    if (!jtree)
        elog(ERROR, "Invalid json document in OSRM response!");

    jobj = json_object_object_get(jtree, "status");
    if (!jobj) {
        *status = -1;
        json_object_put(jtree);
        return pstrdup("Error parsing OSRM response, \"status\" not found.");
    }
        
    *status = json_object_get_int(jobj);

    jobj = json_object_object_get(jtree, "status_message");
    if (!jobj)
        t = pstrdup("Error parsing OSRM response, \"status_message\" not found.");
    else
        t = pstrdup(json_object_get_string(jobj));

    json_object_put(jtree);

    // return the status message
    return t;
}


static int jget_mapped_loc(char *json, float8 *lat, float8 *lon)
{
    struct json_object *jtree;
    struct json_object *jobj;
    struct json_object *ja;

    jtree = json_tokener_parse(json);
    if (!jtree)
        elog(ERROR, "Invalid json document in OSRM response!");

    jobj = json_object_object_get(jtree, "mapped_coordinate");
    if (json_object_get_type(jobj) != json_type_array ||
        json_object_array_length(jobj) != 2) {
        json_object_put(jtree);
        return -1;
    }

    ja = json_object_array_get_idx(jobj, 0);
    if (!ja) {
        json_object_put(jtree);
        return -1;
    }
    *lat = json_object_get_double(ja);

    ja = json_object_array_get_idx(jobj, 1);
    if (!ja) {
        json_object_put(jtree);
        return -1;
    }
    *lon = json_object_get_double(ja);

    json_object_put(jtree);

    return 0;
}


static char *jget_name(char *json)
{
    struct json_object *jtree;
    struct json_object *jobj;
    char *t;

    jtree = json_tokener_parse(json);
    if (!jtree)
        elog(ERROR, "Invalid json document in OSRM response!");

    jobj = json_object_object_get(jtree, "name");
    if (!jobj)
        t = NULL;
    else
        t = pstrdup( json_object_get_string(jobj) );

    json_object_put(jtree);

    return t;
}

#define ALLOC_CHUNK 500

static int decode_geom(const char *encoded, int prec, float8 **llat, float8 **llon)
{
    float8 *fp;
    float8 precision = pow(10, -prec);
    int len = strlen(encoded);
    int index = 0;
    float8 lat = 0.0;
    float8 lon = 0.0;
    int maxll = ALLOC_CHUNK;
    int cnt = 0;

    if (!encoded) return -1;

    DBG("-- about to allocate llat for maxll: %d", maxll);
    *llat = palloc( maxll * sizeof(float8) );
    if (! *llat) return -1;

    DBG("-- about to allocate llon for maxll: %d", maxll);
    *llon = palloc( maxll * sizeof(float8) );
    if (! llon) {
        pfree(*llat);
        *llat = NULL;
        return -1;
    }

    DBG("-- start decoding geometry");
    while (index < len) {
        int b;
        int shift = 0;
        int result = 0;
        int dlat;
        int dlon;

        do {
            b = encoded[index++] - 63;
            result |= (b & 0x1f) << shift;
            shift += 5;
        } while (b >= 0x20);
        dlat = ((result & 1) ? ~(result >> 1) : (result >> 1));
        lat += dlat;

        shift = 0;
        result = 0;
        do {
            b = encoded[index++] - 63;
            result |= (b & 0x1f) << shift;
            shift += 5;
        } while (b >= 0x20);
        dlon = ((result & 1) ? ~(result >> 1) : (result >> 1));
        lon += dlon;

        /* extend the arrays if we filled them up */
        if (cnt >= maxll) {
            maxll += ALLOC_CHUNK;
            DBG("-- repalloc llat and llon for maxll: %d", maxll);
            fp = repalloc( *llat, maxll * sizeof(float8) );
            if (! fp) {
                if (*llat) free(*llat);
                if (*llon) free(*llon);
                return -1;
            }
            *llat = fp;
            fp = repalloc( *llon, maxll * sizeof(float8) );
            if (! fp) {
                if (*llat) free(*llat);
                if (*llon) free(*llon);
                return -1;
            }
            *llon = fp;
        }

        DBG("-- cnt: %d, llat[cnt]: %.6lf, llon[cnt]: %.6lf",
            cnt, (float8)lat * precision, (float8)lon * precision);
        (*llat)[cnt] = (float8)lat * precision;
        (*llon)[cnt] = (float8)lon * precision;
        cnt++;
    }

    return cnt;
}

/*
"route_summary":{"total_distance":76332,"total_time":4676,"start_point":"N20","end_point":"N28"}
"alternative_summaries":[]
*/


static int jget_summary_fields(route_summary *s, struct json_object *jtree)
{
    struct json_object *jobj;

    jobj = json_object_object_get(jtree, "total_distance");
    if (!jobj) {
        DBG("-- Failed to find 'total_distance' key in json document!");
        return 1;
    }
    s->tot_distance = json_object_get_int(jobj);

    jobj = json_object_object_get(jtree, "total_time");
    if (!jobj) {
        DBG("-- Failed to find 'total_time' key in json document!");
        return 1;
    }
    s->tot_time = json_object_get_int(jobj);

    jobj = json_object_object_get(jtree, "start_point");
    if (!jobj)
        s->start = pstrdup( "" );
    else
        s->start = pstrdup( json_object_get_string(jobj) );

    jobj = json_object_object_get(jtree, "end_point");
    if (!jobj)
        s->end = pstrdup( "" );
    else
        s->end = pstrdup( json_object_get_string(jobj) );

    return 0;
}


static route_summary *jget_summary(char *json, bool alt, int *nresults)
{
    struct json_object *jtree;
    struct json_object *jobj;
    struct json_object *ja;
    route_summary *s;
    int cnt = 0;
    int i;

    jtree = json_tokener_parse(json);
    if (!jtree)
        elog(ERROR, "Invalid json document in OSRM response!");

    if (alt) {
        jobj = json_object_object_get(jtree, "alternative_summaries");
        if (!jobj) {
            DBG("-- Failed to find 'alternative_summaries' key in json document!");
            json_object_put(jtree);
            return NULL;
        }
        cnt = json_object_array_length(jobj);
        if (cnt == 0) return NULL;

        s = palloc( cnt * sizeof(route_summary) );
        *nresults = cnt;

        for (i=0; i<cnt; i++) {
            ja = json_object_array_get_idx(jobj, i);
            if (jget_summary_fields(&(s[i]), ja)) {
                json_object_put(jtree);
                return NULL;
            }
        }
    }
    else {
        jobj = json_object_object_get(jtree, "route_summary");
        if (!jobj) {
            DBG("-- Failed to find 'route_summary' key in json document!");
            json_object_put(jtree);
            return NULL;
        }
        cnt = 1;
        if (cnt == 0) return NULL;

        s = palloc( cnt * sizeof(route_summary) );
        *nresults = cnt;

        if (jget_summary_fields(s, jobj)) {
            json_object_put(jtree);
            return NULL;
        }
    }

    json_object_put(jtree);
    return s;
}


static void pfree_route_summary(route_summary *rs, int cnt)
{
    int i;

    if (!rs) return;

    for (i=0; i<cnt; i++) {
        if (rs[i].start) pfree(rs[i].start);
        if (rs[i].end)   pfree(rs[i].end);
    }
    pfree(rs);
}

/*
"route_name":["H22","V13"],
"alternative_names":[["V11","H29"]],
*/


static route_names *jget_route_names(char *json, bool alt, int *nresults)
{
    struct json_object *jtree;
    struct json_object *jobj;
    struct json_object *ja;
    route_names *rn;
    int cnt = 0;
    int i, j, ncnt;

    jtree = json_tokener_parse(json);
    if (!jtree)
        elog(ERROR, "Invalid json document in OSRM response!");

    if (alt) {
        jobj = json_object_object_get(jtree, "alternative_names");
        if (!jobj) {
            DBG("-- Failed to find 'alternative_names' key in json document!");
            json_object_put(jtree);
            return NULL;
        }
        cnt = json_object_array_length(jobj);
        if (cnt == 0) return NULL;

        rn = palloc( cnt * sizeof(route_names) );
        *nresults = cnt;

        for (j=0; j<cnt; j++) {
            ja = json_object_array_get_idx(jobj, j);

            rn[j].cnt = ncnt = json_object_array_length(ja);
            rn[j].names = (char **) palloc( ncnt * sizeof(char *) );

            for (i=0; i<ncnt; i++) {
                rn[j].names[i] = pstrdup( json_object_get_string(
                        json_object_array_get_idx(ja, i) ) );
            }
        }
    }
    else {
        jobj = json_object_object_get(jtree, "route_name");
        if (!jobj) {
            DBG("-- Failed to find 'route_name' key in json document!");
            json_object_put(jtree);
            return NULL;
        }
        cnt = 1;
        if (cnt == 0) return NULL;

        rn = palloc( cnt * sizeof(route_names) );
        *nresults = cnt;

        rn[0].cnt = ncnt = json_object_array_length(jobj);
        rn[0].names = (char **) palloc( ncnt * sizeof(char *) );

        for (i=0; i<ncnt; i++) {
            rn[0].names[i] = pstrdup( json_object_get_string(
                    json_object_array_get_idx(jobj, i) ) );
        }
    }

    json_object_put(jtree);
    return rn;
}



/*
"route_instructions": [
  ["10","N20",5506,0,2915,"5506m","E",90],
  ["3","N60",6542,2,562,"6542m","S",180],
  ["7","N2",4771,3,146,"4771m","E",90],
  ["7","N61",58884,4,1797,"58884m","N",0],
  ["3","N28",627,13,153,"627m","E",90],
  ["15","",0,14,0,"","N",0.0]
],
"alternative_instructions":[],
*/


static int jget_instruction_fields(route_instructions *ri, json_object *jtree)
{
    int i, j, jn;
    json_object *ja;
    json_object *jb;

    if (json_object_get_type(jtree) != json_type_array) {
        DBG("-- jget_instruction_fields was passed a none array object!");
        return 1;
    }
    if (json_object_array_length(jtree) == 0) {
        DBG("-- jget_instruction_fields was passed an array of zero length!");
        return 1;
    }

    ri->num       = json_object_array_length(jtree);
    ri->direction = palloc( ri->num * sizeof(int) );
    ri->name      = palloc( ri->num * sizeof(char *) );
    ri->meters    = palloc( ri->num * sizeof(int) );
    ri->position  = palloc( ri->num * sizeof(int) );
    ri->time      = palloc( ri->num * sizeof(int) );
    ri->length    = palloc( ri->num * sizeof(char *) );
    ri->dir       = palloc( ri->num * sizeof(char *) );
    ri->azimuth   = palloc( ri->num * sizeof(float) );

    //DBG("-- jget_instruction_fields: allocated memory for %d instructions.", ri->num);

    for (i=0; i<ri->num; i++) {
        ja = json_object_array_get_idx(jtree, i);
        if (json_object_get_type(ja) != json_type_array) {
            DBG("-- jget_instruction_fields: instruction was not an array!");
            return 1;
        }
        jn = json_object_array_length(ja);
        if (jn != 8) {
            DBG("-- jget_instruction_fields: instruction array not 8 elements!");
            return 1;
        }
        for (j=0; j<8; j++) {
            jb = json_object_array_get_idx(ja, j);
            // DBG("-- looping (%d, %d)", i, j);
            switch (j) {
                case 0:
                    ri->direction[i] = json_object_get_int(jb);
                    break;
                case 1:
                    ri->name[i] = pstrdup( json_object_get_string(jb) );
                    break;
                case 2:
                    ri->meters[i] = json_object_get_int(jb);
                    break;
                case 3:
                    ri->position[i] = json_object_get_int(jb);
                    break;
                case 4:
                    ri->time[i] = json_object_get_int(jb);
                    break;
                case 5:
                    ri->length[i] = pstrdup( json_object_get_string(jb) );
                    break;
                case 6:
                    ri->dir[i] = pstrdup( json_object_get_string(jb) );
                    break;
                case 7:
                    ri->azimuth[i] = (float) json_object_get_double(jb);
                    break;
            }
        }
    }
    return 0;
}


static route_instructions *jget_instructions(char *json, bool alt, int *nresults)
{
    struct json_object *jtree;
    struct json_object *jobj;
    struct json_object *ja;
    route_instructions *ri;
    int cnt = 0;
    int i;

    jtree = json_tokener_parse(json);
    if (!jtree)
        elog(ERROR, "Invalid json document in OSRM response!");

    if (alt) {
        jobj = json_object_object_get(jtree, "alternative_instructions");
        if (!jobj) {
            DBG("-- Failed to find 'alternative_instructions' key in json document!");
            json_object_put(jtree);
            return NULL;
        }
        cnt = json_object_array_length(jobj);
        if (cnt == 0) return NULL;

        ri = palloc( cnt * sizeof(route_instructions) );
        *nresults = cnt;

        for (i=0; i<cnt; i++) {
            ja = json_object_array_get_idx(jobj, i);
            if (jget_instruction_fields(&(ri[i]), ja)) {
                json_object_put(jtree);
                return NULL;
            }
        }
    }
    else {
        jobj = json_object_object_get(jtree, "route_instructions");
        if (!jobj) {
            DBG("-- Failed to find 'route_instructions' key in json document!");
            json_object_put(jtree);
            return NULL;
        }
        cnt = 1;
        if (cnt == 0) return NULL;

        ri = palloc( cnt * sizeof(route_instructions) );
        *nresults = cnt;

        if (jget_instruction_fields(ri, jobj)) {
            json_object_put(jtree);
            return NULL;
        }
    }

    json_object_put(jtree);
    return ri;
}


static route_geom *jget_route_geometry(char *json, bool alt)
{
    route_geom *rg;
    int rt_cnt;
    float8 **rt_lat = NULL;
    float8 **rt_lon = NULL;
    int *rt_cnts = NULL;
    int i, j;

    struct json_object *jtree;
    struct json_object *jobj;
    struct json_object *ja;
    const char *t;
    char *key = alt ? "alternative_geometries" : "route_geometry";

    jtree = json_tokener_parse(json);
    if (!jtree)
        elog(ERROR, "Invalid json document in OSRM response!");

    jobj = json_object_object_get(jtree, key);
    if (!jobj) {
        DBG("-- Failed to find '%s' key in json document!", key);
        json_object_put(jtree);
        return NULL;
    }
    DBG("-- key: %s", key);
    DBG("-- val: %s", json_object_to_json_string(jobj));

    rt_cnt = 0;
    if (alt) {
        if (json_object_get_type(jobj) == json_type_array) {
            int i;
            rt_cnt = json_object_array_length(jobj);
            if (rt_cnt > 0) {
                rt_lat = palloc( rt_cnt * sizeof(float8 *) );
                rt_lon = palloc( rt_cnt * sizeof(float8 *) );
                rt_cnts = palloc( rt_cnt * sizeof(int *) );
                for (i=0; i<rt_cnt; i++) {
                    ja = json_object_array_get_idx(jobj, i);
                    t = json_object_get_string(ja);
                    rt_cnts[i] = decode_geom(t, 6, &(rt_lat[i]), &(rt_lon[i]));
                }
            }
        }
    }
    else {
        DBG("json_object_get_type(jobj): %d, json_type_string: %d",
            json_object_get_type(jobj), json_type_string);
        if (json_object_get_type(jobj) == json_type_string) {
            rt_cnt = 1;
            rt_lat = palloc( 1 * sizeof(float8 *) );
            rt_lon = palloc( 1 * sizeof(float8 *) );
            rt_cnts = palloc( 1 * sizeof(int *) );
            t = json_object_get_string(jobj);
            DBG("-- rg-text: %s", t);
            rt_cnts[0] = decode_geom(t, 6, &(rt_lat[0]), &(rt_lon[0]));
        }
    }
    json_object_put(jtree);

    DBG("-- rt_cnt: %d", rt_cnt);

    if (rt_cnt) {
        int sum = 0;
        for (i=0; i<rt_cnt; i++) {
            if (rt_cnts[i] < 0) {
                return NULL;
            }
            sum += rt_cnts[i];
        }
        rg = malloc( sizeof(route_geom) );
        if (!rg) {
            elog(ERROR, "Failed to allocate memory!");
        }

        rg->rid = malloc( sum * sizeof(int) );
        rg->lat = malloc( sum * sizeof(float8) );
        rg->lon = malloc( sum * sizeof(float8) );
        if (!rg->rid || !rg->lat || !rg->lon) {
            if (rg->rid) free(rg->rid);
            if (rg->lat) free(rg->lat);
            if (rg->lon) free(rg->lon);
            free(rg);
            elog(ERROR, "Failed to allocate memory!");
        }

        rg->rows = sum;
        sum = 0;
        for (i=0; i<rt_cnt; i++) {
            DBG("-- rt_cnts[i]: %d", rt_cnts[i]);
            for (j=0; j<rt_cnts[i]; j++) {
                rg->rid[sum] = i;
                rg->lat[sum] = rt_lat[i][j];
                rg->lon[sum] = rt_lon[i][j];
                DBG("-- rid: %d, seq: %d, lat: %.6lf, lon: %.6lf",
                    i, sum, rg->lat[sum], rg->lon[sum]);
                sum++;
            }
            pfree(rt_lat[i]);
            pfree(rt_lon[i]);
        }
        pfree(rt_lat);
        pfree(rt_lon);
        pfree(rt_cnts);

        return rg;
    }
    
    return NULL;
}


static char *callOSRM(char *url)
{
    int status;
    char *mess;

    char *json = curl_do_actual_work(OSRMCURL_GET, url, NULL);
    if (!json || strlen(json) == 0) {
        elog(ERROR, "OSRM request failed to return a document!");
    }

    mess = jget_status(json, &status);
    if (status != 0) {
        if ( !mess ) mess = "";
        elog(ERROR, "OSRM request failed with status: (%d) %s", status, mess);
    }

    return json;
}


static DTYPE *get_pgarray(int *num, ArrayType *input)
{
    int        *dims;
    bool       *nulls = NULL;
    Oid         i_eltype;
    int16       i_typlen;
    bool        i_typbyval;
    char        i_typalign;
    Datum      *i_data;
    int         i, n;
    DTYPE      *data;
#ifdef DEBUG
    int         ndims;
    int        *lbs;
#endif

    /* get input array element type */
    i_eltype = ARR_ELEMTYPE(input);
    get_typlenbyvalalign(i_eltype, &i_typlen, &i_typbyval, &i_typalign);


    /* validate input data type */
    switch(i_eltype){
    case INT2OID:
    case INT4OID:
    case FLOAT4OID:
    case FLOAT8OID:
            break;
    default:
            elog(ERROR, "Invalid input array data type");
            break;
    }

    /* get various pieces of data from the input array */
    dims = ARR_DIMS(input);
#ifdef DEBUG
    ndims = ARR_NDIM(input);
    lbs = ARR_LBOUND(input);
#endif

#if 0
    if (ndims != 2 || dims[0] != dims[1]) {
        elog(ERROR, "Error: matrix[num][num] in its definition.");
    }
#endif

    /* get src data */
    deconstruct_array(input, i_eltype, i_typlen, i_typbyval, i_typalign,
&i_data, NULL, &n);

    DBG("get_pgarray: ndims=%d, n=%d", ndims, n);

#ifdef DEBUG
    for (i=0; i<ndims; i++) {
        DBG("   dims[%d]=%d, lbs[%d]=%d", i, dims[i], i, lbs[i]);
    }
#endif

    /* construct a C array */
    data = (DTYPE *) palloc(n * sizeof(DTYPE));
    if (!data) {
        elog(ERROR, "Error: Out of memory!");
    }

    for (i=0; i<n; i++) {
        if (nulls && nulls[i]) {
            data[i] = INFINITY;
        }
        else {
            switch(i_eltype){
                case INT2OID:
                    data[i] = (DTYPE) DatumGetInt16(i_data[i]);
                    break;
                case INT4OID:
                    data[i] = (DTYPE) DatumGetInt32(i_data[i]);
                    break;
                case FLOAT4OID:
                    data[i] = (DTYPE) DatumGetFloat4(i_data[i]);
                    break;
                case FLOAT8OID:
                    data[i] = (DTYPE) DatumGetFloat8(i_data[i]);
                    break;
            }
        }
        DBG("    data[%d]=%.4f", i, data[i]);
    }

    if (nulls) pfree(nulls);
    pfree(i_data);

    *num = dims[0];

    return data;
}


/* ******************* dmatrix functions ********************* */

static char **jget_hints(char *json, int *nhints)
{
    struct json_object *jtree;
    struct json_object *jobj;
    char **hints;
    int i, num;

    jtree = json_tokener_parse(json);
    if (!jtree)
        elog(ERROR, "Invalid json document in OSRM response!");

    jobj = json_object_object_get(jtree, "hint_data");
    if (!jobj) {
        json_object_put(jtree);
        DBG("Could not find \"hint_data\" in json document!");
        *nhints = 0;
        return NULL;
    }

    jobj = json_object_object_get(jobj, "locations");
    if (!jobj) {
        json_object_put(jtree);
        DBG("Could not find \"locations\" key in \"hint_data\"!");
        *nhints = 0;
        return NULL;
    }

    num = json_object_array_length(jobj);
    if (!num) {
        json_object_put(jtree);
        *nhints = 0;
        return NULL;
    }

    hints = palloc( num * sizeof(char *) );
    for (i=0; i<num; i++) {
        hints[i] = pstrdup(json_object_get_string(json_object_array_get_idx(jobj, i)));
    }

    *nhints = num;
    return hints;
}


static char *makeViaRouteUrl(float8 lat1, float8 lon1, float8 lat2, float8 lon2, char *hint1, char *hint2, char *baseurl, int zoom, bool inst, bool alt)
{
    int len;
    char *url;

    if (zoom <  0) zoom =  0;
    if (zoom > 18) zoom = 18;

    len = strlen(baseurl) + 256;
    url = (char *) palloc( len * sizeof(char) );

    snprintf(url, len, "%s/viaroute?z=%d&instructions=%s&alt=%s",
        baseurl,
        zoom,
        inst ? "true" : "false",
        alt ? "true" : "false"
    );


    snprintf(url + strlen(url), len - strlen(url),
        "&loc=%.6lf,%.6lf", lat1, lon1);

/*
    if (hint1) {
        snprintf(url + strlen(url), len - strlen(url), "&hint=%s", hint1);
        DBG("-- added hint1");
    }
*/

    snprintf(url + strlen(url), len - strlen(url),
        "&loc=%.6lf,%.6lf", lat2, lon2);

/*
    if (hint2) {
        snprintf(url + strlen(url), len - strlen(url), "&hint=%s", hint2);
        DBG("-- added hint2");
    }
*/
    return url;
}


static dmatrix_cache *buildDmatrixCache(int cnt, float8 *lat, float8 *lon, char *baseurl, bool dist, bool inst)
{
    int i, j;
    dmatrix_cache *cache;
    char **hints;
    char **thesehints;
    int nhints = 0;
    char *url;

    cache = palloc( sizeof(dmatrix_cache) );
    cache->json = palloc( cnt * cnt * sizeof(char *) );
    for (i=0; i<cnt*cnt; i++) cache->json[i] = NULL;

    cache->cnt = cnt;
    cache->failures = 0;

    hints = palloc( sizeof(char *) );
    for (i=0; i<cnt; i++) hints[i] = NULL;

    for (i=0; i<cnt; i++) {
        for (j=0; j<cnt; j++) {
            if (i == j) continue;

            url = makeViaRouteUrl(lat[i], lon[i], lat[j], lon[j], 
                hints[i], hints[j], baseurl, 18, inst, false );

            //DBG("-- URL(%d, %d): %s", i, j, url);

            cache->json[i*cnt + j] = callOSRM(url);
            pfree(url);

            if (!cache->json[i*cnt + j]) {
                cache->failures++;
                DBG("-- back from callOSRM(): FAILED!");
                continue;
            }
            thesehints = jget_hints(cache->json[i*cnt + j], &nhints);
            if (thesehints) {
                if (!hints[i] && nhints>0) hints[i] = thesehints[0];
                if (!hints[j] && nhints>1) hints[j] = thesehints[1];
            }
        }
    }

    return cache;
}


static float8 *dmatrixFromCache(dmatrix_cache *cache, bool dist, bool symmetric)
{
    int i, j, cnt;
    float8 *dm;
    route_summary *rs;
    int nrs = 0;

    cnt = cache->cnt;
    dm = palloc( cnt * cnt * sizeof(float8) );

    for (i=0; i<cnt; i++) {
        for (j=0; j<cnt; j++) {
            if (i == j) {
                dm[i*cnt + j] = 0.0;
            }
            else if (!cache->json[i*cnt + j]) {
                dm[i*cnt + j] = -1.0;
            }
            else {
                rs = jget_summary(cache->json[i*cnt + j], false, &nrs);
                if (rs)
                    dm[i*cnt + j] = (float8) (dist ? rs[0].tot_distance
                                                   : rs[0].tot_time);
                else
                    dm[i*cnt + j] = -1.0;
                pfree_route_summary(rs, nrs);
            }
        }
    }

    if (symmetric) {
        for (i=0; i<cnt; i++) {
            for (j=0; j<cnt; j++) {
                if (dm[i*cnt + j] == -1.0 && dm[j*cnt + i] == -1.0) {
                    /* error */
                }
                else if (dm[i*cnt + j] == -1.0) {
                    dm[i*cnt + j] = dm[j*cnt + i];
                }
                else if (dm[j*cnt + i] == -1.0) {
                    dm[j*cnt + i] = dm[i*cnt + j];
                }
                else {
                    dm[i*cnt + j] = dm[j*cnt + i] =
                        (dm[i*cnt + j] + dm[j*cnt + i]) / 2.0;
                }
            }
        }
    }

    return dm;
}

/* ******************* public functions ********************** */

/*
CREATE OR REPLACE FUNCTION osrm_locate(
        IN lat float8,
        IN lon float8,
        IN osrmhost text default 'http://localhost:5000',
        OUT m_lat float8,
        OUT m_lon float8
    ) RETURNS RECORD
    AS '$libdir/osrm', 'osrm_locate'
    LANGUAGE c IMMUTABLE STRICT;
*/

Datum osrm_locate(PG_FUNCTION_ARGS)
{
    char *url;
    char *json;
    float8 mlat, mlon;
    int ret;
    float8 lat;
    float8 lon;
    char *baseurl;
    int url_len;

    TupleDesc   tuple_desc;
    HeapTuple   tuple;
    Datum       result;
    Datum       *values;
    bool        *nulls;

    if (get_call_result_type(fcinfo, NULL, &tuple_desc) != TYPEFUNC_COMPOSITE)
        ereport(ERROR,
            (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
            errmsg("function returning record called in context "
                   "that cannot accept type record")));

    lat = PG_GETARG_FLOAT8(0);
    lon = PG_GETARG_FLOAT8(1);
    DBG("-- lat: %.6lf, lon: %.6lf", lat, lon);

    baseurl = text2char(PG_GETARG_TEXT_P(2));
    DBG("-- baseurl: %s", baseurl);

    url_len = strlen(baseurl) + 50;
    DBG("-- url_len: %d (%ld)", url_len, url_len * sizeof(char));

    url = (char *) palloc( url_len * sizeof(char) );
    snprintf(url, url_len, "%s/locate?loc=%.6lf,%.6lf", baseurl, lat, lon);
    DBG("-- url: %s", url);

    json = callOSRM( url );
    DBG("-- json: %s", json);

    ret = jget_mapped_loc( json, &mlat, &mlon );
    DBG("-- jget_mapped_loc ret: %d, mlat: %.6lf, mlon: %.6lf", ret, mlat, mlon);
    if (ret)
        elog(ERROR, "Failed to parse OSRM json document!");

    BlessTupleDesc( tuple_desc );

    values = palloc(2 * sizeof(Datum));
    nulls = palloc(2 * sizeof(bool));

    values[0] = Float8GetDatum( mlat );
    nulls[0] = false;
    values[1] = Float8GetDatum( mlon );
    nulls[1] = false;

    tuple = heap_form_tuple( tuple_desc, values, nulls );
    result = HeapTupleGetDatum( tuple );

    PG_RETURN_DATUM( result );
}


/*
CREATE OR REPLACE FUNCTION osrm_nearest(
        IN lat float8,
        IN lon float8,
        IN osrmhost text default 'http://localhost:5000',
        OUT m_lat float8,
        OUT m_lon float8,
        OUT name text
    ) RETURNS RECORD
    AS '$libdir/osrm', 'osrm_nearest'
    LANGUAGE c IMMUTABLE STRICT;
*/

Datum osrm_nearest(PG_FUNCTION_ARGS)
{
    char *url;
    char *json;
    char *name;
    float8 mlat, mlon;
    int ret;
    char *baseurl;
    float8 lat;
    float8 lon;
    int url_len;

    TupleDesc   tuple_desc;
    HeapTuple   tuple;
    Datum       result;
    Datum       *values;
    bool        *nulls;

    if (get_call_result_type(fcinfo, NULL, &tuple_desc) != TYPEFUNC_COMPOSITE)
        ereport(ERROR,
            (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
            errmsg("function returning record called in context "
                   "that cannot accept type record")));

    lat = PG_GETARG_FLOAT8(0);
    lon = PG_GETARG_FLOAT8(1);
    DBG("-- lat: %.6lf, lon: %.6lf", lat, lon);

    baseurl = text2char(PG_GETARG_TEXT_P(2));
    DBG("-- baseurl: %s", baseurl);

    url_len = strlen(baseurl) + 50;
    DBG("-- url_len: %d (%ld)", url_len, url_len * sizeof(char));

    url = (char *) palloc( url_len * sizeof(char) );
    snprintf(url, url_len, "%s/nearest?loc=%.6lf,%.6lf", baseurl, lat, lon);
    DBG("-- url: %s", url);

    json = callOSRM( url );
    DBG("-- json: %s", json);

    ret = jget_mapped_loc( json, &mlat, &mlon );
    DBG("-- jget_mapped_loc ret: %d, mlat: %.6lf, mlon: %.6lf", ret, mlat, mlon);
    if (ret)
        elog(ERROR, "Failed to parse OSRM json document!");

    name = jget_name( json );
    if (! name)
        name = pstrdup( "" );
    DBG("-- name: %s", name);

    BlessTupleDesc( tuple_desc );

    values = palloc(3 * sizeof(Datum));
    nulls = palloc(3 * sizeof(bool));

    values[0] = Float8GetDatum( mlat );
    nulls[0] = false;
    values[1] = Float8GetDatum( mlon );
    nulls[1] = false;
    values[2] = PointerGetDatum(cstring_to_text( name ));
    nulls[2] = strlen( name ) ? false : true;

    tuple = heap_form_tuple( tuple_desc, values, nulls );
    result = HeapTupleGetDatum( tuple );

    PG_RETURN_DATUM( result );
}


/*
CREATE OR REPLACE FUNCTION osrm_viaroute(
        IN plat float8[],
        IN plon float8[],
        IN alt boolean default false,
        IN instructions boolean default false,
        IN zoom integer default 18
        IN osrmhost text default 'http://localhost:5000'
    ) RETURNS TEXT
    AS '$libdir/osrm', 'osrm_viaroute'
    LANGUAGE c IMMUTABLE STRICT;
*/

Datum osrm_viaroute(PG_FUNCTION_ARGS)
{
    char *url;
    char *json;
    int nlat, nlon;
    int len;
    int i;
    char *baseurl;

    DTYPE *lat     = get_pgarray(&nlat, PG_GETARG_ARRAYTYPE_P(0));
    DTYPE *lon     = get_pgarray(&nlon, PG_GETARG_ARRAYTYPE_P(1));
    bool alt       = PG_GETARG_BOOL(2);
    bool inst      = PG_GETARG_BOOL(3);
    int zoom       = PG_GETARG_INT32(4);

    if (nlat != nlon || nlat == 0) {
        elog(ERROR, "lat[] and lon[] arrays must be the same length!");
    }

    baseurl = text2char(PG_GETARG_TEXT_P(5));

    if (zoom <  0) zoom =  0;
    if (zoom > 18) zoom = 18;

    /* allocate a little more than we will probably need */
    len = strlen(baseurl) + 100 + nlat * 30;
    url = (char *) palloc( len * sizeof(char) );
    snprintf(url, len, "%s/viaroute?z=%d&instructions=%s&alt=%s",
        baseurl,
        zoom,
        inst ? "true" : "false",
        alt ? "true" : "false"
    );

    for (i=0; i<nlat; i++) {
        snprintf(url + strlen(url), len - strlen(url),
                 "&loc=%.6lf,%.6lf", lat[i], lon[i]);
    }

    DBG( "url: %s", url );

    json = callOSRM( url );

    DBG( "json: %s", json );

    pfree(url);

    PG_RETURN_TEXT_P( cstring_to_text(json) );
}


/*
CREATE OR REPLACE FUNCTION osrm_dmatrixGetJson(
        IN lat float8[],
        IN lon float8[],
        IN dist boolean default false,
        IN inst boolean default false,
        IN osrmhost text default 'http://localhost:5000',
        OUT irow integer,
        OUT icol integer,
        OUT json text
    ) RETURNS SETOF RECORD
    AS '$libdir/osrm', 'osrm_dmatrix_get_json'
    LANGUAGE c IMMUTABLE STRICT;
*/

Datum osrm_dmatrix_get_json(PG_FUNCTION_ARGS)
{
    FuncCallContext     *funcctx;
    int                  call_cntr;
    int                  max_calls;
    TupleDesc            tuple_desc;
    char                *baseurl;
    DTYPE               *lat;
    DTYPE               *lon;
    int                  nlat = 0;
    int                  nlon = 0;
    bool                 dist;
    bool                 inst;
    dmatrix_cache       *dmc;

    /* stuff done only on the first call of the function */
    if (SRF_IS_FIRSTCALL()) {
        MemoryContext   oldcontext;

        /* create a function context for cross-call persistence */
        funcctx = SRF_FIRSTCALL_INIT();

        /* switch to memory context appropriate for multiple function calls */
        oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

        lat = get_pgarray(&nlat, PG_GETARG_ARRAYTYPE_P(0));
        lon = get_pgarray(&nlon, PG_GETARG_ARRAYTYPE_P(1));
        dist = PG_GETARG_BOOL(2);
        inst = PG_GETARG_BOOL(3);
        baseurl = text2char(PG_GETARG_TEXT_P(4));

        if (nlat != nlon || nlat < 2) 
            elog(ERROR, "lat/lon arrays must be equal in length and "
                        "greater than one element, and greater than 4 "
                        "elements for use in TSP.");

        dmc = buildDmatrixCache(nlat, lat, lon, baseurl, dist, inst);

        DBG("buildDmatrixCache: cnt: %d, failures: %d", dmc->cnt, dmc->failures);

        funcctx->max_calls = nlat * nlat;
        funcctx->user_fctx = dmc;

        /* Build a tuple descriptor for our result type */
        if (get_call_result_type(fcinfo, NULL, &tuple_desc) != TYPEFUNC_COMPOSITE)
            ereport(ERROR,
                (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                errmsg("function returning record called in context "
                       "that cannot accept type record")));

        funcctx->tuple_desc = BlessTupleDesc(tuple_desc);

        MemoryContextSwitchTo(oldcontext);
    }

    /* stuff done on every call of the function */
    funcctx = SRF_PERCALL_SETUP();

    call_cntr  = funcctx->call_cntr;
    max_calls  = funcctx->max_calls;
    tuple_desc = funcctx->tuple_desc;
    dmc        = funcctx->user_fctx;

    /* do when there is more left to send */
    if (call_cntr < max_calls) {
        HeapTuple    tuple;
        Datum        result;
        Datum *values;
        bool *nulls;
        int irow = call_cntr / dmc->cnt;
        int icol = call_cntr % dmc->cnt;

        values = palloc(3 * sizeof(Datum));
        nulls = palloc(3 * sizeof(bool));

        values[0] = Int32GetDatum(irow + 1);
        nulls[0] = false;
        values[1] = Int32GetDatum(icol + 1);
        nulls[1] = false;
        if (!dmc->json[call_cntr]) {
            values[2] = PointerGetDatum( cstring_to_text( " " ) );
            nulls[2] = true;
        }
        else {
            values[2] = PointerGetDatum( cstring_to_text( dmc->json[call_cntr] ) );
            nulls[2] = false;
        }

        tuple = heap_form_tuple(tuple_desc, values, nulls);
        result = HeapTupleGetDatum(tuple);

        pfree(values);
        pfree(nulls);

        SRF_RETURN_NEXT(funcctx, result);
    }
    else {
        /* Since dmc is allocated with palloc, PG will clean it up */
        SRF_RETURN_DONE(funcctx);
    }
}



/*
CREATE OR REPLACE FUNCTION osrm_dmatrix(
        IN lat float8[],
        IN lon float8[],
        IN dist boolean default false,
        IN osrmhost text default 'http://localhost:5000'
    ) RETURNS FLOAT8[]
    AS '$libdir/osrm', 'osrm_dmatrix'
    LANGUAGE c IMMUTABLE STRICT;

Datum osrm_dmatrix(PG_FUNCTION_ARGS)
{
    PG_RETURN_NULL();
}

*/

/*
CREATE OR REPLACE FUNCTION osrm_dmatrix(
        IN start_lat float8,
        IN start_lon float8,
        IN lat float8[],
        IN lon float8[],
        IN dist boolean default false,
        IN osrmhost text default 'http://localhost:5000'
    ) RETURNS FLOAT8[]
    AS '$libdir/osrm', 'osrm_dmatrix_row'
    LANGUAGE c IMMUTABLE STRICT;

Datum osrm_dmatrix_row(PG_FUNCTION_ARGS)
{
    PG_RETURN_NULL();
}

*/

/*
CREATE OR REPLACE FUNCTION osrm_jget_version(
        IN json text
    ) RETURNS TEXT
    AS '$libdir/osrm', 'osrm_jget_version'
    LANGUAGE c IMMUTABLE STRICT;
*/

Datum osrm_jget_version(PG_FUNCTION_ARGS)
{
    char *json = text2char(PG_GETARG_TEXT_P(0));
    char *v = jget_version(json);
    if (!v)
        PG_RETURN_NULL();

    PG_RETURN_TEXT_P( cstring_to_text(v) );
}


/*
CREATE OR REPLACE FUNCTION osrm_jget_status(
        IN json text,
        OUT status integer,
        OUT message text
    ) RETURNS RECORD
    AS '$libdir/osrm', 'osrm_jget_status'
    LANGUAGE c IMMUTABLE STRICT;
*/

Datum osrm_jget_status(PG_FUNCTION_ARGS)
{
    TupleDesc   tuple_desc;
    HeapTuple   tuple;
    Datum       result;
    Datum       *values;
    bool        *nulls;

    int status;
    char *json = text2char(PG_GETARG_TEXT_P(0));
    char *mess = jget_status(json, &status);

    if (get_call_result_type(fcinfo, NULL, &tuple_desc) != TYPEFUNC_COMPOSITE)
        ereport(ERROR,
            (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
            errmsg("function returning record called in context "
                   "that cannot accept type record")));

    BlessTupleDesc( tuple_desc );

    values = palloc(2 * sizeof(Datum));
    nulls = palloc(2 * sizeof(bool));

    values[0] = Int32GetDatum( status );
    nulls[0] = false;
    values[1] = PointerGetDatum( cstring_to_text(mess) );
    nulls[1] = (!mess) ? true : false;

    tuple = heap_form_tuple( tuple_desc, values, nulls );
    result = HeapTupleGetDatum( tuple );

    PG_RETURN_DATUM( result );
}


/*
CREATE OR REPLACE FUNCTION osrm_jget_route(
        IN json text,
        IN alt boolean default false
        OUT rid integer,
        OUT seq integer,
        OUT lat float8,
        OUT lon float8
    ) RETURNS SETOF RECORD
    AS '$libdir/osrm', 'osrm_jget_route'
    LANGUAGE c IMMUTABLE STRICT;
*/

Datum osrm_jget_route(PG_FUNCTION_ARGS)
{
    FuncCallContext     *funcctx;
    int                  call_cntr;
    int                  max_calls;
    TupleDesc            tuple_desc;
    char                *json;
    bool                 alt;
    route_geom          *rg;

    /* stuff done only on the first call of the function */
    if (SRF_IS_FIRSTCALL()) {
        MemoryContext   oldcontext;

        /* create a function context for cross-call persistence */
        funcctx = SRF_FIRSTCALL_INIT();

        /* switch to memory context appropriate for multiple function calls */
        oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

        json = text2char(PG_GETARG_TEXT_P(0));
        alt = PG_GETARG_BOOL(1);

        rg = jget_route_geometry(json, alt);

        pfree(json);
        if ( !rg ) {
            DBG("-- jget_route_geometry returned null");
            SRF_RETURN_DONE(funcctx);
        }

        DBG("route_geom: rows: %d", rg->rows);

        funcctx->max_calls = rg->rows;
        funcctx->user_fctx = rg;

        /* Build a tuple descriptor for our result type */
        if (get_call_result_type(fcinfo, NULL, &tuple_desc) != TYPEFUNC_COMPOSITE)
            ereport(ERROR,
                (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                errmsg("function returning record called in context "
                       "that cannot accept type record")));

        funcctx->tuple_desc = BlessTupleDesc(tuple_desc);

        MemoryContextSwitchTo(oldcontext);
    }

    /* stuff done on every call of the function */
    funcctx = SRF_PERCALL_SETUP();

    call_cntr  = funcctx->call_cntr;
    max_calls  = funcctx->max_calls;
    tuple_desc = funcctx->tuple_desc;
    rg         = funcctx->user_fctx;

    /* do when there is more left to send */
    if (call_cntr < max_calls) {
        HeapTuple    tuple;
        Datum        result;
        Datum *values;
        bool *nulls;

        values = palloc(4 * sizeof(Datum));
        nulls = palloc(4 * sizeof(bool));

        values[0] = Int32GetDatum(rg->rid[call_cntr]);
        nulls[0] = false;
        values[1] = Int32GetDatum(call_cntr+1);
        nulls[1] = false;
        values[2] = Float8GetDatum(rg->lat[call_cntr]);
        nulls[2] = false;
        values[3] = Float8GetDatum(rg->lon[call_cntr]);
        nulls[3] = false;

        tuple = heap_form_tuple(tuple_desc, values, nulls);
        result = HeapTupleGetDatum(tuple);

        pfree(values);
        pfree(nulls);

        SRF_RETURN_NEXT(funcctx, result);
    }
    else {
        if ( rg ) {
            if (rg->rid) free(rg->rid);
            if (rg->lat) free(rg->lat);
            if (rg->lon) free(rg->lon);
            free(rg);
        }
        SRF_RETURN_DONE(funcctx);
    }
}


/*
CREATE OR REPLACE FUNCTION osrm_jget_instructions(
        IN json text,
        IN alt boolean default false,
        OUT rid integer,
        OUT seq integer,
        OUT direction integer,
        OUT name text,
        OUT meters integer,
        OUT postion integer,
        OUT time integer,
        OUT length text,
        OUT dir text,
        OUT azimuth float
    ) RETURNS SETOF RECORD
    AS '$libdir/osrm', 'osrm_jget_instructions'
    LANGUAGE c IMMUTABLE STRICT;
*/

Datum osrm_jget_instructions(PG_FUNCTION_ARGS)
{
    FuncCallContext     *funcctx;
    int                  call_cntr;
    int                  max_calls;
    TupleDesc            tuple_desc;
    int                  nresults = 0;
    int                  seq;
    char                *json;
    bool                 alt;
    route_instructions  *ri;

    typedef struct user_ctx_t {
        route_instructions *ri;
        int rid;
        int seq;
    } user_ctx;
    user_ctx *my_ctx;

    if (SRF_IS_FIRSTCALL()) {
        MemoryContext   oldcontext;

        /* create a function context for cross-call persistence */
        funcctx = SRF_FIRSTCALL_INIT();

        /* switch to memory context appropriate for multiple function calls */
        oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

        json = text2char(PG_GETARG_TEXT_P(0));
        alt = PG_GETARG_BOOL(1);
        DBG("-- alt: %s", alt?"true":"false");

        /* allocate a context object to keep track of our stuff */
        my_ctx = palloc(sizeof(user_ctx));

        my_ctx->ri = jget_instructions(json, alt, &nresults);
        my_ctx->rid = 0;
        my_ctx->seq = 0;

        pfree(json);
        if (!my_ctx->ri || nresults == 0) {
            DBG("-- jget_instructions returned null or zero results");
            SRF_RETURN_DONE(funcctx);
        }

        DBG("-- jget_instructions: return %d rows", nresults);

        funcctx->max_calls = nresults;
        funcctx->user_fctx = (void *) my_ctx;

        /* Build a tuple descriptor for our result type */
        if (get_call_result_type(fcinfo, NULL, &tuple_desc) != TYPEFUNC_COMPOSITE)
            ereport(ERROR,
                (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                errmsg("function returning record called in context "
                       "that cannot accept type record")));

        funcctx->tuple_desc = BlessTupleDesc( tuple_desc );

        MemoryContextSwitchTo(oldcontext);
    }

    /* stuff done on every call of the function */
    funcctx = SRF_PERCALL_SETUP();

    call_cntr  = funcctx->call_cntr;
    max_calls  = funcctx->max_calls;
    tuple_desc = funcctx->tuple_desc;
    my_ctx     = funcctx->user_fctx;

    DBG("-- call_cntr: %d, max_calls: %d, rid: %d, seq: %d", call_cntr, max_calls, my_ctx->rid, my_ctx->seq);

    /* do when there is more left to return */
    if (my_ctx->rid < max_calls &&
        my_ctx->seq < my_ctx->ri[my_ctx->rid].num) {

        HeapTuple            tuple;
        Datum                result;
        Datum               *values;
        bool                *nulls;
        
        values = palloc(10 * sizeof(Datum));
        nulls = palloc(10 * sizeof(bool));

        /* get a pointer to the correct route summary for this call */
        ri = &(my_ctx->ri[my_ctx->rid]);
        seq = my_ctx->seq;

/*
        DBG("-- preparing record.");
        DBG("-- ri->name[seq]: %s", ri->name[seq]);
        DBG("-- ri->length[seq]: %s", ri->length[seq]);
        DBG("-- ri->dir[seq]: %s", ri->dir[seq]);
*/

        values[0] = Int32GetDatum( my_ctx->rid +1 );
        nulls[0] = false;
        values[1] = Int32GetDatum( seq + 1 );
        nulls[1] = false;
        values[2] = Int32GetDatum( ri->direction[seq] );
        nulls[2] = false;
        values[3] = PointerGetDatum( cstring_to_text( ri->name[seq] ) );
        nulls[3] = false;
        values[4] = Int32GetDatum( ri->meters[seq] );
        nulls[4] = false;
        values[5] = Int32GetDatum( ri->position[seq] );
        nulls[5] = false;
        values[6] = Int32GetDatum( ri->time[seq] );
        nulls[6] = false;
        values[7] = PointerGetDatum( cstring_to_text( ri->length[seq] ) );
        nulls[7] = false;
        values[8] = PointerGetDatum( cstring_to_text( ri->dir[seq] ) );
        nulls[8] = false;
        values[9] = Float4GetDatum( ri->azimuth[seq] );
        nulls[9] = false;

        //DBG("-- calling heap_form_tuple");
        tuple = heap_form_tuple( tuple_desc, values, nulls );
        //DBG("-- calling HeapTupleGetDatum");
        result = HeapTupleGetDatum( tuple );

        //DBG("-- pfree stuff");
        pfree(values);
        pfree(nulls);

        //DBG("-- incrementing seq");
        my_ctx->seq++;
        if (my_ctx->seq >= ri->num) {
            my_ctx->seq = 0;
            my_ctx->rid++;
        }

#ifdef DEBUG
        if (call_cntr > 1000) {
            DBG("-- call_cntr > 1000 !!!!");
            SRF_RETURN_DONE(funcctx);
        }
#endif

        //DBG("--returning record");
        SRF_RETURN_NEXT(funcctx, result );
    }
    else {
        /* since ri and its contents are all palloc we don't need to pfree them */
        SRF_RETURN_DONE(funcctx);
    }
}


/*
CREATE OR REPLACE FUNCTION osrm_jget_summary(
        IN json text,
        IN alt boolean default false,
        OUT rid integer,
        OUT tot_dist integer,
        OUT tot_time integer,
        OUT start_point text,
        OUT end_point text
    ) RETURNS SETOF RECORD
    AS '$libdir/osrm', 'osrm_jget_summary'
    LANGUAGE c IMMUTABLE STRICT;
*/


Datum osrm_jget_summary(PG_FUNCTION_ARGS)
{
    FuncCallContext     *funcctx;
    int                  call_cntr;
    int                  max_calls;
    TupleDesc            tuple_desc;
    int                  nresults;
    char                *json;
    bool                 alt;
    route_summary       *rs;

    if (SRF_IS_FIRSTCALL()) {
        MemoryContext   oldcontext;

        /* create a function context for cross-call persistence */
        funcctx = SRF_FIRSTCALL_INIT();

        /* switch to memory context appropriate for multiple function calls */
        oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);


        json = text2char(PG_GETARG_TEXT_P(0));
        alt = PG_GETARG_BOOL(1);

        rs = jget_summary(json, alt, &nresults);

        pfree(json);
        if (!rs || nresults == 0) {
            DBG("-- jget_summary returned null or zero results");
            if (rs) pfree(rs);
            SRF_RETURN_DONE(funcctx);
        }

        DBG("-- jget_summary: return %d rows", nresults);

        funcctx->max_calls = nresults;
        funcctx->user_fctx = (void *) rs;

        /* Build a tuple descriptor for our result type */
        if (get_call_result_type(fcinfo, NULL, &tuple_desc) != TYPEFUNC_COMPOSITE)
            ereport(ERROR,
                (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                errmsg("function returning record called in context "
                       "that cannot accept type record")));

        funcctx->tuple_desc = BlessTupleDesc( tuple_desc );

        MemoryContextSwitchTo(oldcontext);
    }

    /* stuff done on every call of the function */
    funcctx = SRF_PERCALL_SETUP();

    call_cntr  = funcctx->call_cntr;
    max_calls  = funcctx->max_calls;
    tuple_desc = funcctx->tuple_desc;
    rs         = (route_summary *) funcctx->user_fctx;

    /* do when there is more left to return */
    if (call_cntr < max_calls) {
        HeapTuple            tuple;
        Datum                result;
        Datum               *values;
        bool                *nulls;
        
        values = palloc(5 * sizeof(Datum));
        nulls = palloc(5 * sizeof(bool));

        /* get a pointer to the correct route summary for this call */
        rs = &(rs[call_cntr]);
    
        values[0] = Int32GetDatum( call_cntr );
        nulls[0] = false;
        values[1] = Int32GetDatum( rs->tot_distance );
        nulls[1] = false;
        values[2] = Int32GetDatum( rs->tot_time );
        nulls[2] = false;
        values[3] = PointerGetDatum( cstring_to_text( rs->start ) );
        nulls[3] = false;
        values[4] = PointerGetDatum( cstring_to_text( rs->end ) );
        nulls[4] = false;

        tuple = heap_form_tuple( tuple_desc, values, nulls );
        result = HeapTupleGetDatum( tuple );

        pfree(values);
        pfree(nulls);

        SRF_RETURN_NEXT(funcctx, result );
    }
    else {
        /* since rs and its contents are all palloc we don't need to pfree them */
        SRF_RETURN_DONE(funcctx);
    }
}


/*
CREATE OR REPLACE FUNCTION osrm_jget_viapoints(
        IN json text,
        OUT seq integer,
        OUT lat float8,
        OUT lon float8
    ) RETURNS SETOF RECORD
    AS '$libdir/osrm', 'osrm_jget_viapoints'
    LANGUAGE c IMMUTABLE STRICT;

"via_points":[[43.235198,-76.400000 ],[43.709579,-76.280000 ]],

*/

Datum osrm_jget_viapoints(PG_FUNCTION_ARGS)
{
    FuncCallContext     *funcctx;
    int                  call_cntr;
    int                  max_calls;
    TupleDesc            tuple_desc;
    int                  num;
    int                  i;
    char                *json;
    struct json_object  *jtree;
    struct json_object  *jobj;
    struct json_object  *ja;
    struct json_object  *jb;
    struct latlon_t {
        float8 lat;
        float8 lon;
    };
    struct latlon_t *latlon;

    if (SRF_IS_FIRSTCALL()) {
        MemoryContext   oldcontext;

        /* create a function context for cross-call persistence */
        funcctx = SRF_FIRSTCALL_INIT();

        /* switch to memory context appropriate for multiple function calls */
        oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);


        json = text2char(PG_GETARG_TEXT_P(0));
        jtree = json_tokener_parse(json);
        if (!jtree)
            elog(ERROR, "Invalid json document in OSRM response!");

        jobj = json_object_object_get(jtree, "via_points");
        if (!jobj) {
            json_object_put(jtree);
            elog(ERROR, "Could not find \"via_points\" in json document!");
        }

        num = json_object_array_length(jobj);
        if (!num) {
            json_object_put(jtree);
            SRF_RETURN_DONE(funcctx);
        }

        latlon = palloc( num * sizeof(struct latlon_t) );
        for (i=0; i<num; i++) {
            ja = json_object_array_get_idx(jobj, i);
            jb = json_object_array_get_idx(ja, 0);
            latlon[i].lat = (float8) json_object_get_double(jb);
            jb = json_object_array_get_idx(ja, 1);
            latlon[i].lon = (float8) json_object_get_double(jb);
        }

        pfree(json);

        DBG("-- jget_via_points: found %d rows", num);

        funcctx->max_calls = num;
        funcctx->user_fctx = (void *) latlon;

        /* Build a tuple descriptor for our result type */
        if (get_call_result_type(fcinfo, NULL, &tuple_desc) != TYPEFUNC_COMPOSITE)
            ereport(ERROR,
                (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                errmsg("function returning record called in context "
                       "that cannot accept type record")));

        funcctx->tuple_desc = BlessTupleDesc( tuple_desc );

        MemoryContextSwitchTo(oldcontext);
    }

    /* stuff done on every call of the function */
    funcctx = SRF_PERCALL_SETUP();

    call_cntr  = funcctx->call_cntr;
    max_calls  = funcctx->max_calls;
    tuple_desc = funcctx->tuple_desc;
    latlon     = (struct latlon_t *) funcctx->user_fctx;

    /* do when there is more left to return */
    if (call_cntr < max_calls) {
        HeapTuple            tuple;
        Datum                result;
        Datum               *values;
        bool                *nulls;
        
        values = palloc(3 * sizeof(Datum));
        nulls = palloc(3 * sizeof(bool));

        values[0] = Int32GetDatum( call_cntr + 1 );
        nulls[0] = false;
        values[1] = Float8GetDatum( latlon[call_cntr].lat );
        nulls[1] = false;
        values[2] = Float8GetDatum( latlon[call_cntr].lon );
        nulls[2] = false;

        tuple = heap_form_tuple( tuple_desc, values, nulls );
        result = HeapTupleGetDatum( tuple );

        pfree(values);
        pfree(nulls);

        SRF_RETURN_NEXT(funcctx, result );
    }
    else {
        /* since rs and its contents are all palloc we don't need to pfree them */
        SRF_RETURN_DONE(funcctx);
    }
}


/*
CREATE OR REPLACE FUNCTION osrm_jget_route_names(
        IN json text,
        IN alt boolean default false,
        OUT rid integer,
        OUT seq integer,
        OUT name text
    ) RETURNS SETOF RECORD
    AS '$libdir/osrm', 'osrm_jget_viapoints'
    LANGUAGE c IMMUTABLE STRICT;

"route_name":["H22","V13"],
"alternative_names":[["V11","H29"]],

*/

Datum osrm_jget_route_names(PG_FUNCTION_ARGS)
{
    FuncCallContext     *funcctx;
    int                  max_calls;
    TupleDesc            tuple_desc;
    int                  nresults = 0;
    char                *json;
    bool                 alt;
    route_names         *rn;

    struct my_ctx_t {
        route_names *rn;
        int rid;
        int seq;
    };
    struct my_ctx_t     *my_ctx;

    if (SRF_IS_FIRSTCALL()) {
        MemoryContext   oldcontext;

        /* create a function context for cross-call persistence */
        funcctx = SRF_FIRSTCALL_INIT();

        /* switch to memory context appropriate for multiple function calls */
        oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);


        json = text2char(PG_GETARG_TEXT_P(0));
        alt = PG_GETARG_BOOL(1);

        my_ctx = palloc(sizeof(struct my_ctx_t));

        my_ctx->rn = jget_route_names(json, alt, &nresults);
        my_ctx->rid = 0;
        my_ctx->seq = 0;

        pfree(json);
        if (!my_ctx->rn || nresults == 0) {
            DBG("-- jget_route_names returned null or zero results");
            SRF_RETURN_DONE(funcctx);
        }

        DBG("-- jget_route_names: return %d rows", nresults);

        funcctx->max_calls = nresults;
        funcctx->user_fctx = (void *) my_ctx;

        /* Build a tuple descriptor for our result type */
        if (get_call_result_type(fcinfo, NULL, &tuple_desc) != TYPEFUNC_COMPOSITE)
            ereport(ERROR,
                (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                errmsg("function returning record called in context "
                       "that cannot accept type record")));

        funcctx->tuple_desc = BlessTupleDesc( tuple_desc );

        MemoryContextSwitchTo(oldcontext);
    }

    /* stuff done on every call of the function */
    funcctx = SRF_PERCALL_SETUP();

    max_calls  = funcctx->max_calls;
    tuple_desc = funcctx->tuple_desc;
    my_ctx     = (struct my_ctx_t *) funcctx->user_fctx;

    /* do when there is more left to return */
    if (my_ctx->rid < max_calls && my_ctx->seq < my_ctx->rn[my_ctx->rid].cnt) {
        HeapTuple            tuple;
        Datum                result;
        Datum               *values;
        bool                *nulls;
        
        values = palloc(3 * sizeof(Datum));
        nulls = palloc(3 * sizeof(bool));

        DBG("-- osrm_jget_route_names: rid: %d, seq: %d", my_ctx->rid, my_ctx->seq);
        /* get a pointer to the correct route summary for this call */
        rn = &(my_ctx->rn[my_ctx->rid]);

        DBG("-- rn->cnt: %d", rn->cnt);
        DBG("-- rn->names[0]: '%s'", rn->names[0]);
    
        values[0] = Int32GetDatum( my_ctx->rid + 1 );
        nulls[0] = false;
        values[1] = Int32GetDatum( my_ctx->seq + 1 );
        nulls[1] = false;
        values[2] = PointerGetDatum( cstring_to_text( rn->names[my_ctx->seq] ));
        nulls[2] = false;

        tuple = heap_form_tuple( tuple_desc, values, nulls );
        result = HeapTupleGetDatum( tuple );

        pfree(values);
        pfree(nulls);

        my_ctx->seq++;
        if (my_ctx->seq >= rn->cnt) {
            my_ctx->rid++;
            my_ctx->seq = 0;
        }

        SRF_RETURN_NEXT(funcctx, result );
    }
    else {
        /* since rn and its contents are all palloc we don't need to pfree them */
        SRF_RETURN_DONE(funcctx);
    }
}


/*
CREATE OR REPLACE FUNCTION osrm_jget_hints(
        IN json text,
        OUT seq integer,
        OUT hint text
    ) RETURNS SETOF RECORD
    AS '$libdir/osrm', 'osrm_jget_hints'
    LANGUAGE c IMMUTABLE STRICT;

"hint_data": {
    "checksum":1944723008,
    "locations": [
        "JA4AADUAAADaKgAA____f18crUSzxeI_freTAoA6cvs", 
        "oBMAADcAAABzIAAAXyEAAALwiPcJjd8_i_SaAkAPdPs"
    ]
}

*/

Datum osrm_jget_hints(PG_FUNCTION_ARGS)
{
    FuncCallContext     *funcctx;
    int                  call_cntr;
    int                  max_calls;
    TupleDesc            tuple_desc;
    int                  num;
    int                  i;
    char                *json;
    struct json_object  *jtree;
    struct json_object  *jobj;
    char               **hints;

    if (SRF_IS_FIRSTCALL()) {
        MemoryContext   oldcontext;

        /* create a function context for cross-call persistence */
        funcctx = SRF_FIRSTCALL_INIT();

        /* switch to memory context appropriate for multiple function calls */
        oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);


        json = text2char(PG_GETARG_TEXT_P(0));
        jtree = json_tokener_parse(json);
        if (!jtree)
            elog(ERROR, "Invalid json document in OSRM response!");

        jobj = json_object_object_get(jtree, "hint_data");
        if (!jobj) {
            json_object_put(jtree);
            elog(ERROR, "Could not find \"hint_data\" in json document!");
        }

        jobj = json_object_object_get(jobj, "locations");
        if (!jobj) {
            json_object_put(jtree);
            elog(ERROR, "Could not find \"locations\" key in \"hint_data\"!");
        }

        num = json_object_array_length(jobj);
        if (!num) {
            json_object_put(jtree);
            SRF_RETURN_DONE(funcctx);
        }

        hints = palloc( num * sizeof(char *) );
        for (i=0; i<num; i++) {
            hints[i] = pstrdup(json_object_get_string(json_object_array_get_idx(jobj, i)));
        }

        pfree(json);

        DBG("-- jget_hints: found %d rows", num);

        funcctx->max_calls = num;
        funcctx->user_fctx = (void *) hints;

        /* Build a tuple descriptor for our result type */
        if (get_call_result_type(fcinfo, NULL, &tuple_desc) != TYPEFUNC_COMPOSITE)
            ereport(ERROR,
                (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                errmsg("function returning record called in context "
                       "that cannot accept type record")));

        funcctx->tuple_desc = BlessTupleDesc( tuple_desc );

        MemoryContextSwitchTo(oldcontext);
    }

    /* stuff done on every call of the function */
    funcctx = SRF_PERCALL_SETUP();

    call_cntr  = funcctx->call_cntr;
    max_calls  = funcctx->max_calls;
    tuple_desc = funcctx->tuple_desc;
    hints      = (char **) funcctx->user_fctx;

    /* do when there is more left to return */
    if (call_cntr < max_calls) {
        HeapTuple            tuple;
        Datum                result;
        Datum               *values;
        bool                *nulls;
        
        values = palloc(2 * sizeof(Datum));
        nulls = palloc(2 * sizeof(bool));

        values[0] = Int32GetDatum( call_cntr + 1 );
        nulls[0] = false;
        values[1] = PointerGetDatum( cstring_to_text( hints[call_cntr] ) );
        nulls[1] = false;

        tuple = heap_form_tuple( tuple_desc, values, nulls );
        result = HeapTupleGetDatum( tuple );

        pfree(values);
        pfree(nulls);

        SRF_RETURN_NEXT(funcctx, result );
    }
    else {
        /* since rs and its contents are all palloc we don't need to pfree them */
        SRF_RETURN_DONE(funcctx);
    }
}


