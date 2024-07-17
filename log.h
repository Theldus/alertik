/*
 * Alertik: a tiny 'syslog' server & notification tool for Mikrotik routers.
 * This is free and unencumbered software released into the public domain.
 */

#ifndef LOG_H
#define LOG_H

	#include <errno.h>
	#include <stdlib.h>
	#include <sys/types.h>
	struct log_event;

	/* Uncomment/comment to enable/disable the following settings. */
	// #define USE_FILE_AS_LOG           /* stdout if commented. */

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

	#define LOG_FILE "log/log.txt"

	extern char *get_formatted_time(time_t time, char *time_str);
	extern void print_log_event(struct log_event *ev);
	extern void log_msg(const char *fmt, ...);
	extern void log_init(void);

#endif /* LOG_H */
