/*
 * Alertik: a tiny 'syslog' server & notification tool for Mikrotik routers.
 * This is free and unencumbered software released into the public domain.
 */

#ifndef ALERTIK_H
#define ALERTIK_H

	#include <stdarg.h>
	#include <stdlib.h>

	#define panic_errno(s) \
		do {\
			log_msg("%s: %s", (s), strerror(errno)); \
			exit(EXIT_FAILURE); \
		} while(0);

	#define panic(...) \
		do {\
			log_msg(__VA_ARGS__); \
			exit(EXIT_FAILURE); \
		} while(0);

	#define MIN(a,b) (((a)<(b))?(a):(b))

	extern char *TELEGRAM_NICKNAME;
	extern char *get_formatted_time(time_t time, char *time_str);
	extern void log_msg(const char *fmt, ...);
	extern int  send_telegram_notification(const char *msg);

#endif /* ALERTIK_H */
