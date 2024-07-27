/*
 * Alertik: a tiny 'syslog' server & notification tool for Mikrotik routers.
 * This is free and unencumbered software released into the public domain.
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <curl/curl.h>

#include "log.h"
#include "notifiers.h"
#include "alertik.h"

/*
 * Notification handling/notifiers
 */

/* EPOCH in secs of last sent notification. */
static time_t time_last_sent_notify;

/* Just to omit the print to stdout. */
size_t libcurl_noop_cb(void *ptr, size_t size, size_t nmemb, void *data) {
	((void)ptr);
	((void)data);
	return size * nmemb;
}

/**
 * @brief Just updates the time (Epoch) of the last sent
 * notify.
 */
void update_notify_last_sent(void) {
	time_last_sent_notify = time(NULL);
}

/**
 * @brief Checks if the current time is within or not
 * the minimal threshold to send a nofication.
 *
 * @return Returns 1 if within the range (can send nofications),
 * 0 otherwise.
 */
int is_within_notify_threshold(void) {
	return (time(NULL) - time_last_sent_notify) > LAST_SENT_THRESHOLD_SECS;
}

/**
 * @brief Initializes and configures the CURL handle for sending a request.
 *
 * @param hnd CURL handle.
 * @param url Request URL.
 * @return Returns 0.
 */
static int setopts_get_curl(CURL *hnd, const char *url)
{
	curl_easy_setopt(hnd, CURLOPT_URL, url);
	curl_easy_setopt(hnd, CURLOPT_NOPROGRESS,    1L);
	curl_easy_setopt(hnd, CURLOPT_USERAGENT, CURL_USER_AGENT);
	curl_easy_setopt(hnd, CURLOPT_MAXREDIRS,     3L);
    curl_easy_setopt(hnd, CURLOPT_WRITEFUNCTION, libcurl_noop_cb);
#ifdef CURL_VERBOSE
	curl_easy_setopt(hnd, CURLOPT_VERBOSE, 1L);
#endif
#ifndef VALIDATE_CERTS
	curl_easy_setopt(hnd, CURLOPT_SSL_VERIFYPEER, 0L);
#endif
	return 0;
}

/**
 *
 */
static void do_curl_cleanup(CURL *hnd, char *escape, struct curl_slist *slist)
{
	curl_free(escape);
	curl_slist_free_all(slist);
	if (hnd)
		curl_easy_cleanup(hnd);
}

/**
 *
 */
static CURLcode do_curl(CURL *hnd, char *escape, struct curl_slist *slist)
{
	CURLcode ret_curl = !CURLE_OK;

#ifndef DISABLE_NOTIFICATIONS
	ret_curl = curl_easy_perform(hnd);
	if (ret_curl != CURLE_OK) {
		log_msg("> Unable to send request!\n");
		goto error;
	} else {
		log_msg("> Done!\n");
	}
#endif

	ret_curl = CURLE_OK;
error:
	do_curl_cleanup(hnd, escape, slist);
	return ret_curl;
}

/**
 * @brief Initializes and configures the CURL handle for sending a POST
 * request with a JSON payload.
 *
 * @param hnd           CURL handle.
 * @param url           Request URL.
 * @param json_payload  Payload data in JSON format.
 *
 * @return Returns 0 if successful, 1 otherwise.
 */
static int setopts_post_json_curl(CURL *hnd, const char *url,
	const char *json_payload, struct curl_slist **slist)
{
	struct curl_slist *s  = *slist;

	s = NULL;
	s = curl_slist_append(s, "Content-Type: application/json");
	s = curl_slist_append(s, "Accept: application/json");
	if (!s) {
		*slist = s;
		return 1;
	}

	curl_easy_setopt(hnd, CURLOPT_HTTPHEADER,    s);
	curl_easy_setopt(hnd, CURLOPT_URL,         url);
	curl_easy_setopt(hnd, CURLOPT_NOPROGRESS,    1L);
	curl_easy_setopt(hnd, CURLOPT_USERAGENT, CURL_USER_AGENT);
	curl_easy_setopt(hnd, CURLOPT_MAXREDIRS,     3L);
    curl_easy_setopt(hnd, CURLOPT_WRITEFUNCTION, libcurl_noop_cb);
#ifdef CURL_VERBOSE
	curl_easy_setopt(hnd, CURLOPT_VERBOSE, 1L);
#endif
#ifndef VALIDATE_CERTS
	curl_easy_setopt(hnd, CURLOPT_SSL_VERIFYPEER, 0L);
#endif
	curl_easy_setopt(hnd, CURLOPT_POSTFIELDS, json_payload);
	*slist = s;
	return 0;
}

/**
 *
 */
static int send_generic_webhook(const char *url, const char *text)
{
	CURL *hnd               = NULL;
	struct curl_slist *s    = NULL;
	char payload_data[4096] = {0};

	if (!(hnd = curl_easy_init())) {
		log_msg("Failed to initialize libcurl!\n");
		return 1;
	}

	snprintf(
		payload_data,
		sizeof payload_data - 1,
		"{\"text\":\"%s\"}",
		text
	);


	#if 0
	TODO:
	- escape "" in the payload, otherwise it will silently
	escape from the {"text": }
	- think about the return code from the request and
	emmit some message if not 200
	#endif

	if (setopts_post_json_curl(hnd, url, payload_data, &s))
		return 1;

	log_msg("> Sending notification!\n");
	return do_curl(hnd, NULL, s);
}


////////////////////////////////////////////////////////////////////////////////
//////////////////////////////// TELEGRAM //////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

/* Telegram & request settings. */
static char *telegram_bot_token;
static char *telegram_chat_id;

void setup_telegram(void)
{
	static int setup = 0;
	if (setup)
		return;

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
	setup = 1;
}

static int send_telegram_notification(const char *msg)
{
	char full_request_url[4096] = {0};
	char *escaped_msg = NULL;
	CURL *hnd         = NULL;

	if (!(hnd = curl_easy_init())) {
		log_msg("Failed to initialize libcurl!\n");
		return -1;
	}

	escaped_msg = curl_easy_escape(hnd, msg, 0);
	if (!escaped_msg) {
		log_msg("> Unable to escape notification message...\n");
		do_curl_cleanup(hnd, escaped_msg, NULL);
	}

	snprintf(
		full_request_url,
		sizeof full_request_url - 1,
		"https://api.telegram.org/bot%s/sendMessage?chat_id=%s&text=%s",
		telegram_bot_token, telegram_chat_id, escaped_msg);

	setopts_get_curl(hnd, full_request_url);
	log_msg("> Sending notification!\n");
	return do_curl(hnd, escaped_msg, NULL);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////// SLACK ///////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

/* Slack settings. */
static char *slack_webhook_url;

void setup_slack(void)
{
	static int setup = 0;
	if (setup)
		return;

	slack_webhook_url = getenv("SLACK_WEBHOOK_URL");
	if (!slack_webhook_url) {
		panic("Unable to find env vars for, please check if you have set\n"
		      "the SLACK_WEBHOOK_URL!!\n");
	}
	setup = 1;
}

static int send_slack_notification(const char *msg) {
	return send_generic_webhook(slack_webhook_url, msg);
}


////////////////////////////////// END ////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

const char *const notifiers_str[] = {
	"Telegram", "Slack",    "Discord",  "Teams",
	"Generic1", "Generic2", "Generic3", "Generic4"
};

struct notifier notifiers[] = {
	/* Telegram. */
	{
		.setup = setup_telegram,
		.send_notification = send_telegram_notification
	},
	/* Slack. */
	{
		.setup = setup_slack,
		.send_notification = send_slack_notification
	}
};
