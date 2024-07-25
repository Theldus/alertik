/*
 * Alertik: a tiny 'syslog' server & notification tool for Mikrotik routers.
 * This is free and unencumbered software released into the public domain.
 */

#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <pthread.h>

#include "alertik.h"
#include "events.h"
#include "env_events.h"
#include "log.h"
#include "notifiers.h"
#include "syslog.h"

/*
 * Alertik
 */

static void *handle_messages(void *p)
{
	((void)p);

	int handled = 0;
	struct log_event ev = {0};

	while (syslog_pop_msg_from_fifo(&ev) >= 0) {
		print_log_event(&ev);

		if (!is_within_notify_threshold()) {
			log_msg("ignoring, reason: too many notifications!\n");
			continue;
		}

		handled  = process_static_event(&ev);
		handled += process_environment_event(&ev);

		if (handled)
			update_notify_last_sent();
		else
			log_msg("> Not handled!\n");
	}
	return NULL;
}

int main(void)
{
	pthread_t handler;
	int fd;

	log_init();

	log_msg(
		"Alertik (" GIT_HASH ") (built at " __DATE__ " " __TIME__ ")\n");
	log_msg("     (https://github.com/Theldus/alertik)\n");
	log_msg("-------------------------------------------------\n");

	if (!init_static_events() && !init_environment_events())
		panic("No event was configured, please configure at least one\n"
		      "before proceeding!\n");

	fd = syslog_create_udp_socket();
	if (pthread_create(&handler, NULL, handle_messages, NULL))
		panic_errno("Unable to create hanler thread!");

	log_msg("Waiting for messages at :%d (UDP)...\n", SYSLOG_PORT);

	while (syslog_enqueue_new_upd_msg(fd) >= 0);
	return EXIT_SUCCESS;
}
