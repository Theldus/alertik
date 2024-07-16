/*
 * Alertik: a tiny 'syslog' server & notification tool for Mikrotik routers.
 * This is free and unencumbered software released into the public domain.
 */

#ifndef LOG_H
#define LOG_H

	/* Uncomment/comment to enable/disable the following settings. */
	// #define USE_FILE_AS_LOG           /* stdout if commented. */

	#define LOG_FILE "log/log.txt"

	extern char *get_formatted_time(time_t time, char *time_str);
	extern void print_log_event(struct log_event *ev);
	extern void log_msg(const char *fmt, ...);
	extern void log_init(void);

#endif /* LOG_H */
