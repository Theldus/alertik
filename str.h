/*
 * Alertik: a tiny 'syslog' server & notification tool for Mikrotik routers.
 * This is free and unencumbered software released into the public domain.
 */

#ifndef STR_H
#define STR_H

	#include <stddef.h>

	/*
	 * Enable to disable malloc support and enable dinamically
	 * allocated buffer.
	 */
	#if 0
	#define AB_USE_MALLOC
	#endif

	/* Maximum highlighted line len, when built without malloc. */
	#define MAX_LINE 4096

	/* Append buffer. */
	struct str_ab
	{
	#ifndef AB_USE_MALLOC
		char buff[MAX_LINE + 1];
	#else
		char *buff;
	#endif
		size_t buff_len;
		size_t pos;
	};

	extern int ab_init(struct str_ab *ab);
	extern int ab_append_chr(struct str_ab *sh, char c);
	extern int ab_append_str(struct str_ab *ab, const char *s, size_t len);
	extern int ab_append_fmt(struct str_ab *ab, const char *fmt, ...);

#endif /* STR_H. */
