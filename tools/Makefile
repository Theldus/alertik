#
# Alertik: a tiny 'syslog' server & notification tool for Mikrotik routers.
# This is free and unencumbered software released into the public domain.
#

CC     ?= cc
CC_JS   = emcc
CFLAGS += -Wall -Wextra -O2
CFLAGS_JS += $(CFLAGS)
CFLAGS_JS += -s EXPORTED_FUNCTIONS='["_do_regex", "_malloc", "_free"]'
CFLAGS_JS += -s 'EXPORTED_RUNTIME_METHODS=["stringToUTF8", "UTF8ToString", "setValue"]'

all: regext.js regext Makefile

regext.js: regext.c
	$(CC_JS) $(CFLAGS_JS) regext.c -o regext.js

regext: regext.c
	$(CC) $(CFLAGS) -DUSE_C regext.c -o regext

clean:
	rm -f regext.js regext.wasm regext *.o
