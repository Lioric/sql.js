#!/usr/bin/env python
import BaseHTTPServer, GZipSimpleHTTPServer, os
 
# We need to host from the root because we are going to be requesting files inside of dist
os.chdir('../')
port=8081
print "Running on port %d" % port
 
GZipSimpleHTTPServer.SimpleHTTPRequestHandler.extensions_map['.wasm'] = 'application/wasm'

httpd = BaseHTTPServer.HTTPServer(('localhost', port), GZipSimpleHTTPServer.SimpleHTTPRequestHandler)
 
print "Mapping \".wasm\" to \"%s\"" % GZipSimpleHTTPServer.SimpleHTTPRequestHandler.extensions_map['.wasm']
httpd.serve_forever()
