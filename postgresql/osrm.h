#ifndef OSRM_WRAPPER_H
#define OSRM_WRAPPER_H

#define OSRMCURL_GET 1
#define OSRMCURL_PUT 2
#define OSRMCURL_POST 3
#define OSRMCURL_DELETE 4

/* libcurl specific */
#include <curl/curl.h>
/* json-c lib headers */
#include <json/json.h>

/*PostgreSQL specific*/
#include "postgres.h"
#include <inttypes.h>
#include "access/heapam.h"
#include "access/htup.h"
#if PGSQL_VERSION > 92
#include "access/htup_details.h"
#endif
#include "catalog/pg_type.h"
#include "executor/spi.h"
#include "fmgr.h"
#include "funcapi.h"
#include "lib/stringinfo.h"
#include "utils/builtins.h"
#include "utils/datetime.h"
#include "utils/guc.h"
#include "utils/memutils.h"
#include "utils/lsyscache.h"


typedef struct route_geom_t {
    int rows;
    int *rid;
    float8 *lat;
    float8 *lon;
} route_geom;

typedef struct route_summary_t {
    int tot_distance;
    int tot_time;
    char *start;
    char *end;
} route_summary;

typedef struct route_names_t {
    int cnt;
    char **names;
} route_names;

typedef struct route_instructions_t {
    int num;
    int *direction;
    char **name;
    int *meters;
    int *position;
    int *time;
    char **length;
    char **dir;
    float *azimuth;
} route_instructions;

typedef struct dmatrix_cache_t {
    int cnt;
    int failures;
    char **json;
} dmatrix_cache;

typedef struct dmatrix_cache2_t {
    int cnt;
    bool dist;
    char *baseurl;
    float8 *lat;
    float8 *lon;
    char **hints;
    char *ver;
    float8 *dm;
} dmatrix_cache2;

/* Per-backend global state. */
struct osrm_curl_global
{
    /* context in which long-lived state is allocated */
    MemoryContext pg_ctxt;
};
 
struct help_struct {
    char *string;
    int size;
};

void _PG_init(void);
void _PG_fini(void);

Datum osrm_locate           (PG_FUNCTION_ARGS);
Datum osrm_nearest          (PG_FUNCTION_ARGS);
Datum osrm_viaroute         (PG_FUNCTION_ARGS);
Datum osrm_dmatrix_get_json (PG_FUNCTION_ARGS);
Datum osrm_dmatrix_from_sql (PG_FUNCTION_ARGS);
Datum osrm_dmatrix          (PG_FUNCTION_ARGS);
Datum osrm_dmatrix_row      (PG_FUNCTION_ARGS);
Datum osrm_jget_version     (PG_FUNCTION_ARGS);
Datum osrm_jget_status      (PG_FUNCTION_ARGS);
Datum osrm_jget_route       (PG_FUNCTION_ARGS);
Datum osrm_jget_route_text  (PG_FUNCTION_ARGS);
Datum osrm_jget_instructions(PG_FUNCTION_ARGS);
Datum osrm_jget_summary     (PG_FUNCTION_ARGS);
Datum osrm_jget_route_names (PG_FUNCTION_ARGS);
Datum osrm_jget_viapoints   (PG_FUNCTION_ARGS);
Datum osrm_jget_hints       (PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(osrm_locate);
PG_FUNCTION_INFO_V1(osrm_nearest);
PG_FUNCTION_INFO_V1(osrm_viaroute);
PG_FUNCTION_INFO_V1(osrm_dmatrix_get_json);
PG_FUNCTION_INFO_V1(osrm_dmatrix_from_sql);
PG_FUNCTION_INFO_V1(osrm_dmatrix);
PG_FUNCTION_INFO_V1(osrm_dmatrix_row);
PG_FUNCTION_INFO_V1(osrm_jget_version);
PG_FUNCTION_INFO_V1(osrm_jget_status);
PG_FUNCTION_INFO_V1(osrm_jget_route);
PG_FUNCTION_INFO_V1(osrm_jget_route_text);
PG_FUNCTION_INFO_V1(osrm_jget_instructions);
PG_FUNCTION_INFO_V1(osrm_jget_summary);
PG_FUNCTION_INFO_V1(osrm_jget_route_names);
PG_FUNCTION_INFO_V1(osrm_jget_viapoints);
PG_FUNCTION_INFO_V1(osrm_jget_hints);

#endif
