/*
 * Alertik: a tiny 'syslog' server & notification tool for Mikrotik routers.
 * This is free and unencumbered software released into the public domain.
 */

#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netdb.h>

#include <curl/curl.h>

#include "alertik.h"
#include "events.h"

/* Uncomment/comment to enable/disable the following settings. */
// #define USE_FILE_AS_LOG           /* stdout if commented. */
// #define CURL_VERBOSE
// #define VALIDATE_CERTS
// #define DISABLE_NOTIFICATIONS

#define FIFO_MAX    64
#define SYSLOG_PORT 5140
#define LOG_FILE    "log/log.txt"

/* Telegram & request settings. */
static char *TELEGRAM_BOT_TOKEN;
static char *TELEGRAM_CHAT_ID;
char *TELEGRAM_NICKNAME;

#define CURL_USER_AGENT "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 " \
                        "(KHTML, like Gecko) Chrome/125.0.0.0 Safari/537.36"

/* Circular message buffer. */
static struct circ_buffer {
	int head;
	int tail;
	struct log_event log_ev [FIFO_MAX];
} circ_buffer = {0};

/* Sync. */
static pthread_mutex_t fifo_mutex        = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t log_mutex         = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t fifo_new_log_entry = PTHREAD_COND_INITIALIZER;

/* Misc. */
#define LAST_SENT_THRESHOLD_SECS 10  /* Minimum time (in secs) between two */
static time_t time_last_sent_notify; /* notifications. */
static int curr_file;

//////////////////////////////// LOGGING //////////////////////////////////////

/* There should *always* be a corresponding close_log_file() call. */
static inline void open_log_file(void)
{
	struct stat sb;

	pthread_mutex_lock(&log_mutex);
		if (curr_file == STDOUT_FILENO)
			return;

		if (stat("log", &sb) < 0)
			if (mkdir("log", 0755) < 0)
				return;

		curr_file = openat(AT_FDCWD, LOG_FILE,
			O_WRONLY|O_CREAT|O_APPEND, 0666);

		if (curr_file < 0)
			curr_file = STDOUT_FILENO; /* fallback to stdout if can't open. */
}

/* This should *always* be called *after* a call to open_log_file(). */
static void close_log_file(void)
{
		if (curr_file || curr_file == STDOUT_FILENO)
			goto out;

		fsync(curr_file);
		close(curr_file);
out:
	pthread_mutex_unlock(&log_mutex);
}

char *get_formatted_time(time_t time, char *time_str)
{
	strftime(
		time_str,
		32,
		"%Y-%m-%d %H:%M:%S",
		localtime(&time)
	);
	return time_str;
}

void log_msg(const char *fmt, ...)
{
	char time_str[32] = {0};
	va_list ap;

	open_log_file();
		dprintf(curr_file, "[%s] ", get_formatted_time(time(NULL), time_str));
		va_start(ap, fmt);
		vdprintf(curr_file, fmt, ap);
		va_end(ap);
	close_log_file();
}

static inline void print_log_event(struct log_event *ev)
{
	char time_str[32] = {0};
	open_log_file();
		dprintf(curr_file, "\n[%s] %s\n",
			get_formatted_time(ev->timestamp, time_str), ev->msg);
	close_log_file();
}

/////////////////////////////////// NETWORK ///////////////////////////////////
static int push_msg_into_fifo(const char *msg, time_t timestamp);

static int create_socket(void)
{
	struct sockaddr_in svaddr;
	int yes;
	int fd;

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0)
		panic_errno("Unable to create UDP socket...");

	memset(&svaddr, 0, sizeof(svaddr));
	svaddr.sin_family      = AF_INET;
	svaddr.sin_addr.s_addr = INADDR_ANY;
	svaddr.sin_port = SYSLOG_PORT;

	if (bind(fd, (const struct sockaddr *)&svaddr, sizeof(svaddr)) < 0)
		panic_errno("Unable to bind...");

	yes = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (void*)&yes,
		sizeof(yes)) < 0) {
		panic_errno("Unable to reuse address...");
	}

	return fd;
}

static int read_new_upd_msg(int fd)
{
	struct sockaddr_storage cli;
	char msg[MSG_MAX] = {0};
	socklen_t clilen;
	ssize_t ret;

	ret = recvfrom(fd, msg, sizeof msg - 1, 0, (struct sockaddr*)&cli,
		&clilen);

	if (ret < 0)
		return -1;

	if (push_msg_into_fifo(msg, time(NULL)) < 0)
		panic("Circular buffer full! (size: %d)\n", FIFO_MAX);

	return 0;
}

