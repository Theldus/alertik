/*
 * Alertik: a tiny 'syslog' server & notification tool for Mikrotik routers.
 * This is free and unencumbered software released into the public domain.
 */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "events.h"
#include "alertik.h"
#include "notifiers.h"
#include "log.h"

static void handle_wifi_login_attempts(struct log_event *, int);

/* Handlers. */
struct ev_handler handlers[NUM_EVENTS] = {
	/* Failed login attempts. */
	{
		.str = "unicast key exchange timeout",
		.hnd = handle_wifi_login_attempts,
		.evnt_type = EVNT_SUBSTR,
		.enabled   = 0,
		.evnt_notifier_idx = NOTIFY_IDX_TELE
	},
	/* Add new handlers here. */
};

/**/
static char *get_event_str(long ev_num, char *str)
{
	char *env;
	char ev[64] = {0};
	snprintf(ev, sizeof ev - 1, "STATIC_EVENT%ld_%s", ev_num, str);
	if (!(env = getenv(ev)))
		panic("Unable to find event for %s\n", ev);
	return env;
}

/**/
static int
get_event_idx(long ev_num, char *str, const char *const *str_list, int size)
{
	char *env = get_event_str(ev_num, str);
	for (int i = 0; i < size; i++) {
		if (!strcmp(env, str_list[i]))
			return i;
	}
	panic("String parameter (%s) invalid for %s\n", env, str);
}

/**
 * @brief Given an event, checks if it belongs to one of the
 * registered events and then, handle it.
 *
 * @param ev Event to be processed.
 *
 * @return Returns the amount of matches, 0 if none (not handled).
 */
int process_static_event(struct log_event *ev)
{
	int i;
	int handled;

	for (i = 0, handled = 0; i < NUM_EVENTS; i++) {
		/* Skip not enabled events. */
		if (!handlers[i].enabled)
			continue;

		if (strstr(ev->msg, handlers[i].str)) {
			handlers[i].hnd(ev, i);
			handled += 1;
		}
	}
	return handled;
}

/**
 * @brief Initialize static events.
 *
 * @return Returns 0 if there is no static event, and 1 if
 * there is at least one _and_ is successfully configured.
*/
int init_static_events(void)
{
	char *ptr, *end;
	long ev;

	/* Check for: STATIC_EVENTS_ENABLED=0,3,5,2... */
	ptr = getenv("STATIC_EVENTS_ENABLED");
	if (!ptr || ptr[0] == '\0') {
		log_msg("Static events not detected, disabling...\n");
		return (0);
	}

	end   = ptr;
	errno = 0;

	do
	{
		ev = strtol(end, &end, 10);
		if (errno != 0 || ((ptr == end) && ev == 0))
			panic("Unable to parse STATIC_EVENTS_ENABLED, aborting...\n");

		/* Skip whitespaces. */
		while (*end != '\0' && isspace(*end))
			end++;

		/* Check if ev number is sane. */
		if (ev < 0 || ev >= NUM_EVENTS)
			panic("Event (%ld) is not valid!, should be between 0-%d\n",
				ev, NUM_EVENTS - 1);

		/* Try to retrieve & initialize notifier for the event. */
		handlers[ev].evnt_notifier_idx =
			get_event_idx(ev, "NOTIFIER", notifiers_str, NUM_NOTIFIERS);
		handlers[ev].enabled = 1;

		if (*end != ',' && *end != '\0')
			panic("Wrong event number in STATIC_EVENTS_ENABLED, aborting...\n");

	} while (*end++ != '\0');


	log_msg("Static events summary:\n");
	for (int i = 0; i < NUM_EVENTS; i++) {
		if (!handlers[i].enabled)
			continue;

		printf(
			"STATIC_EVENT%d         : enabled\n"
			"STATIC_EVENT%d_NOTIFIER: %s\n\n",
			i, i, notifiers_str[handlers[i].evnt_notifier_idx]
		);

		/* Try to setup notifier if not yet. */
		notifiers[handlers[i].evnt_notifier_idx].setup();
	}
	return 1;
}


///////////////////////////// FAILED LOGIN ATTEMPTS ///////////////////////////
static int
parse_login_attempt_msg(const char *msg, char *wifi_iface, char *mac_addr)
{
	size_t len = strlen(msg);
	size_t tmp = 0;
	size_t at  = 0;

	/* Find '@' and the last ' '. */
	for (at = 0; at < len && msg[at] != '@'; at++) {
		if (msg[at] == ' ')
			tmp = at;
	}

	if (at == len || !tmp) {
		log_msg("unable to parse additional data, ignoring...\n");
		return -1;
	}

	memcpy(mac_addr, msg + tmp + 1, MIN(at - tmp - 1, 32));

	/*
	 * Find network name.
	 * Assuming that the interface name does not have ':'...
	 */
	for (tmp = at + 1; tmp < len && msg[tmp] != ':'; tmp++);
	if (tmp == len) {
		log_msg("unable to find interface name!, ignoring..\n");
		return -1;
	}

	memcpy(wifi_iface, msg + at + 1, MIN(tmp - at - 1, 32));
	return (0);
}

static void handle_wifi_login_attempts(struct log_event *ev, int idx_env)
{
	char time_str[32]   = {0};
	char mac_addr[32]   = {0};
	char wifi_iface[32] = {0};
	char notification_message[2048] = {0};
	int notif_idx;

	log_msg("> Login attempt detected!\n");

	if (parse_login_attempt_msg(ev->msg, wifi_iface, mac_addr) < 0)
		return;

	/* Send our notification. */
	snprintf(
		notification_message,
		sizeof notification_message - 1,
		"There is someone trying to connect "
		"to your WiFi: %s, with the mac-address: %s, at:%s",
		wifi_iface,
		mac_addr,
		get_formatted_time(ev->timestamp, time_str)
	);

	log_msg("> Retrieved info, MAC: (%s), Interface: (%s)\n", mac_addr, wifi_iface);

	notif_idx = handlers[idx_env].evnt_notifier_idx;
	if (notifiers[notif_idx].send_notification(notification_message) < 0) {
		log_msg("unable to send the notification!\n");
		return;
	}
}

////////////////////////////// YOUR HANDLER HERE //////////////////////////////
