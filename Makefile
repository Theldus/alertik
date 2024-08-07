#
# Alertik: a tiny 'syslog' server & notification tool for Mikrotik routers.
# This is free and unencumbered software released into the public domain.
#

CC      ?= cc
CFLAGS  += -Wall -Wextra -O2
LDLIBS  += -pthread -lcurl
STRIP    = strip
VERSION  = v0.1
OBJS     = alertik.o events.o env_events.o notifiers.o log.o syslog.o str.o

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

GIT_HASH=$(shell git rev-parse --short HEAD 2>/dev/null || echo '$(VERSION)')
CFLAGS += -DGIT_HASH=\"$(GIT_HASH)\"

.PHONY: all clean

all: alertik Makefile
	$(STRIP) --strip-all alertik

alertik: $(OBJS)

clean:
	rm -f $(OBJS) alertik
