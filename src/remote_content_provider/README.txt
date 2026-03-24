REMOTE-CONTENT-PROVIDER

remote-content-provider provides a way to serve files on http server on dulink space.
usage steps:
1> build the remote-content-provider with make.
2> copy it into your target GOS
3> run remote content provider

For linux build, aarch64-linux curl with openssl library and its dependencies should be added to the
cross compile environment.

ATTENTION: http server need to set Accept-Ranges: bytes.
	   for python you should use rangehttpserver.
	   A simple command to host files on current directory with port 8080 is:
	   > python -m RangeHTTPServer

More information about rangehttpserver can be found in https://github.com/danvk/RangeHTTPServer
