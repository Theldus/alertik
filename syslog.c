/*
 * Alertik: a tiny 'syslog' server & notification tool for Mikrotik routers.
 * This is free and unencumbered software released into the public domain.
 */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <time.h>

#include "events.h"
#include "log.h"
#include "syslog.h"

/*
 * UDP message handling and FIFO.
 */

/* Forward server data. */
static struct addrinfo *fwd_addr_info;
static int fwd_fd;

/* Circular message buffer. */
static struct circ_buffer {
	int head;
	int tail;
	struct log_event log_ev [FIFO_MAX];
} circ_buffer = {0};

/* Sync. */
static pthread_mutex_t fifo_mutex        = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t fifo_new_log_entry = PTHREAD_COND_INITIALIZER;
static int syslog_push_msg_into_fifo(const char *, time_t);


/**
 * @brief Create an UDP socket to read from.
 *
 * @return Returns the UDP socket fd if success.
 */
int syslog_create_udp_socket(void)
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

/**
 * @brief Initializes the forwarding to the syslog server if
 * the required environment vars were informed.
 *
 * @return Returns 0.
 */
int syslog_init_forward(void)
{
	struct addrinfo hints, *results, *try;
	char *host, *port;
	int sock = 0;

	/* Check if we should forward messages. */
	host = getenv("FORWARD_HOST");
	port = getenv("FORWARD_PORT");
	if (!host && !port) {
		log_msg("Forward Mode: disabled\n\n");
		return 0;
	}

	if (!host || !port)
		panic("FORWARD_ADDR and FORWARD_PORT must be specified!\n");

	memset(&hints, 0, sizeof(hints));
	hints.ai_family   = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;

	if (getaddrinfo(host, port, &hints, &results) != 0)
		panic_errno("Unable to getaddrinfo...");

	/* Iterate over results. */
	for (try = results; try != NULL; try = try->ai_next) {
		sock = socket(try->ai_family, try->ai_socktype, try->ai_protocol);
		if (sock < 0)
			continue;
		break;
	}

	if (sock < 0)
		panic("Unable to create a socket for forward...\n");

	fwd_fd = sock;
	fwd_addr_info = try;

	log_msg("Forward Mode: enabled:\n");
	log_msg("----------------------\n");
	log_msg("FORWARD_HOST: %s\n", host);
	log_msg("FORWARD_PORT: %s\n\n", port);
	return 0;
}

/**
 * @brief Sends a message @p msg of length @p len to the
 * configured syslog server.
 */
static int syslog_fwd_msg(const char *msg, size_t len) {
	return (
		sendto(fwd_fd, msg, len, 0, fwd_addr_info->ai_addr,
			fwd_addr_info->ai_addrlen)
	);
}

/**
 * @brief Receives a new UDP message and then adds it
 * to the message queue. Additionally, also forwards
 * the message to a previously configured syslog server.
 *
 * @param fd UDP file descriptor to receive from.
 *
 * @return Returns 0 if success, -1 otherwise.
 */
int syslog_enqueue_new_upd_msg(int fd)
{
	struct sockaddr_storage cli = {0};
	char msg[MSG_MAX] = {0};
	socklen_t clilen;
	ssize_t ret;

	clilen = sizeof(cli);
	ret = recvfrom(fd, msg, sizeof msg - 1, 0, (struct sockaddr*)&cli,
		&clilen);

	if (ret < 0)
		return -1;

	/* Forward message if forwarding was configured. */
	if (fwd_fd) {
		if (syslog_fwd_msg(msg, ret) < 0)
			log_errno("Unable to forward message...\n");
	}

	if (syslog_push_msg_into_fifo(msg, time(NULL)) < 0)
		panic("Circular buffer full! (size: %d)\n", FIFO_MAX);

	return 0;
}



///////////////////////////////// FIFO ////////////////////////////////////////
/**
 * @brief For a given message @p msg and a timestamp @p timestamp,
 * adds both to the message queue and then wakes up the waiting
 * thread.
 *
 * @param msg       Read message from UDP.
 * @param timestamp Current timestamp.
 *
 * @return Returns 0 if success, -1 otherwise.
 */
static int syslog_push_msg_into_fifo(const char *msg, time_t timestamp)
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

/**
 * @brief Pops a single message from the message queue (if any),
 * and saves it into @p ev.
 *
 * @param ev Target buffer to the retrieved log event.
 *
 * @return Returns 0.
 */
int syslog_pop_msg_from_fifo(struct log_event *ev)
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
