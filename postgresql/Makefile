OSRM_VERSION = 1.1
OBJS = osrm.o
MODULE_big = osrm-$(OSRM_VERSION)
EXTENSION = osrm
DATA = osrm--$(OSRM_VERSION).sql
DOCS = README.osrm

CURL_LIBS := $(shell curl-config --libs)
JSON_LIBS = -L /lib/i386-linux-gnu/ -ljson
SHLIB_LINK = $(CURL_LIBS) $(JSON_LIBS)

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
