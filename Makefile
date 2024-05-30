#
# Alertik: a tiny 'syslog' server & notification tool for Mikrotik routers.
# This is free and unencumbered software released into the public domain.
#

CC      ?= cc
CFLAGS  += -Wall -Wextra
LDLIBS  += -pthread -lcurl
STRIP    = strip

ifeq ($(LOG_FILE),yes)
	CFLAGS += -DUSE_FILE_AS_LOG
endif

# We're cross-compiling?
ifneq ($(CROSS),)
	CC       = $(CROSS)-linux-musleabi-gcc
	STRIP    = $(CROSS)-linux-musleabi-strip
	ifeq ($(LOG_FILE),)
		CFLAGS  += -DUSE_FILE_AS_LOG # We don't have stdout...
	endif
	LDLIBS  += -lbearssl
	LDFLAGS += -no-pie --static
endif

all: alertik Makefile
	$(STRIP) --strip-all alertik

alertik: alertik.o

clean:
	rm -f alertik.o alertik
