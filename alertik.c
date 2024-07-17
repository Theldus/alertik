/*
 * Alertik: a tiny 'syslog' server & notification tool for Mikrotik routers.
 * This is free and unencumbered software released into the public domain.
 */

#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netdb.h>

#include "alertik.h"
#include "events.h"
#include "env_events.h"
#include "log.h"
#include "notifiers.h"
#include "syslog.h"

/* Misc. */
#define LAST_SENT_THRESHOLD_SECS 10  /* Minimum time (in secs) between two */
time_t time_last_sent_notify; /* notifications. */


static void *handle_messages(void *p)
{
	((void)p);
	size_t i;
	struct log_event ev = {0};

	while (syslog_pop_msg_from_fifo(&ev) >= 0) {
		print_log_event(&ev);

		if ((time(NULL) - time_last_sent_notify) <= LAST_SENT_THRESHOLD_SECS) {
			log_msg("ignoring, reason: too many notifications!\n");
			continue;
		}

		/* Check if it belongs to any of our desired events. */
		for (i = 0; i < NUM_EVENTS; i++) {
			if (strstr(ev.msg, handlers[i].str)) {
				handlers[i].hnd(&ev);
				break;
			}
		}

		if (i == NUM_EVENTS)
			log_msg("> No match!\n");
	}
	return NULL;
}

int main(void)
{
	pthread_t handler;
	int fd;

	log_init();
	setup_notifiers();
	init_environment_events();

	log_msg(
		"Alertik (" GIT_HASH ") (built at " __DATE__ " " __TIME__ ")\n");
	log_msg("     (https://github.com/Theldus/alertik)\n");
	log_msg("-------------------------------------------------\n");

	fd = syslog_create_udp_socket();
	if (pthread_create(&handler, NULL, handle_messages, NULL))
		panic_errno("Unable to create hanler thread!");

	log_msg("Waiting for messages at :%d (UDP)...\n", SYSLOG_PORT);

	while (syslog_enqueue_new_upd_msg(fd) >= 0);
	return EXIT_SUCCESS;
}
