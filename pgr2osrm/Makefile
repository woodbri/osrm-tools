
OSRMDIR= ../../Project-OSRM
CPP = g++
CPPFLAGS = -g -O0 -I$(OSRMDIR)/Util 
LDFLAGS = $(OSRMDIR)/build/libUUID.a -lpqxx -lpq
OSRM=$(OSRMDIR)/Util/UUID.h $(OSRMDIR)/build/libUUID.a

all: pgr2osrm

pgr2osrm: pgr2osrm.cpp $(OSRM)
	$(CPP) $(CPPFLAGS) -o pgr2osrm pgr2osrm.cpp $(LDFLAGS)

test: pgr2osrm test/st.osrm
	cd test && ../pgr2osrm st st_vertices_pgr junk

clean:
	rm pgr2osrm
