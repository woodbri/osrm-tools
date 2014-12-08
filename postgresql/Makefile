OSRM_VERSION = 2.0
OBJS = osrm.o
MODULE_big = osrm-$(OSRM_VERSION)
EXTENSION = osrm
DATA = osrm--$(OSRM_VERSION).sql
DOCS = README.osrm

CURL_LIBS := $(shell curl-config --libs)
JSON_LIBS = -L /lib/i386-linux-gnu/ -ljson
SHLIB_LINK = $(CURL_LIBS) $(JSON_LIBS)


all:
ifndef PGVER
	@echo "make PGVER=9.2|9.3 clean|install"

else

PG_CONFIG = /usr/lib/postgresql/$(PGVER)/bin/pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

endif

