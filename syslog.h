/*
 * Alertik: a tiny 'syslog' server & notification tool for Mikrotik routers.
 * This is free and unencumbered software released into the public domain.
 */

#ifndef SYSLOG_H
#define SYSLOG_H

	struct log_event;

	#define FIFO_MAX    64
	#define SYSLOG_PORT 5140

	extern int syslog_create_udp_socket(void);
	extern int syslog_enqueue_new_upd_msg(int fd);
	extern int syslog_pop_msg_from_fifo(struct log_event *ev);

#endif /* SYSLOG_H */
