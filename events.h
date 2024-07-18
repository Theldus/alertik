/*
 * Alertik: a tiny 'syslog' server & notification tool for Mikrotik routers.
 * This is free and unencumbered software released into the public domain.
 */

#ifndef EVENTS_H
#define EVENTS_H

	#include <time.h>

	#define MSG_MAX  2048
	#define NUM_EVENTS  1

	#define EVNT_SUBSTR 0
	#define EVNT_REGEX  1

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
	extern int process_static_event(struct log_event *ev);

#endif /* EVENTS_H */
