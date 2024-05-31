/*
 * Alertik: a tiny 'syslog' server & notification tool for Mikrotik routers.
 * This is free and unencumbered software released into the public domain.
 */

#ifndef EVENTS_H
#define EVENTS_H

	#include <time.h>

	#define MSG_MAX  2048
	#define NUM_EVENTS  1

	#define EVNT_SUBSTR 1
	#define EVNT_REGEX  2

	/* Log event. */
	struct log_event {
		char   msg[MSG_MAX];
		time_t timestamp;
	};

	struct ev_handler {
		const char *str;
		void(*hnd)(struct log_event *);
		int evnt_type;
	};

	extern struct ev_handler handlers[NUM_EVENTS];

#endif /* EVENTS_H */
