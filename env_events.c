/*
 * Alertik: a tiny 'syslog' server & notification tool for Mikrotik routers.
 * This is free and unencumbered software released into the public domain.
 */

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "env_events.h"
#include "alertik.h"
#include "notifiers.h"

/* Event match types. */
#define MATCH_TYPES_LEN 2
static const char *const match_types[] = {"substr", "regex"};

/* Environment events list. */
struct env_event env_events[MAX_ENV_EVENTS] = {0};

/**
 * Safe string-to-int routine that takes into account:
 * - Overflow and Underflow
 * - No undefined behavior
 *
 * Taken from https://stackoverflow.com/a/12923949/3594716
 * and slightly adapted: no error classification, because
 * I don't need to know, error is error.
 *
 * @param out Pointer to integer.
 * @param s String to be converted.
 *
 * @return Returns 0 if success and a negative number otherwise.
 */
static int str2int(int *out, const char *s)
{
	char *end;
	if (s[0] == '\0' || isspace(s[0]))
		return -1;
	errno = 0;

	long l = strtol(s, &end, 10);

	/* Both checks are needed because INT_MAX == LONG_MAX is possible. */
	if (l > INT_MAX || (errno == ERANGE && l == LONG_MAX))
		return -1;
	if (l < INT_MIN || (errno == ERANGE && l == LONG_MIN))
		return -1;
	if (*end != '\0')
		return -1;

	*out = l;
	return 0;
}

/**/
static char *get_event_str(int ev_num, char *str)
{
	char *env;
	char ev[64] = {0};
	snprintf(ev, sizeof ev - 1, "EVENT%d_%s", ev_num, str);
	if (!(env = getenv(ev)))
		panic("Unable to find event for %s\n", ev);
	return env;
}

/**/
static int
get_event_idx(int ev_num, char *str, const char *const *str_list, int size)
{
	char *env = get_event_str(ev_num, str);
	for (int i = 0; i < size; i++) {
		if (!strcmp(env, str_list[i]))
			return i;
	}
	panic("String parameter (%s) invalid for %s\n", env, str);
}







/**/
int init_environment_events(void)
{
	char *tmp;
	int events;

	tmp = getenv("ENV_EVENTS");
	if (!tmp || (str2int(&events, tmp) < 0) || events <= 0)  {
		log_msg("Environment events not detected, disabling...\n");
		return (0);
	}

	if (events >= MAX_ENV_EVENTS)
		panic("Environment events exceeds the maximum supported (%d/%d)\n",
			events, MAX_ENV_EVENTS);

	log_msg("%d environment events found, registering...\n");
	for (int i = 0; i < events; i++) {
		/* EVENTn_MATCH_TYPE. */
		env_events[i].ev_match_type   = get_event_idx(i, "MATCH_TYPE",
			match_types, MATCH_TYPES_LEN);
		/* EVENTn_NOTIFIER. */
		env_events[i].ev_notifier_idx = get_event_idx(i, "NOTIFIER",
			notifiers_str, NUM_NOTIFIERS);
		/* EVENTn_MATCH_STR. */
		env_events[i].ev_match_str    = get_event_str(i, "MATCH_STR");
		/* EVENTn_MASK_MSG. */
		env_events[i].ev_mask_msg     = get_event_str(i, "MASK_MSG");
	}

	log_msg("Environment events summary:\n");
	for (int i = 0; i < events; i++) {
		printf(
			"EVENT%d_MATCH_TYPE: %s\n"
			"EVENT%d_MATCH_STR:  %s\n"
			"EVENT%d_NOTIFIER:   %s\n"
			"EVENT%d_MASK_MSG:   %s\n\n",
			i, match_types[env_events[i].ev_match_type],
			i, env_events[i].ev_match_str,
			i, notifiers_str[env_events[i].ev_notifier_idx],
			i, env_events[i].ev_mask_msg
		);
	}

	return (0);
}
