-- complain if script is sourced in psql, rather than via CREATE EXTENSION
--\echo Use "CREATE EXTENSION osrm" to load this file. \quit

---------------------------------------------------------------------
-- Core functions to access OSRM from postgresql
-- Authoe: Stephen Woodbridge <woodbri@swoodbridge.com>
-- Date: 2013-11-16
---------------------------------------------------------------------

CREATE OR REPLACE FUNCTION osrm_locate(
        IN lat float8,
        IN lon float8,
        IN osrmhost text default 'http://localhost:5000',
        OUT m_lat float8,
        OUT m_lon float8
    ) RETURNS RECORD
    AS '$libdir/osrm', 'osrm_locate'
    LANGUAGE c IMMUTABLE STRICT;

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

CREATE OR REPLACE FUNCTION osrm_viaroute(
        IN plat float8[],
        IN plon float8[],
        IN alt boolean default false,
        IN instructions boolean default false,
        IN zoom integer default 18,
        IN osrmhost text default 'http://localhost:5000'
    ) RETURNS TEXT
    AS '$libdir/osrm', 'osrm_viaroute'
    LANGUAGE c IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION osrm_viaroute(
        IN pnts geometry[],
        IN alt boolean default false,
        IN instructions boolean default false,
        IN zoom integer default 18,
        IN osrmhost text default 'http://localhost:5000'
    ) RETURNS TEXT AS
$$
    WITH a AS (
        SELECT array_agg(ST_Y(g)) AS lat, array_agg(ST_X(g)) AS lon
          FROM unnest($1) AS g
    )
    SELECT osrm_viaroute(a.lat, a.lon, $2, $3, $4, $5) FROM a
$$
    LANGUAGE sql IMMUTABLE STRICT;

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

/*
CREATE OR REPLACE FUNCTION osrm_dmatrix(
        IN lat float8[],
        IN lon float8[],
        IN dist boolean default false,
        IN osrmhost text default 'http://localhost:5000'
    ) RETURNS FLOAT8[]
    AS '$libdir/osrm', 'osrm_dmatrix'
    LANGUAGE c IMMUTABLE STRICT;

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
*/

CREATE OR REPLACE FUNCTION osrm_jgetVersion(
        IN json text
    ) RETURNS TEXT
    AS '$libdir/osrm', 'osrm_jget_version'
    LANGUAGE c IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION osrm_jgetStatus(
        IN json text,
        OUT status integer,
        OUT message text
    ) RETURNS RECORD
    AS '$libdir/osrm', 'osrm_jget_status'
    LANGUAGE c IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION osrm_jgetRoute(
        IN json text,
        IN alt boolean default false,
        OUT rid integer,
        OUT seq integer,
        OUT lat float8,
        OUT lon float8
    ) RETURNS SETOF RECORD
    AS '$libdir/osrm', 'osrm_jget_route'
    LANGUAGE c IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION osrm_jgetInstructions(
        IN json text,
        IN alt boolean default false,
        OUT rid integer,
        OUT seq integer,
        OUT direction integer,
        OUT name text,
        OUT meters integer,
        OUT "position" integer,
        OUT "time" integer,
        OUT length text,
        OUT dir text,
        OUT azimuth float
    ) RETURNS SETOF RECORD
    AS '$libdir/osrm', 'osrm_jget_instructions'
    LANGUAGE c IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION osrm_jgetSummary(
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

CREATE OR REPLACE FUNCTION osrm_jgetViaPoints(
        IN json text,
        OUT seq integer,
        OUT lat float8,
        OUT lon float8
    ) RETURNS SETOF RECORD
    AS '$libdir/osrm', 'osrm_jget_viapoints'
    LANGUAGE c IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION osrm_jgetRouteNames(
        IN json text,
        IN alt boolean default false,
        OUT rid integer,
        OUT seq integer,
        OUT name text
    ) RETURNS SETOF RECORD
    AS '$libdir/osrm', 'osrm_jget_route_names'
    LANGUAGE c IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION osrm_jgetHints(
        IN json text,
        OUT seq integer,
        OUT hint text
    ) RETURNS SETOF RECORD
    AS '$libdir/osrm', 'osrm_jget_hints'
    LANGUAGE c IMMUTABLE STRICT;

