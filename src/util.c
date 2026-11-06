#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

int ofwc_debug;

void
die(const char *fmt, ...)
{
	va_list ap;

	fputs("ofwc: ", stderr);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	fflush(stderr);
	exit(EXIT_FAILURE);
}

void
ofwc_log(const char *fmt, ...)
{
	va_list ap;

	fputs("ofwc: ", stderr);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	fflush(stderr);
}

void
ofwc_dbg(const char *fmt, ...)
{
	va_list ap;

	if (!ofwc_debug)
		return;
	fputs("ofwc[debug]: ", stderr);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	fflush(stderr);
}

void *
ecalloc(size_t nmemb, size_t size)
{
	void *p = calloc(nmemb ? nmemb : 1, size ? size : 1);
	if (!p)
		die("out of memory");
	return p;
}

void *
erealloc(void *ptr, size_t size)
{
	void *p = realloc(ptr, size ? size : 1);
	if (!p)
		die("out of memory");
	return p;
}

char *
estrdup(const char *s)
{
	size_t n = strlen(s) + 1;
	char *p = ecalloc(n, 1);
	memcpy(p, s, n);
	return p;
}
