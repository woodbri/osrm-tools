OBJS = osrm.o
MODULE_big = osrm
EXTENSION = osrm
DATA = osrm--1.0.sql
DOCS = README.osrm

CURL_LIBS := $(shell curl-config --libs)
JSON_LIBS = -L /lib/i386-linux-gnu/ -ljson
SHLIB_LINK = $(CURL_LIBS) $(JSON_LIBS)

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
