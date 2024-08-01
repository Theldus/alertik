/*
 * Alertik: a tiny 'syslog' server & notification tool for Mikrotik routers.
 * This is free and unencumbered software released into the public domain.
 */

#include <stdio.h>
#include <string.h>
#include <regex.h>
#include <stdint.h>

#ifdef DBG
#define LOG(...) printf(__VA_ARGS__)
#else
#define LOG(...)
#endif

int do_regex(
	const char *re,       const char *str,
	int32_t    *sub_expr, int32_t    *rm_so,   int32_t *rm_eo,
	char       *error_msg)
{
	int ret;
	regex_t    regex;
	regmatch_t pmatch[32];

	if ((ret = regcomp(&regex, re, REG_EXTENDED))) {
		regerror(ret, &regex, error_msg, 128);
		regfree(&regex);
		LOG("Error: %s\n", error_msg);
		return (-1);
	}

	if (regexec(&regex, str, 32, pmatch, 0) == REG_NOMATCH) {
		LOG("No match!\n");
		return (0);
	}

	*sub_expr = regex.re_nsub;
	if (!regex.re_nsub) {
		regfree(&regex);
		LOG("Match without subexpressions!\n");
		return (1);
	}

	LOG("N subexpr: %d\n", *sub_expr);

	/* If exists sub-expressions, save them. */
	for (size_t i = 0; i < regex.re_nsub + 1; i++) {
		rm_so[i - 0] = pmatch[i].rm_so;
		rm_eo[i - 0] = pmatch[i].rm_eo;
		LOG("rm_so[i-1] = %d\nrm_eo[i-1] = %d\n",
			rm_so[i],
			rm_eo[i]);
	}

	regfree(&regex);
	return (2);
}


#ifdef USE_C
int main(int argc, char **argv)
{
	int32_t se, so[32], eo[32];
	char  msg[128] = {0};

	char *re = argv[1];
	char *in = argv[2];

	if (argc < 3) {
		fprintf(stderr, "Usage: %s <regex> <input-text>\n", argv[0]);
		return (1);
	}

	printf("Regex     : %s\n", re);
	printf("input-text: %s\n", in);

	int r = do_regex(re, in, &se, so, eo, msg);
	switch (r) {
		case -1:
			printf("Error, reason: %s\n", msg);
			break;
		case 0:
			printf("No match!\n");
			break;
		case 1:
			printf("Match without sub-expressions!\n");
			break;
		case 2:
			printf("Match!!: %.*s\n", eo[0]-so[0], in+so[0]);
			printf("Found %d sub-expressions:\n", se);
			for (int i = 1; i < se+1; i++) {
				printf("$%d: %.*s\n", i, eo[i]-so[i], in+so[i]);
			}
			break;
	}

	return (0);
}
#endif
