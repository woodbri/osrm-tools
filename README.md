osrm-tools
==========

---------------------------------------------------------------------------
Author: Stephen Woodbridge < woodbri (at) swoodbridge (dot) com >
Date: 12-14-2013
License: BSD 2-clause
---------------------------------------------------------------------------

Tools to extract postgresql database into OSRM normalized files, and postgresql function to access OSRM Server. Read the respective README.md files in each of the following folders for more information on how to use each.

pgr2osrm
--------

This directory contains a tools that will create an OSRM normalized file from a pgRouting database. The ORSM normalized file can be prepared with ``osrm-prepare`` and then run using ``osrm-routed``.

postgresql
----------

This directory contains an postgresql extention that will allow you to interface with the osrm-routed to make route requests and parse the results. This includes the ability to create a distance matrix that can them be used by pgRouting 2.0 TSP function of the gsoc-vrp branch to solve single depot vehicle routing problems.

test
----

This directory has tools to build a simple rectangluar grid graph in pgRouting that can then be exported to the OSRM normalized file format, and ultimately run in the ``osrm-routed`` for testing the these tools.

web
----

This directory has a simple Leaflet and jQuery based HTML page that will display the test database ``osrm_test`` using mapserver to render it. There is a ``osrm.map`` file for use with mapserver and ``osrm-optimize.php`` PHP script to hanlde the ajax requests.

## BUILD AND TEST

There are various dependencies (like Ubuntu packages):

 * postgresql-server-dev-9.2
 * libpq-dev
 * libpqxx3-dev
 * libcurl4-gnutls-dev
 * libjson0-dev
 * Project-OSRM (built from github)

and probably others that I have forgotten to mention.

The build and test process is something like the following. You may have to change files for postgresql credentials.

```
cd pgr2osrm
make
cd ../test
./mk-testdb2  # drop and create "osrm_test" database
./extract-ddnoded2
osrm-prepare ddnoded2.osrm
cd ..
./run-test-server
cd postgresql
make
sudo make install
psql -U postgres -h localhost -a osrm_test -f test.sql
```

----------------------------------------------------------------------------

Support
=======

I hope you find this project useful. Feel free to open tickets if you have issue, and to submit pull requests if you have fixes or enhancements to add.

I am a GIS consultant and if you need help with enhancements, setting up servers, or help in areas like geocoding, reverse geocoding, mapping, routing or driving directions then please contact me at the email address above. I would be happy to discuss your requirements and provide you a quote for development and/or support.

