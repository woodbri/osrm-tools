\pset pager off
--create extension postgis;
--create extension osrm;

drop table if exists json cascade;
create table json (
    id serial not null primary key,
    json text
);

select * from osrm_locate(43.235198,-76.420898);

select * from osrm_locate(43.235198,-76.420898, -1);

select * from osrm_nearest(43.235198,-76.420898);

select * from osrm_viaroute(array[43.235198,43.709579], array[-76.420898,-76.286316], true, true);

select * from osrm_viaroute(array[
    st_makepoint(-76.420898,43.235198),
    st_makepoint(-76.286316,43.709579)], true, true);

select * from osrm_viaroute('{43.235198,43.709579}'::float8[], '{-76.420898,-76.286316}'::float8[], true, true);

insert into json (json) select * from osrm_viaroute(array[43.235198,43.709579], array[-76.420898::float8,-76.286316::float8]::float8[], true, true);

select * from osrm_jgetversion((select json from json where id=1));

select * from osrm_jgetstatus((select json from json where id=1));

select * from osrm_jgetroute((select json from json where id=1));

select * from osrm_jgetroute((select json from json where id=1), false);

select * from osrm_jgetroute((select json from json where id=1), true);

select * from osrm_jgetsummary((select json from json where id=1), alt := false);

select * from osrm_jgetinstructions((select json from json where id=1), alt := false);

select * from osrm_jgethints((select json from json where id=1));

select * from osrm_jgetroutenames((select json from json where id=1), alt := false);

select * from osrm_jgetviapoints((select json from json where id=1));

drop table if exists dm_cache cascade;
create table dm_cache (
    id serial not null primary key,
    irow integer,
    icol integer,
    json text
);

-- 1,-75.790558,43.227194;2,-75.205536,43.71355;3,-75.923767,43.348151;4,-75.379944,43.827592;5,-76.043243,43.531625;6,-75.140991,43.528638;7,-75.922394,43.706601;8,-75.264587,43.409038;9,-75.655975,43.798854;10,-75.473328,43.319183

insert into dm_cache (irow, icol, json)
select * from osrm_dmatrixgetjson(
    array[43.227194,43.71355,43.348151,43.827592,43.531625,43.528638,43.706601,43.409038,43.798854,43.319183],
    array[-75.790558,-75.205536,-75.923767,-75.379944,-76.043243,-75.140991,-75.922394,-75.264587,-75.655975,-75.473328],
    false,
    true);

select * from dm_cache order by id asc;


select irow, 
       icol,
       case when json is null then 0
            else (osrm_jgetsummary(json)).tot_time end as cost
  from dm_cache order by irow, icol;
-- 100 rows, 910 ms.

select irow, 
       icol, 
       case when json is null then 0 
            else (osrm_jgetsummary(json)).tot_time end as cost
  from osrm_dmatrixgetjson(
    array[43.227194,43.71355,43.348151,43.827592,43.531625,43.528638,43.706601,43.409038,43.798854,43.319183],
    array[-75.790558,-75.205536,-75.923767,-75.379944,-76.043243,-75.140991,-75.922394,-75.264587,-75.655975,-75.473328],
    false,
    false);
-- 100 rows, 1290 ms


create aggregate array_agg(float8[])
(
    sfunc = array_cat,
    stype = float8[],
    initcond = '{}'
);


select array_agg(array[row]) from (
    select irow, array_agg(cost::float8) as row from (
        select irow, icol, case when json is null then 0 else (osrm_jgetsummary(json)).tot_time end as cost
          from osrm_dmatrixgetjson(
               array[43.227194,43.71355,43.348151,43.827592,43.531625,43.528638,43.706601,43.409038,43.798854,43.319183],
               array[-75.790558,-75.205536,-75.923767,-75.379944,-76.043243,-75.140991,-75.922394,-75.264587,-75.655975,-75.473328],
               false,
               false) order by irow, icol
    ) as foo group by irow order by irow
) as bar;
-- 1 row, 1283 ms

select * from osrm_jgetRouteText((select json from dm_cache where irow=1 and icol=2));



/*
-- these still have to be coded

-- N x N distance matrix
select * from osrm_dmatrix(array[43.235198,43.709579,...], array[-76.420898,-76.286316,...]);

-- one to many distance matrix
select * from osrm_dmatrix(43.500846, -75.476632, array[43.235198,43.709579,...], array[-76.420898,-76.286316],...);

*/

