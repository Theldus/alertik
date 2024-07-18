/*
 * Alertik: a tiny 'syslog' server & notification tool for Mikrotik routers.
 * This is free and unencumbered software released into the public domain.
 */

#ifndef NOTIFIERS_H
#define NOTIFIERS_H

	/* Uncomment/comment to enable/disable the following settings. */
	// #define CURL_VERBOSE
	// #define VALIDATE_CERTS
	// #define DISABLE_NOTIFICATIONS

	#define CURL_USER_AGENT "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 " \
	                        "(KHTML, like Gecko) Chrome/125.0.0.0 Safari/537.36"

	#define NUM_NOTIFIERS 1

	/* Minimum time (in secs) between two */
	#define LAST_SENT_THRESHOLD_SECS 10

	/* Notifiers list, like:
	 * - Telegram
	 * - Slack
	 * - Discord
	 * - Teams
	 */
	extern const char *const notifiers_str[NUM_NOTIFIERS];

	/* Notifier struct. */
	struct notifier {
		void(*setup)(void);
		int(*send_notification)(const char *msg);
	};

	extern struct notifier notifiers[NUM_NOTIFIERS];
	extern void setup_notifiers(void);
	extern int is_within_notify_threshold(void);
	extern void update_notify_last_sent(void);

#endif /* NOTIFIERS_H */
