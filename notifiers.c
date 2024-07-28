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
#include "str.h"

/*
 * Notification handling/notifiers
 */

struct webhook_data {
	char *webhook_url;
	const char *env_var;
};

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
 * @brief Cleanup all resources used by libcurl, including the handler,
 * curl_slist and escape'd chars.
 *
 * @param hnd    curl handler
 * @param escape Escape string if any (leave NULL if there's none).
 * @param slist  String list if any (leave NULL if there's none).
 */
static void do_curl_cleanup(CURL *hnd, char *escape, struct curl_slist *slist)
{
	curl_free(escape);
	curl_slist_free_all(slist);
	if (hnd)
		curl_easy_cleanup(hnd);
}

/**
 * @brief Finally sends a curl request, check its return code
 * and then cleanup the resources allocated.
 *
 * @param hnd    curl handler
 * @param escape Escape string if any (leave NULL if there's none).
 * @param slist  String list if any (leave NULL if there's none).
 *
 * @return Returns CURLE_OK if success, !CURLE_OK if error.
 */
static CURLcode do_curl(CURL *hnd, char *escape, struct curl_slist *slist)
{
	long response_code = 0;
	CURLcode ret_curl  = !CURLE_OK;

#ifndef DISABLE_NOTIFICATIONS
	ret_curl = curl_easy_perform(hnd);
	if (ret_curl != CURLE_OK) {
		log_msg("> Unable to send request!\n");
		goto error;
	}
	else {
		curl_easy_getinfo(hnd, CURLINFO_RESPONSE_CODE, &response_code);
		log_msg("> Done!\n", response_code);
		if (response_code != 200) {
			log_msg("(Info: Response code != 200 (%ld), your message might "
			        "not be correctly sent!)\n", response_code);
		}
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
 * @brief Sends a generic webhook POST request with JSON payload in the
 * format {"text": "text here"}.
 *
 * @param url  Target webhook URL.
 * @param text Text to be sent in the json payload.
 *
 * @return Returns CURLE_OK if success, 1 if error.
 */
static int send_generic_webhook(const char *url, const char *text)
{
	CURL *hnd               = NULL;
	struct curl_slist *s    = NULL;
	struct str_ab payload_data;
	const char *t;

	if (!(hnd = curl_easy_init())) {
		log_msg("Failed to initialize libcurl!\n");
		return 1;
	}

	ab_init(&payload_data);
	ab_append_str(&payload_data, "{\"text\":\"", 9);

	/* Append the payload data text while escaping double
	 * quotes.
	 */
	for (t = text; *t != '\0'; t++) {
		if (*t != '"') {
			if (ab_append_chr(&payload_data, *t) < 0)
				return 1;
		}
		else {
			if (ab_append_str(&payload_data, "\\\"", 2) < 0)
				return 1;
		}
	}

	/* End the string. */
	if (ab_append_str(&payload_data, "\"}", 2) < 0)
		return 1;

	if (setopts_post_json_curl(hnd, url, payload_data.buff, &s))
		return 1;

	log_msg("> Sending notification!\n");
	return do_curl(hnd, NULL, s);
}


///////////////////////////////////////////////////////////////////////////////
//////////////////////////////// TELEGRAM /////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

/* Telegram & request settings. */
static char *telegram_bot_token;
static char *telegram_chat_id;

void setup_telegram(struct notifier *self)
{
	static int setup = 0;
	if (setup)
		return;

	((void)self);

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

static int send_telegram_notification(const struct notifier *self, const char *msg)
{
	struct str_ab full_request_url;
	char *escaped_msg = NULL;
	CURL *hnd         = NULL;
	int  ret;

	((void)self);

	if (!(hnd = curl_easy_init())) {
		log_msg("Failed to initialize libcurl!\n");
		return -1;
	}

	escaped_msg = curl_easy_escape(hnd, msg, 0);
	if (!escaped_msg) {
		log_msg("> Unable to escape notification message...\n");
		do_curl_cleanup(hnd, escaped_msg, NULL);
	}

	ab_init(&full_request_url);

	ret = ab_append_fmt(&full_request_url,
		"https://api.telegram.org/bot%s/sendMessage?chat_id=%s&text=%s",
		telegram_bot_token, telegram_chat_id, escaped_msg);

	if (ret)
		return -1;

	setopts_get_curl(hnd, full_request_url.buff);
	log_msg("> Sending notification!\n");
	return do_curl(hnd, escaped_msg, NULL);
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////// GENERIC /////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

void setup_generic_webhook(struct notifier *self)
{
	struct webhook_data *data = self->data;

	if (data->webhook_url)
		return;

	data->webhook_url = getenv(data->env_var);
	if (!data->webhook_url) {
		panic("Unable to find env vars, please check if you have set the %s!!\n",
			data->env_var);
	}
}

static int send_generic_webhook_notification(
	const struct notifier *self, const char *msg)
{
	struct webhook_data *data = self->data;
    return send_generic_webhook(data->webhook_url, msg);
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////// DISCORD /////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

/* Discord in Slack-compatible mode. */
static int send_discord_notification(
	const struct notifier *self, const char *msg)
{
	struct webhook_data *data = self->data;
	struct str_ab url;
	ab_init(&url);
	if (ab_append_fmt(&url, "%s/slack", data->webhook_url) < 0)
		return 1;
	return send_generic_webhook(url.buff, msg);
}

////////////////////////////////// END ////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

const char *const notifiers_str[] = {
	"Telegram", "Slack",    "Teams",    "Discord",
	"Generic1", "Generic2", "Generic3", "Generic4"
};

struct notifier notifiers[] = {
	/* Telegram. */
	{
		.setup             = setup_telegram,
		.send_notification = send_telegram_notification,
	},
	/* Slack. */
	{
		.setup             = setup_generic_webhook,
		.send_notification = send_generic_webhook_notification,
		.data              = &(struct webhook_data)
		                     {.env_var = "SLACK_WEBHOOK_URL"},
	},
	/* Teams. */
	{
		.setup             = setup_generic_webhook,
		.send_notification = send_generic_webhook_notification,
		.data              = &(struct webhook_data)
		                     {.env_var = "TEAMS_WEBHOOK_URL"},
	},
	/*
	 * Discord:
	 * Since Discord doesn't follow like the others, we need
	 * to slightly change the URL before proceeding, so this
	 * is why its function is not generic!.
	 */
	{
		.setup             = setup_generic_webhook,
		.send_notification = send_discord_notification,
		.data              = &(struct webhook_data)
		                     {.env_var = "DISCORD_WEBHOOK_URL"},
	}
};