///////////////////////////////// FIFO ////////////////////////////////////////

static int push_msg_into_fifo(const char *msg, time_t timestamp)
{
	int next;
	int head;

	pthread_mutex_lock(&fifo_mutex);
		head = circ_buffer.head;
		next = head + 1;
		if (next >= FIFO_MAX)
			next = 0;

		if (next == circ_buffer.tail) {
			pthread_mutex_unlock(&fifo_mutex);
			return -1;
		}

		memcpy(circ_buffer.log_ev[head].msg, msg, MSG_MAX);
		circ_buffer.log_ev[head].timestamp = timestamp;

		circ_buffer.head = next;
		pthread_cond_signal(&fifo_new_log_entry);
	pthread_mutex_unlock(&fifo_mutex);

	return 0;
}

static int pop_msg_from_fifo(struct log_event *ev)
{
	int next;
	int tail;

	pthread_mutex_lock(&fifo_mutex);
		while (circ_buffer.head == circ_buffer.tail) {
			pthread_cond_wait(&fifo_new_log_entry, &fifo_mutex);
		}

		next = circ_buffer.tail + 1;
		if (next >= FIFO_MAX)
			next = 0;

		tail = circ_buffer.tail;
		ev->timestamp = circ_buffer.log_ev[tail].timestamp;
		memcpy(ev->msg, circ_buffer.log_ev[tail].msg, MSG_MAX);

		circ_buffer.tail = next;
	pthread_mutex_unlock(&fifo_mutex);

	return 0;
}

///////////////////////////// MESSAGE HANDLING ////////////////////////////////

/* Just to omit the print to stdout. */
size_t libcurl_noop_cb(void *ptr, size_t size, size_t nmemb, void *data) {
	((void)ptr);
	((void)data);
	return size * nmemb;
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
		TELEGRAM_BOT_TOKEN, TELEGRAM_CHAT_ID, escaped_msg);

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

static void *handle_messages(void *p)
{
	((void)p);
	size_t i;
	struct log_event ev = {0};

	while (pop_msg_from_fifo(&ev) >= 0) {
		print_log_event(&ev);

		if ((time(NULL) - time_last_sent_notify) <= LAST_SENT_THRESHOLD_SECS) {
			log_msg("ignoring, reason: too many notifications!\n");
			continue;
		}

		/* Check if it belongs to any of our desired events. */
		for (i = 0; i < NUM_EVENTS; i++) {
			if (strstr(ev.msg, handlers[i].str)) {
				handlers[i].hnd(&ev);
				break;
			}
		}

		if (i == NUM_EVENTS)
			log_msg("> No match!\n");
	}
	return NULL;
}

int main(void)
{
	pthread_t handler;
	int fd;

	atexit(close_log_file);

	TELEGRAM_BOT_TOKEN = getenv("TELEGRAM_BOT_TOKEN");
	TELEGRAM_CHAT_ID   = getenv("TELEGRAM_CHAT_ID");
	TELEGRAM_NICKNAME  = getenv("TELEGRAM_NICKNAME");

#ifndef USE_FILE_AS_LOG
	curr_file = STDOUT_FILENO;
#endif

	if (!TELEGRAM_BOT_TOKEN || !TELEGRAM_CHAT_ID || !TELEGRAM_NICKNAME) {
		panic("Unable to find env vars, please check if you have all of the "
			"following set:\n"
			"- TELEGRAM_BOT_TOKEN\n"
			"- TELEGRAM_CHAT_ID\n"
			"- TELEGRAM_NICKNAME\n");
	}

	log_msg(
		"Alertik (" GIT_HASH ") (built at " __DATE__ " " __TIME__ ")\n");
	log_msg("     (https://github.com/Theldus/alertik)\n");
	log_msg("-------------------------------------------------\n");

	fd = create_socket();
	if (pthread_create(&handler, NULL, handle_messages, NULL))
		panic_errno("Unable to create hanler thread!");

	log_msg("Waiting for messages at :%d (UDP)...\n", SYSLOG_PORT);

	while (read_new_upd_msg(fd) >= 0);
	return EXIT_SUCCESS;
}
