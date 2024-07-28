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

	/*
	 * Notifier indexes.
	 */
	#define NUM_NOTIFIERS 8
	#define NOTIFY_IDX_TELE    0
	#define NOTIFY_IDX_SLACK   1
	#define NOTIFY_IDX_TEAMS   2
	#define NOTIFY_IDX_DISCORD 3
	#define NOTIFY_IDX_GENRC1  (NUM_NOTIFIERS-4)
	#define NOTIFY_IDX_GENRC2  (NUM_NOTIFIERS-3)
	#define NOTIFY_IDX_GENRC3  (NUM_NOTIFIERS-2)
	#define NOTIFY_IDX_GENRC4  (NUM_NOTIFIERS-1)

	#define CURL_USER_AGENT "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 " \
	                        "(KHTML, like Gecko) Chrome/125.0.0.0 Safari/537.36"

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
		void *data;
		void(*setup)(struct notifier *self);
		int(*send_notification)(const struct notifier *self, const char *msg);
	};

	extern struct notifier notifiers[NUM_NOTIFIERS];
	extern int is_within_notify_threshold(void);
	extern void update_notify_last_sent(void);

#endif /* NOTIFIERS_H */
