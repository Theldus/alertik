/*
 * Alertik: a tiny 'syslog' server & notification tool for Mikrotik routers.
 * This is free and unencumbered software released into the public domain.
 */

#ifndef ENV_EVENTS_H
#define ENV_EVENTS_H

	#include "events.h"

	#define MAX_ENV_EVENTS  16

	struct env_event {
		int         ev_match_type;     /* whether regex or str.     */
		int         ev_notifier_idx;   /* Telegram, Discord...      */
		const char *ev_match_str;      /* regex str or substr here. */
		const char *ev_mask_msg;       /* Mask message to be sent.  */
	};

	extern struct env_event env_events[MAX_ENV_EVENTS];
	extern int init_environment_events(void);

#endif /* ENV_EVENTS_H */