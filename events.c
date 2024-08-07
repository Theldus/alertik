/*
 * Alertik: a tiny 'syslog' server & notification tool for Mikrotik routers.
 * This is free and unencumbered software released into the public domain.
 */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "events.h"
#include "notifiers.h"
#include "log.h"
#include "str.h"

/*
 * Static events
 */

#define MIN(a,b) (((a)<(b))?(a):(b))

/* Misc. */
#define MAX_MATCHES 32
static regmatch_t pmatch[MAX_MATCHES];

/* Handlers. */
static void handle_wifi_login_attempts(struct log_event *, int);
struct static_event static_events[NUM_EVENTS] = {
	/* Failed login attempts. */
	{
		.ev_match_str    = "unicast key exchange timeout",
		.hnd             = handle_wifi_login_attempts,
		.ev_match_type   = EVNT_SUBSTR,
		.enabled         = 0,
		.ev_notifier_idx = NOTIFY_IDX_TELE
	},
	/* Add new handlers here. */
};

/**
 * @brief Retrieves the event string from the environment variables.
 *
 * @param ev_num Event number.
 * @param str    String identifier.
 *
 * @return Returns the event string.
 */
static char *get_event_str(long ev_num, char *str)
{
	char *env;
	char ev[64] = {0};
	snprintf(ev, sizeof ev - 1, "STATIC_EVENT%ld_%s", ev_num, str);
	if (!(env = getenv(ev)))
		panic("Unable to find event for %s\n", ev);
	return env;
}

/**
 * @brief Retrieves the index of the event from the environment variables.
 *
 * @param ev_num    Event number.
 * @param str       String identifier.
 * @param str_list  List of strings to match against.
 * @param size      Size of the string list.
 *
 * @return Returns the index of the matching event.
 */
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
	struct static_event *sta_ev;

	for (i = 0, handled = 0; i < NUM_EVENTS; i++) {
		/* Skip not enabled events. */
		if (!static_events[i].enabled)
			continue;

		sta_ev = &static_events[i];

		if (static_events[i].ev_match_type == EVNT_SUBSTR) {
			if (strstr(ev->msg, static_events[i].ev_match_str)) {
				static_events[i].hnd(ev, i);
				handled += 1;
			}
		}

		else {
			if (regexec(&sta_ev->regex, ev->msg, MAX_MATCHES, pmatch, 0)) {
				static_events[i].hnd(ev, i);
				handled += 1;
			}
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
	struct notifier *self;
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
		static_events[ev].ev_notifier_idx =
			get_event_idx(ev, "NOTIFIER", notifiers_str, NUM_NOTIFIERS);
		static_events[ev].enabled = 1;

		if (*end != ',' && *end != '\0')
			panic("Wrong event number in STATIC_EVENTS_ENABLED, aborting...\n");

	} while (*end++ != '\0');


	log_msg("Static events summary:\n");
	for (int i = 0; i < NUM_EVENTS; i++) {
		if (!static_events[i].enabled)
			continue;

		log_msg("STATIC_EVENT%d         : enabled\n", i);
		log_msg("STATIC_EVENT%d_NOTIFIER: %s\n\n",
			i, notifiers_str[static_events[i].ev_notifier_idx]);

		/* Try to setup notifier if not yet. */
		self = &notifiers[static_events[i].ev_notifier_idx];
		self->setup(self);

		/* If regex, compile it first. */
		if (static_events[i].ev_match_type == EVNT_REGEX) {
			if (regcomp(
			    &static_events[i].regex,
			    static_events[i].ev_match_str,
			    REG_EXTENDED))
			{
				panic("Unable to compile regex (%s) for EVENT%d!!!",
					static_events[i].ev_match_str, i);
			}
		}
	}
	return 1;
}


///////////////////////////////////////////////////////////////////////////////
///////////////////////////// FAILED LOGIN ATTEMPTS ///////////////////////////
///////////////////////////////////////////////////////////////////////////////

/**
 * @brief Parses the message pointed by @p msg and saves the
 * read mac-address and interface in @p mac_addr and @wifi_iface.
 *
 * @param msg         Buffer to be read and parsed.
 * @param wifi_iface  Output buffer that will contain the parsed
 *                    device interface.
 * @param mac_addr    Output buffer that will contain the parsed
 *                    mac address.
 *
 * @return Returns 0 if success, -1 otherwise.
 */
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

/**
 * @brief For a given log event @p ev and offset index @p idx_env,
 * handle the event and send a notification message to the
 * configured notifier.
 *
 * @param ev       Log event structure.
 * @param idx_env  Event index.
 */
static void handle_wifi_login_attempts(struct log_event *ev, int idx_env)
{
	char time_str[32]   = {0};
	char mac_addr[32]   = {0};
	char wifi_iface[32] = {0};
	struct str_ab notif_message;
	struct notifier *self;
	int notif_idx;
	int ret;

	log_msg("> Login attempt detected!\n");

	if (parse_login_attempt_msg(ev->msg, wifi_iface, mac_addr) < 0)
		return;

	ab_init(&notif_message);

	/* Send our notification. */
	ret = ab_append_fmt(&notif_message,
		"There is someone trying to connect "
		"to your WiFi: %s, with the mac-address: %s, at:%s",
		wifi_iface,
		mac_addr,
		get_formatted_time(ev->timestamp, time_str)
	);

	if (ret)
		return;

	log_msg("> Retrieved info, MAC: (%s), Interface: (%s)\n", mac_addr, wifi_iface);

	notif_idx = static_events[idx_env].ev_notifier_idx;
	self      = &notifiers[notif_idx];

	if (self->send_notification(self, notif_message.buff) < 0) {
		log_msg("unable to send the notification!\n");
		return;
	}
}

////////////////////////////// YOUR HANDLER HERE //////////////////////////////
