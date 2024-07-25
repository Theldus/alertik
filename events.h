/*
 * Alertik: a tiny 'syslog' server & notification tool for Mikrotik routers.
 * This is free and unencumbered software released into the public domain.
 */

#ifndef EVENTS_H
#define EVENTS_H

	#include <regex.h>
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

	struct static_event {
		void(*hnd)(struct log_event *, int); /* Event handler.            */
		const char *ev_match_str;   /* Substr or regex to match.          */
		int        ev_match_type;   /* Whether substr or regex.           */
		int        ev_notifier_idx; /* Telegram, Discord...               */
		int        enabled;         /* Whether if handler enabled or not. */
		regex_t    regex;           /* Compiled regex.                    */
	};

	extern int process_static_event(struct log_event *ev);
	extern int init_static_events(void);

#endif /* EVENTS_H */
