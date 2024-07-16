/*
 * Alertik: a tiny 'syslog' server & notification tool for Mikrotik routers.
 * This is free and unencumbered software released into the public domain.
 */

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include <curl/curl.h>

#include "notifiers.h"
#include "alertik.h"

/* Just to omit the print to stdout. */
size_t libcurl_noop_cb(void *ptr, size_t size, size_t nmemb, void *data) {
	((void)ptr);
	((void)data);
	return size * nmemb;
}

//////////////////////////////// TELEGRAM //////////////////////////////////////
/* Telegram & request settings. */
static char *telegram_bot_token;
static char *telegram_chat_id;

void setup_telegram(void)
{
	telegram_bot_token = getenv("TELEGRAM_BOT_TOKEN");
	telegram_chat_id   = getenv("TELEGRAM_CHAT_ID");
	if (!telegram_bot_token || !telegram_chat_id) {
		panic(
			"Unable to find env vars, please check if you have all of the "
			"following set:\n"
			"- TELEGRAM_BOT_TOKEN\n"
			"- TELEGRAM_CHAT_ID\n"
		);
	}
}

int send_telegram_notification(const char *msg)
{
	char full_request_url[4096] = {0};
	char *escaped_msg = NULL;
	CURLcode ret_curl;
	CURL *hnd;
	int ret;

	ret = -1;

	hnd = curl_easy_init();
	if (!hnd) {
		log_msg("> Unable to initialize libcurl!\n");
		return ret;
	}

	log_msg("> Sending notification!\n");

	escaped_msg = curl_easy_escape(hnd, msg, 0);
	if (!escaped_msg) {
		log_msg("> Unable to escape notification message...\n");
		goto error;
	}

	snprintf(
		full_request_url,
		sizeof full_request_url - 1,
		"https://api.telegram.org/bot%s/sendMessage?chat_id=%s&text=%s",
		telegram_bot_token, telegram_chat_id, escaped_msg);

	curl_easy_setopt(hnd, CURLOPT_URL, full_request_url);
	curl_easy_setopt(hnd, CURLOPT_NOPROGRESS,    1L);
	curl_easy_setopt(hnd, CURLOPT_USERAGENT, CURL_USER_AGENT);
	curl_easy_setopt(hnd, CURLOPT_MAXREDIRS,     3L);
	curl_easy_setopt(hnd, CURLOPT_TCP_KEEPALIVE, 1L);
	curl_easy_setopt(hnd, CURLOPT_WRITEFUNCTION, libcurl_noop_cb);
#ifdef CURL_VERBOSE
	curl_easy_setopt(hnd, CURLOPT_VERBOSE, 1L);
#endif
#ifndef VALIDATE_CERTS
	curl_easy_setopt(hnd, CURLOPT_SSL_VERIFYPEER, 0L);
#endif

#ifndef DISABLE_NOTIFICATIONS
	ret_curl = curl_easy_perform(hnd);
	if (ret_curl != CURLE_OK) {
		log_msg("> Unable to send request!\n");
		goto error;
	} else {
		time_last_sent_notify = time(NULL); /* Update the time of our last sent */
		log_msg("> Done!\n");               /* notification. */
	}
#endif

	ret = 0;
error:
	curl_free(escaped_msg);
	curl_easy_cleanup(hnd);
	return ret;
}
////////////////////////////////// END ////////////////////////////////////////

const char *const notifiers_str[] = {
	"Telegram"
};

struct notifier notifiers[] = {
	/* Telegram. */
	{
		.setup = setup_telegram,
		.send_notification = send_telegram_notification
	}
};

/* Global setup. */
void setup_notifiers(void)
{
	for (int i = 0; i < NUM_NOTIFIERS; i++)
		notifiers[i].setup();
}
