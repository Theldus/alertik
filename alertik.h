/*
 * Alertik: a tiny 'syslog' server & notification tool for Mikrotik routers.
 * This is free and unencumbered software released into the public domain.
 */

#ifndef ALERTIK_H
#define ALERTIK_H

	#include <stdarg.h>
	#include <stdlib.h>
	#include <time.h>

	#define MIN(a,b) (((a)<(b))?(a):(b))

	extern time_t time_last_sent_notify;
	extern char *get_formatted_time(time_t time, char *time_str);
	extern void log_msg(const char *fmt, ...);
	extern int  send_telegram_notification(const char *msg);

#endif /* ALERTIK_H */
