#ifndef OFWC_UTIL_H
#define OFWC_UTIL_H

#include <stddef.h>
#include <stdio.h>

#define LENGTH(x) (sizeof(x) / sizeof((x)[0]))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

extern int ofwc_debug;

void die(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));
void ofwc_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void ofwc_dbg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void *ecalloc(size_t nmemb, size_t size);
void *erealloc(void *ptr, size_t size);
char *estrdup(const char *s);

#endif
