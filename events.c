/*
 * Alertik: a tiny 'syslog' server & notification tool for Mikrotik routers.
 * This is free and unencumbered software released into the public domain.
 */

#include <stdio.h>
#include <string.h>
#include "events.h"
#include "alertik.h"

void handle_wifi_login_attempts(struct log_event *ev);

/* Handlers. */
struct ev_handler handlers[NUM_EVENTS] = {
	/* Failed login attempts. */
	{
		.str = "unicast key exchange timeout",
		.hnd = handle_wifi_login_attempts,
		.evnt_type = EVNT_SUBSTR
	},
	/* Add new handlers here. */
};

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

void handle_wifi_login_attempts(struct log_event *ev)
{
	char time_str[32]   = {0};
	char mac_addr[32]   = {0};
	char wifi_iface[32] = {0};
	char notification_message[2048] = {0};

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

	if (send_telegram_notification(notification_message) < 0) {
		log_msg("unable to send the notification!\n");
		return;
	}
}

////////////////////////////// YOUR HANDLER HERE //////////////////////////////
