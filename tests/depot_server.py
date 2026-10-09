#!/usr/bin/env python3
"""Serves a fixture depot like "python3 -m http.server", with two behaviours of real
depots the client must cope with:
 - the lists (*.txt) are refused (403) to a User-Agent that is not a browser's,
   as cdn.colimata.com does;
 - under /noquery/, any URL with a query string answers 404, as ramonster.nl does.
usage: depot_server.py <port> <directory>"""
import sys, functools, http.server

class Handler(http.server.SimpleHTTPRequestHandler):
	def do_GET(self):
		path, _, query = self.path.partition("?")
		if path.endswith(".txt") and not self.headers.get("User-Agent", "").startswith("Mozilla/5.0"):
			self.send_error(403, "browsers only")
			return
		if path.startswith("/noquery/") and query:
			self.send_error(404, "no query strings here")
			return
		super().do_GET()

	def log_message(self, *args):
		pass

port, directory = int(sys.argv[1]), sys.argv[2]
http.server.ThreadingHTTPServer(("127.0.0.1", port), functools.partial(Handler, directory=directory)).serve_forever()
