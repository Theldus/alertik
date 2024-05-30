# conf file for armv6 builds on BearSSL
include conf/Unix.mk

# We override the build directory.
BUILD = armv6

# C compiler, linker, and static library builder.
CC = armv6-linux-musleabi-gcc
CFLAGS = -W -Wall -Os
LD = armv6-linux-musleabi-gcc
AR = armv6-linux-musleabi-ar

# We compile only the static library.
DLL = no
TOOLS = no
TESTS = no
