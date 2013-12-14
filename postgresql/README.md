Extension to access OSRM servers via plpgsql.
=============================================

Author: Stephen Woodbridge <woodbri (at) swoodbridge (dot) com>
Date: 2013-12-14
License: BSD 2-clause

---------------------------------------------------------------------------

This is a PostgreSQL extension that provides access to a Project-OSRM server via plpgsql.

This assumes you have setup you own OSRM server. **PLEASE DO NOT USE THIS ON A PUBLIC SERVER!** as it will most likely get you banned from the server and it can generate a heavy load on the server that other people are trying to use.

This extension works by turning your queries into HTTP requests to the OSRM server and returning the json responses so they can be cached in a table. It also provides functions to extract specific items from a json document so that they can be used.

See the ``test.sql`` files for examples of how to use the various functions.

----------------------------------------------------------------------------

Support
=======

I hope you find this project useful. Feel free to open tickets if you have issue, and to submit pull requests if you have fixes or enhancements to add.

I am a GIS consultant and if you need help with enhancements, setting up servers, or help in areas like geocoding, reverse geocoding, mapping, routing or driving directions then please contact me at the email address above. I would be happy to discuss your requirements and provide you a quote for development and/or support.

