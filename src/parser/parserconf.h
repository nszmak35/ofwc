#ifndef OFWC_PARSERCONF_H
#define OFWC_PARSERCONF_H

#include <stddef.h>

typedef enum {
	OFWC_SCALAR = 0,
	OFWC_LIST,
	OFWC_TABLE
} OfwcType;

typedef struct OfwcEntry {
	OfwcType type;
	const char *key;
	const char *value;
	const char **items;
	size_t count;
	int source;
} OfwcEntry;

typedef int (*OfwcEntryCb)(const OfwcEntry *entry, void *userdata);

int ofwc_parse_buffer(const char *src, const char *name, int source,
                      OfwcEntryCb cb, void *userdata);

int ofwc_parse_file(const char *path, int source, OfwcEntryCb cb,
                    void *userdata, int *found);

#endif
