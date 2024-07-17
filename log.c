/*
 * Alertik: a tiny 'syslog' server & notification tool for Mikrotik routers.
 * This is free and unencumbered software released into the public domain.
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "events.h"
#include "log.h"

static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
static int curr_file;

/* There should *always* be a corresponding close_log_file() call. */
static inline void open_log_file(void)
{
	struct stat sb;
	pthread_mutex_lock(&log_mutex);
		if (curr_file == STDOUT_FILENO)
			return;

		if (stat("log", &sb) < 0) {
			if (mkdir("log", 0755) < 0)
				return;
		}
		curr_file = openat(AT_FDCWD, LOG_FILE,
			O_WRONLY|O_CREAT|O_APPEND, 0666);

		if (curr_file < 0)
			curr_file = STDOUT_FILENO; /* fallback to stdout if can't open. */
}

/* This should *always* be called *after* a call to open_log_file(). */
static void close_log_file(void)
{
		if (curr_file || curr_file == STDOUT_FILENO)
			goto out;
		fsync(curr_file);
		close(curr_file);
out:
	pthread_mutex_unlock(&log_mutex);
}

/**/
char *get_formatted_time(time_t time, char *time_str)
{
	strftime(
		time_str,
		32,
		"%Y-%m-%d %H:%M:%S",
		localtime(&time)
	);
	return time_str;
}

/**/
void log_msg(const char *fmt, ...)
{
	char time_str[32] = {0};
	va_list ap;

	open_log_file();
		dprintf(curr_file, "[%s] ", get_formatted_time(time(NULL), time_str));
		va_start(ap, fmt);
		vdprintf(curr_file, fmt, ap);
		va_end(ap);
	close_log_file();
}

/**/
void print_log_event(struct log_event *ev)
{
	char time_str[32] = {0};
	open_log_file();
		dprintf(curr_file, "\n[%s] %s\n",
			get_formatted_time(ev->timestamp, time_str), ev->msg);
	close_log_file();
}

/**/
void log_init(void) {
	atexit(close_log_file);
#ifndef USE_FILE_AS_LOG
	curr_file = STDOUT_FILENO;
#endif
}
