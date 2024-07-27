/*
 * Alertik: a tiny 'syslog' server & notification tool for Mikrotik routers.
 * This is free and unencumbered software released into the public domain.
 */

#ifndef ALERTIK_H
#define ALERTIK_H

	#include <time.h>

	#define MIN(a,b) (((a)<(b))?(a):(b))

	extern char *get_formatted_time(time_t time, char *time_str);
	extern void log_msg(const char *fmt, ...);

#endif /* ALERTIK_H */
