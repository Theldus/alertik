/*
 * Alertik: a tiny 'syslog' server & notification tool for Mikrotik routers.
 * This is free and unencumbered software released into the public domain.
 */

#ifndef ENV_EVENTS_H
#define ENV_EVENTS_H

	#include <regex.h>

	#define MAX_ENV_EVENTS  16
	struct log_event;

	struct env_event {
		int         ev_match_type;     /* whether regex or str.     */
		int         ev_notifier_idx;   /* Telegram, Discord...      */
		const char *ev_match_str;      /* regex str or substr here. */
		const char *ev_mask_msg;       /* Mask message to be sent.  */
		regex_t    regex;              /* Compiled regex.           */
	};

	extern struct env_event env_events[MAX_ENV_EVENTS];
	extern int init_environment_events(void);
	extern int process_environment_event(struct log_event *ev);

#endif /* ENV_EVENTS_H */
