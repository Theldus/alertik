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
#include "events.h"
#include "env_events.h"
#include "notifiers.h"
#include "str.h"

/*
 * Environment events
 */

/* Regex params. */
#define MAX_MATCHES 32

/* Event match types. */
#define MATCH_TYPES_LEN 2
static const char *const match_types[] = {"substr", "regex"};

/* Environment events list. */
static int num_env_events;
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
 * @param out  Pointer to integer.
 * @param s    String to be converted.
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

/**
 * @brief Retrieves the event string from the environment variables.
 *
 * @param ev_num Event number.
 * @param str    String identifier.
 *
 * @return Returns the event string.
 */
static char *get_event_str(int ev_num, char *str)
{
	char *env;
	char ev[64] = {0};
	snprintf(ev, sizeof ev - 1, "EVENT%d_%s", ev_num, str);
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
get_event_idx(int ev_num, char *str, const char *const *str_list, int size)
{
	char *env = get_event_str(ev_num, str);
	for (int i = 0; i < size; i++) {
		if (!strcmp(env, str_list[i]))
			return i;
	}
	panic("String parameter (%s) invalid for %s\n", env, str);
}

/**
 * @brief Handles match replacement in the event mask message.
 *
 * @param notif_message Pointer to the append buffer.
 * @param c_msk   Pointer to the current position in the mask message.
 * @param e_msk   End of the mask message.
 * @param pmatch  Array of regex matches.
 * @param env     Pointer to the environment event.
 * @param log_ev  Pointer to the log event.
 *
 * @return Returns 1 if the replacement was handled, 0 otherwise.
 */
static int handle_match_replacement(
	struct str_ab *notif_message,
	const char **c_msk, const char *e_msk,
	regmatch_t *pmatch,
	struct env_event *env,
	struct log_event *log_ev)
{
	const char *c = *c_msk;
	const char *e =  e_msk;
	size_t match  = *c - '0';
	regoff_t off;
	regoff_t len;

	/* Check if there is a second digit. */
	if (c < e) {
		if (c[1] >= '0' && c[1] <= '9') {
			match = (match * 10) + (c[1] - '0');
			c++;
		}
	}

	/* Validate if read number is within the match range
	 * i.e., between 1-nsub.
	 */
	if (!match || match > env->regex.re_nsub)
		return 0;

	*c_msk = c;

	/* Append c_msk into our dst, according to the informed
	 * match.
	 */
	off = pmatch[match].rm_so;
	len = pmatch[match].rm_eo - off;

	if (ab_append_str(notif_message, log_ev->msg + off, len) < 0)
		return 0;

	return 1;
}

/**
 * @brief Creates a masked message based on the base mask string
 * and the matches found.
 *
 * @param env       Pointer to the environment event.
 * @param pmatch    Array of regex matches.
 * @param log_ev    Pointer to the log event.
 * @param buf       Buffer to store the masked message.
 * @param buf_size  Size of the buffer.
 *
 * @return Returns the pointer to the end of the masked message.
 */
static int
create_masked_message(struct env_event *env, regmatch_t *pmatch,
	struct log_event *log_ev, struct str_ab *notif_message)
{
	const char *c_msk, *e_msk;

	c_msk = env->ev_mask_msg;
	e_msk = c_msk + strlen(c_msk);

	for (; *c_msk != '\0'; c_msk++)
	{
		if (*c_msk != '@') {
			if (ab_append_chr(notif_message, *c_msk) < 0)
				break;
			continue;
		}

		/* Abort if there is no next char to look ahead. */
		else if (c_msk + 1 >= e_msk)
			break;

		/* Look next char, in order to escape it if needed.
		 * If next is also '@',escape it.
		 */
		if (c_msk[1] == '@') {
			if (ab_append_chr(notif_message, *c_msk) < 0)
				break;
			else {
				c_msk++;
				continue; /* skip next char (since we already read it). */
			}
		}

		/* If not a number, abort. */
		else if (!(c_msk[1] >= '0' && c_msk[1] <= '9')) {
			log_msg("Warning: expected number at input, but found (%c), "
				    "the resulting message will be incomplete!\n",
				    c_msk[1]);
			break;
		}

		/* Its a number, proceed the replacement. */
		else
		{
			c_msk++;
			if (!handle_match_replacement(notif_message, &c_msk, e_msk,
				pmatch, env, log_ev))
			{
				break;
			}
		}
	}

	/* If we could parse the entire mask. */
	return (*c_msk == '\0');
}

/**
 * @brief Handles a log event with a regex match.
 *
 * @param ev      Pointer to the log event.
 * @param idx_env Index of the environment event.
 *
 * @return Returns 1 if the event was handled, 0 otherwise.
 */
static int handle_regex(struct log_event *ev, int idx_env)
{
	char time_str[32]               = {0};
	regmatch_t pmatch[MAX_MATCHES]  = {0};
	struct str_ab notif_message;
	struct notifier *self;

	int ret;
	int notif_idx;
	struct env_event *env_ev;

	env_ev    = &env_events[idx_env];
	notif_idx = env_ev->ev_notifier_idx;
	self      = &notifiers[notif_idx];

	if (regexec(&env_ev->regex, ev->msg, MAX_MATCHES, pmatch, 0) == REG_NOMATCH)
		return 0;

	log_msg("> Environment event detected!\n");
	log_msg(">   type         : regex\n");
	log_msg(">   expr         : %s\n",  env_ev->ev_match_str);
	log_msg(">   amnt sub expr: %zu\n", env_ev->regex.re_nsub);
	log_msg(">   notifier     : %s\n",  notifiers_str[notif_idx]);

	ab_init(&notif_message);

	/* Check if there are any subexpressions, if not, just format
	 * the message.
	 */
	if (env_ev->regex.re_nsub) {
		if (!create_masked_message(env_ev, pmatch, ev, &notif_message)) {
			log_msg("Unable to create masked message!\n");
			return 0;
		}

		if (ab_append_fmt(&notif_message, ", at: %s",
		    get_formatted_time(ev->timestamp, time_str)))
		{
			return 0;
		}
	}

	else {
		ret = ab_append_fmt(&notif_message,
			"%s, at: %s",
			env_ev->ev_mask_msg,
			get_formatted_time(ev->timestamp, time_str));

		if (ret)
			return 0;
	}

	if (self->send_notification(self, notif_message.buff) < 0) {
		log_msg("unable to send the notification through %s\n",
			notifiers_str[notif_idx]);
	}

	return 1;
}

/**
 * @brief Handles a log event with a substring match.
 *
 * @param ev       Pointer to the log event.
 * @param idx_env  Index of the environment event.
 *
 * @return Returns 1 if the event was handled, 0 otherwise.
 */
static int handle_substr(struct log_event *ev, int idx_env)
{
	int ret;
	int notif_idx;
	char time_str[32] = {0};

	struct notifier *self;
	struct env_event *env_ev;
	struct str_ab notif_message;

	env_ev    = &env_events[idx_env];
	notif_idx = env_ev->ev_notifier_idx;
	self      = &notifiers[notif_idx];

	if (!strstr(ev->msg, env_ev->ev_match_str))
		return 0;

	log_msg("> Environment event detected!\n");
	log_msg(">   type: substr, match: (%s), notifier: %s\n",
		env_ev->ev_match_str, notifiers_str[notif_idx]);

	ab_init(&notif_message);

	/* Format the message. */
	ret = ab_append_fmt(&notif_message,
		"%s, at: %s",
		env_ev->ev_mask_msg,
		get_formatted_time(ev->timestamp, time_str)
	);

	if (ret)
		return 0;

	if (self->send_notification(self, notif_message.buff) < 0) {
		log_msg("unable to send the notification through %s\n",
			notifiers_str[notif_idx]);
	}

	return 1;
}

/**
 * @brief Given an environment-variable event, checks if it
 * belongs to one of the registered events and then, handle
 * it.
 *
 * @param ev Event to be processed.
 *
 * @return Returns the amount of matches, 0 if none (not handled).
 */
int process_environment_event(struct log_event *ev)
{
	int i;
	int handled;

	for (i = 0, handled = 0; i < num_env_events; i++) {
		if (env_events[i].ev_match_type == EVNT_SUBSTR)
			handled += handle_substr(ev, i);
		else
			handled += handle_regex(ev, i);
	}
	return handled;
}

/**
 * @brief Initialize environment variables events.
 *
 * @return Returns 0 if there is no environment event,
 * 1 if there is at least one _and_ is successfully
 * configured.
*/
int init_environment_events(void)
{
	struct notifier *self;
	char *tmp;
	tmp = getenv("ENV_EVENTS");

	if (!tmp || (str2int(&num_env_events, tmp) < 0) || num_env_events <= 0)  {
		log_msg("Environment events not detected, disabling...\n");
		return (0);
	}

	if (num_env_events >= MAX_ENV_EVENTS)
		panic("Environment ENV_EVENTS exceeds the maximum supported (%d/%d)\n",
			num_env_events, MAX_ENV_EVENTS);

	log_msg("%d environment event(s) found, registering...\n", num_env_events);
	for (int i = 0; i < num_env_events; i++) {
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
	for (int i = 0; i < num_env_events; i++)
	{
		log_msg("EVENT%d_MATCH_TYPE: %s\n", i,
				match_types[env_events[i].ev_match_type]);
		log_msg("EVENT%d_MATCH_STR:  %s\n", i, env_events[i].ev_match_str);
		log_msg("EVENT%d_NOTIFIER:   %s\n", i,
				notifiers_str[env_events[i].ev_notifier_idx]);
		log_msg("EVENT%d_MASK_MSG:   %s\n\n", i, env_events[i].ev_mask_msg);

		/* Try to setup notifier if not yet. */
		self = &notifiers[env_events[i].ev_notifier_idx];
		self->setup(self);

		/* If regex, compile it first. */
		if (env_events[i].ev_match_type == EVNT_REGEX) {
			if (regcomp(
			    &env_events[i].regex,
			    env_events[i].ev_match_str,
			    REG_EXTENDED))
			{
				panic("Unable to compile regex (%s) for EVENT%d!!!",
					env_events[i].ev_match_str, i);
			}
		}
	}
	return 1;
}
