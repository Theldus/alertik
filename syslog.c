/*
 * Alertik: a tiny 'syslog' server & notification tool for Mikrotik routers.
 * This is free and unencumbered software released into the public domain.
 */

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <time.h>

#include "events.h"
#include "log.h"
#include "syslog.h"

/*
 * UDP message handling and FIFO.
 */

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


/* Create an UDP socket to read from. */
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

/**/
int syslog_enqueue_new_upd_msg(int fd)
{
	struct sockaddr_storage cli;
	char msg[MSG_MAX] = {0};
	socklen_t clilen;
	ssize_t ret;

	ret = recvfrom(fd, msg, sizeof msg - 1, 0, (struct sockaddr*)&cli,
		&clilen);

	if (ret < 0)
		return -1;

	if (syslog_push_msg_into_fifo(msg, time(NULL)) < 0)
		panic("Circular buffer full! (size: %d)\n", FIFO_MAX);

	return 0;
}



///////////////////////////////// FIFO ////////////////////////////////////////
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
