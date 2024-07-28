/*
 * Alertik: a tiny 'syslog' server & notification tool for Mikrotik routers.
 * This is free and unencumbered software released into the public domain.
 */

/*
 * String/append buffer implementation based on Aqua:
 *  https://gist.github.com/Theldus/09ed2205aa5ba15cdf4571b71cd1c8fc
 */

#include "str.h"
#include "log.h"

/* Malloc is only used if AB_USE_MALLOC is defined. */
#ifdef AB_USE_MALLOC
#if defined(AB_CALLOC) && defined(AB_REALLOC) && defined(AB_FREE)
#	define AB_USE_STDLIB
#elif !defined(AB_CALLOC) && !defined(AB_REALLOC) && !defined(AB_FREE)
#	define AB_USE_STDLIB
#else
#error "For custom memory allocators, you should define all three routines!"
#error "Please define: AB_CALLOC, AB_REALLOC and AB_FREE!"
#endif
#endif

#ifndef AB_CALLOC
#define AB_CALLOC(nmemb,sz) calloc((nmemb),(sz))
#define AB_REALLOC(p,newsz) realloc((p),(newsz))
#define AB_FREE(p)          free((p))
#endif

#ifdef AB_USE_STDLIB
#include <stdlib.h>
#endif

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* ========================================================================= */
/*                            BUFFER ROUTINES                                */
/* ========================================================================= */

#ifdef AB_USE_MALLOC
/**
 * @brief Rounds up to the next power of two.
 *
 * @param target Target number to be rounded.
 *
 * @return Returns the next power of two.
 */
static size_t next_power(size_t target)
{
	target--;
	target |= target >> 1;
	target |= target >> 2;
	target |= target >> 4;
	target |= target >> 8;
	target |= target >> 16;
	target++;
	return (target);
}
#endif

/**
 * @brief Checks if the new size fits in the append buffer, if not,
 * reallocates the buffer size by @p incr bytes.
 *
 * If the macro AB_USE_MALLOC is not defined (default), this only
 * checks if the new size fits the buffer.
 *
 * @param sh Aqua highlight context.
 * @param incr Size (in bytes) to be incremented.
 *
 * @return Returns 0 if success, -1 otherwise.
 *
 * @note The new size is the next power of two, that is capable
 * to hold the required buffer size.
 */
static int increase_buff(struct str_ab *sh, size_t incr)
{
#ifndef AB_USE_MALLOC
	if (sh->pos + incr >= MAX_LINE) {
		log_msg("(increase buffer) Unable to fit appended message!\n");
		log_msg("(static storage)  incr: %zu, buff_len: %zu, pos: %zu\n",
			incr, sh->pos, sh->buff_len);
		return (-1);
	}
#else
	char *new;
	size_t new_size;
	if (sh->pos + incr >= sh->buff_len)
	{
		new_size = next_power(sh->buff_len + incr);
		new = AB_REALLOC(sh->buff, new_size);
		if (new == NULL)
		{
			AB_FREE(sh->buff);
			sh->buff = NULL;

			log_msg("(increase buffer) Unable to fit appended message!\n");
			log_msg("(realloc storage) incr: %zu, buff_len: %zu, pos: %zu\n",
				incr, sh->pos, sh->buff_len);
			return (-1);
		}
		sh->buff_len = new_size;
		sh->buff = new;
	}
#endif
	return (0);
}

/**
 * @brief Initializes the append buffer context.
 *
 * @param ab Append buffer structure.
 *
 * @return Returns 0 if success, -1 otherwise.
 */
int ab_init(struct str_ab *ab)
{
	if (!ab)
		return (-1);

	memset(ab, 0, sizeof(*ab));

#ifndef AB_USE_MALLOC
	ab->buff_len = MAX_LINE;
#else
	ab->buff = AB_CALLOC(MAX_LINE, 1);
	if (!ab->buff)
		return (-1);
	ab->buff_len = MAX_LINE;
#endif

	return (0);
}

/**
 * @brief Append a given char @p c into the buffer.
 *
 * @param sh Aqua highlight context.
 * @param c Char to be appended.
 *
 * @return Returns 0 if success, -1 otherwise.
 */
int ab_append_chr(struct str_ab *sh, char c)
{
	if (increase_buff(sh, 2) < 0)
		return (-1);

	sh->buff[sh->pos + 0] = c;
	sh->buff[sh->pos + 1] = '\0';
	sh->pos++;
	return (0);
}

/**
 * @brief Appends a given string pointed by @p s of size @p len
 * into the current buffer.
 *
 * If @p len is 0, the string is assumed to be null-terminated
 * and its length is obtained.
 *
 * @param ab  Append buffer context.
 * @param s   String to be append into the buffer.
 * @param len String size, if 0, it's length is obtained.
 *
 * @return Returns 0 if success, -1 otherwise.
 */
int ab_append_str(struct str_ab *ab, const char *s, size_t len)
{
	if (!len)
		len = strlen(s);

	if (increase_buff(ab, len + 1) < 0)
		return (-1);

	memcpy(ab->buff + ab->pos, s, len);
	ab->pos += len;
	ab->buff[ab->pos] = '\0';
	return (0);
}

/**
 * @brief Appends a given formatted string pointed by @p fmt.
 *
 * @param ab  Append buffer context.
 * @param fmt Formatted string to be appended.
 *
 * @return Returns 0 if success, -1 otherwise.
 */
int ab_append_fmt(struct str_ab *ab, const char *fmt, ...)
{
	int str_len, ab_len, orig_ab_pos;
	char *buff_st;
	va_list ap;

	buff_st = ab->buff     + ab->pos;
	ab_len  = ab->buff_len - ab->pos;

	va_start(ap, fmt);
		str_len = vsnprintf(buff_st, ab_len, fmt, ap);
		if (str_len < 0) {
			log_msg("Unable to fit appended message!\n");
			return (-1);
		}
	va_end(ap);

	/* If it fits, just happily returns. */
	if (str_len + 1 <= ab_len) {
		ab->pos += str_len;
		return (0);
	}

	/* Otherwise, adjust current pos and try to increase buffer. */
	else {
		orig_ab_pos = ab->pos;

		/* temporarily advance our buffer
		 * to trick our increase buffer. */
		ab->pos = ab->buff_len;

		if (increase_buff(ab, (str_len + 1) - ab_len))
			return (-1);

		ab->pos = orig_ab_pos;
	}

	buff_st = ab->buff     + ab->pos;
	ab_len  = ab->buff_len - ab->pos;

	va_start(ap, fmt);
		str_len = vsnprintf(buff_st, ab_len, fmt, ap);
		if (str_len < 0 || (str_len + 1) > ab_len) {
			log_msg("Unable to fit appended message!\n");
			return (-1);
		}
	va_end(ap);

	ab->pos += str_len;
	return (0);
}
