#define _GNU_SOURCE
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>

#include "dwl-ipc-unstable-v2-client-protocol.h"
#include "ofwc-ipc-unstable-v1-client-protocol.h"

#define MAXTAGS 31

enum mode {
	MODE_NONE,
	MODE_WORKSPACE,
	MODE_FOCUS,
	MODE_MONITOR,
	MODE_KBLAYOUT,
	MODE_ALL
};

struct taginfo {
	uint32_t state, clients, focused;
};

struct monitor {
	struct wl_output *wl_output;
	struct zdwl_ipc_output_v2 *ipc_output;
	uint32_t registry_name;
	struct taginfo tags[MAXTAGS];
	int active;
	char *title, *appid;
	uint32_t fullscreen, floating, visibility;
};

struct state {
	struct wl_display *display;
	struct wl_registry *registry;
	struct zdwl_ipc_manager_v2 *manager;
	struct zofwc_ipc_v1 *ext;
	struct monitor **monitors;
	size_t nmonitors;
	uint32_t ntags;
	char *kb_layouts;
	uint32_t kb_index;
	enum mode mode;
	uint32_t workspace;
	int dirty;
};

static const char *progname = "ofwcctl";

static void
die(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fprintf(stderr, "%s: ", progname);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
	exit(EXIT_FAILURE);
}

static void *
xrealloc(void *p, size_t n)
{
	void *q = realloc(p, n);

	if (!q)
		die("out of memory");
	return q;
}

static char *
xstrdup(const char *s)
{
	char *p = strdup(s ? s : "");

	if (!p)
		die("out of memory");
	return p;
}

static void
set_string(char **dst, const char *value)
{
	free(*dst);
	*dst = xstrdup(value);
}

static void
json_string(const char *s)
{
	const unsigned char *p = (const unsigned char *)(s ? s : "");

	putchar('"');
	for (; *p; p++) {
		switch (*p) {
		case '\\': fputs("\\\\", stdout); break;
		case '"': fputs("\\\"", stdout); break;
		case '\b': fputs("\\b", stdout); break;
		case '\f': fputs("\\f", stdout); break;
		case '\n': fputs("\\n", stdout); break;
		case '\r': fputs("\\r", stdout); break;
		case '\t': fputs("\\t", stdout); break;
		default:
			if (*p < 0x20)
				printf("\\u%04x", (unsigned)*p);
			else
				putchar(*p);
		}
	}
	putchar('"');
}

static void
json_bool(const char *key, int value)
{
	printf(",\"%s\":%s", key, value ? "true" : "false");
}

static void
json_focus_fields(const struct monitor *m)
{
	fputs("{\"title\":", stdout);
	json_string(m->title);
	fputs(",\"appid\":", stdout);
	json_string(m->appid);
}

static void
json_focus(const struct monitor *m)
{
	json_focus_fields(m);
	putchar('}');
}

static int
kb_code(const char *layouts, uint32_t n, char *out, size_t cap)
{
	const char *p = layouts;
	uint32_t i = 0;

	out[0] = '\0';
	if (!p)
		return 0;
	for (;;) {
		const char *q = strchr(p, ',');
		size_t len = q ? (size_t)(q - p) : strlen(p), k;

		if (i == n) {
			while (len && *p == ' ') {
				p++;
				len--;
			}
			for (k = 0; k < len && p[k] != '(' && k + 1 < cap; k++)
				out[k] = p[k];
			out[k] = '\0';
			return 1;
		}
		if (!q)
			return 0;
		p = q + 1;
		i++;
	}
}

static uint32_t
kb_count(const char *layouts)
{
	char code[64];
	uint32_t n = 0;

	while (kb_code(layouts, n, code, sizeof(code)))
		n++;
	return n;
}

static void
kb_current(const struct state *s, char *out, size_t cap)
{
	if (!kb_code(s->kb_layouts, s->kb_index, out, cap))
		out[0] = '\0';
}

static struct monitor *
find_monitor(struct state *s, const struct zdwl_ipc_output_v2 *output)
{
	size_t i;

	for (i = 0; i < s->nmonitors; i++)
		if (s->monitors[i]->ipc_output == output)
			return s->monitors[i];
	return NULL;
}

static struct monitor *
active_monitor(const struct state *s)
{
	size_t i;

	for (i = 0; i < s->nmonitors; i++)
		if (s->monitors[i]->active)
			return s->monitors[i];
	return s->nmonitors ? s->monitors[0] : NULL;
}

static void
monitor_free(struct monitor *m)
{
	if (m->ipc_output)
		zdwl_ipc_output_v2_release(m->ipc_output);
	if (wl_output_get_version(m->wl_output) >= 3)
		wl_output_release(m->wl_output);
	else
		wl_output_destroy(m->wl_output);
	free(m->title);
	free(m->appid);
	free(m);
}

static void
state_free(struct state *s)
{
	size_t i;

	for (i = 0; i < s->nmonitors; i++)
		monitor_free(s->monitors[i]);
	free(s->monitors);
	free(s->kb_layouts);
	if (s->ext)
		zofwc_ipc_v1_destroy(s->ext);
	if (s->manager)
		zdwl_ipc_manager_v2_release(s->manager);
	if (s->registry)
		wl_registry_destroy(s->registry);
	if (s->display)
		wl_display_disconnect(s->display);
	memset(s, 0, sizeof(*s));
}

static void
monitor_json(const struct monitor *m, const struct state *s)
{
	uint32_t i;

	putchar('{');
	printf("\"id\":%u", m->registry_name);
	json_bool("active", m->active);
	json_bool("fullscreen", m->fullscreen);
	json_bool("floating", m->floating);
	json_bool("visibility", m->visibility);

	fputs(",\"workspaces\":[", stdout);
	for (i = 0; i < s->ntags; i++) {
		const struct taginfo *t = &m->tags[i];

		if (i)
			putchar(',');
		printf("{\"name\":\"%u\"", i + 1);
		json_bool("active", t->state & ZDWL_IPC_OUTPUT_V2_TAG_STATE_ACTIVE);
		json_bool("urgent", t->state & ZDWL_IPC_OUTPUT_V2_TAG_STATE_URGENT);
		printf(",\"windows\":%u", t->clients);
		json_bool("focused", t->focused);
		putchar('}');
	}
	putchar(']');

	fputs(",\"focus\":", stdout);
	if (m->title && *m->title)
		json_focus(m);
	else
		fputs("null", stdout);
	putchar('}');
}

static void
print_workspace(const struct monitor *m, const struct state *s)
{
	const struct taginfo *t = &m->tags[s->workspace - 1];

	printf("{\"workspace\":\"%u\"", s->workspace);
	json_bool("active", t->state & ZDWL_IPC_OUTPUT_V2_TAG_STATE_ACTIVE);
	json_bool("urgent", t->state & ZDWL_IPC_OUTPUT_V2_TAG_STATE_URGENT);
	printf(",\"windows\":%u,\"focused_window\":", t->clients);
	if (t->focused && m->title && *m->title)
		json_focus(m);
	else
		fputs("null", stdout);
	puts("}");
}

static void
print_all(const struct state *s)
{
	struct monitor *active = active_monitor(s);
	char code[64];
	size_t i;

	kb_current(s, code, sizeof(code));
	fputs("{\"keyboard_layout\":", stdout);
	json_string(code);
	fputs(",\"monitor\":", stdout);
	if (active)
		monitor_json(active, s);
	else
		fputs("null", stdout);
	fputs(",\"monitors\":[", stdout);
	for (i = 0; i < s->nmonitors; i++) {
		if (i)
			putchar(',');
		monitor_json(s->monitors[i], s);
	}
	puts("]}");
}

static void
emit(const struct state *s)
{
	struct monitor *m = active_monitor(s);
	char code[64];

	if (s->mode == MODE_KBLAYOUT) {
		kb_current(s, code, sizeof(code));
		puts(code);
	} else if (s->mode == MODE_ALL) {
		print_all(s);
	} else if (m) {
		switch (s->mode) {
		case MODE_FOCUS:
			json_focus_fields(m);
			json_bool("fullscreen", m->fullscreen);
			json_bool("floating", m->floating);
			puts("}");
			break;
		case MODE_MONITOR:
			monitor_json(m, s);
			putchar('\n');
			break;
		case MODE_WORKSPACE:
			print_workspace(m, s);
			break;
		default:
			break;
		}
	}
	fflush(stdout);
}

static void
output_toggle_visibility(void *data, struct zdwl_ipc_output_v2 *output)
{
	struct monitor *m = find_monitor(data, output);

	if (m)
		m->visibility = !m->visibility;
}

static void
output_active(void *data, struct zdwl_ipc_output_v2 *output, uint32_t active)
{
	struct monitor *m = find_monitor(data, output);

	if (m)
		m->active = active != 0;
}

static void
output_tag(void *data, struct zdwl_ipc_output_v2 *output, uint32_t tag,
           uint32_t state, uint32_t clients, uint32_t focused)
{
	struct monitor *m = find_monitor(data, output);

	if (!m || tag >= MAXTAGS)
		return;
	m->tags[tag].state = state;
	m->tags[tag].clients = clients;
	m->tags[tag].focused = focused;
}

static void
output_layout(void *data, struct zdwl_ipc_output_v2 *output, uint32_t layout)
{
	(void)data;
	(void)output;
	(void)layout;
}

static void
output_title(void *data, struct zdwl_ipc_output_v2 *output, const char *title)
{
	struct monitor *m = find_monitor(data, output);

	if (m)
		set_string(&m->title, title);
}

static void
output_appid(void *data, struct zdwl_ipc_output_v2 *output, const char *appid)
{
	struct monitor *m = find_monitor(data, output);

	if (m)
		set_string(&m->appid, appid);
}

static void
output_layout_symbol(void *data, struct zdwl_ipc_output_v2 *output,
                     const char *layout)
{
	(void)data;
	(void)output;
	(void)layout;
}

static void
output_frame(void *data, struct zdwl_ipc_output_v2 *output)
{
	struct state *s = data;

	(void)output;
	s->dirty = 1;
}

static void
output_fullscreen(void *data, struct zdwl_ipc_output_v2 *output, uint32_t value)
{
	struct monitor *m = find_monitor(data, output);

	if (m)
		m->fullscreen = value;
}

static void
output_floating(void *data, struct zdwl_ipc_output_v2 *output, uint32_t value)
{
	struct monitor *m = find_monitor(data, output);

	if (m)
		m->floating = value;
}

static const struct zdwl_ipc_output_v2_listener output_listener = {
	.toggle_visibility = output_toggle_visibility,
	.active = output_active,
	.tag = output_tag,
	.layout = output_layout,
	.title = output_title,
	.appid = output_appid,
	.layout_symbol = output_layout_symbol,
	.frame = output_frame,
	.fullscreen = output_fullscreen,
	.floating = output_floating,
};

static void
manager_tags(void *data, struct zdwl_ipc_manager_v2 *manager, uint32_t amount)
{
	struct state *s = data;

	(void)manager;
	s->ntags = amount < MAXTAGS ? amount : MAXTAGS;
	s->dirty = 1;
}

static void
manager_layout(void *data, struct zdwl_ipc_manager_v2 *manager, const char *name)
{
	(void)data;
	(void)manager;
	(void)name;
}

static const struct zdwl_ipc_manager_v2_listener manager_listener = {
	.tags = manager_tags,
	.layout = manager_layout,
};

static void
ext_keyboard_layouts(void *data, struct zofwc_ipc_v1 *ext, const char *layouts)
{
	struct state *s = data;

	(void)ext;
	set_string(&s->kb_layouts, layouts);
	s->dirty = 1;
}

static void
ext_keyboard_layout(void *data, struct zofwc_ipc_v1 *ext, uint32_t index)
{
	struct state *s = data;

	(void)ext;
	s->kb_index = index;
	s->dirty = 1;
}

static const struct zofwc_ipc_v1_listener ext_listener = {
	.keyboard_layouts = ext_keyboard_layouts,
	.keyboard_layout = ext_keyboard_layout,
};

static void
bind_monitor(struct state *s, struct monitor *m)
{
	m->ipc_output = zdwl_ipc_manager_v2_get_output(s->manager, m->wl_output);
	if (!m->ipc_output)
		die("cannot create the IPC output object");
	zdwl_ipc_output_v2_add_listener(m->ipc_output, &output_listener, s);
}

static void
registry_global(void *data, struct wl_registry *registry, uint32_t name,
                const char *interface, uint32_t version)
{
	struct state *s = data;

	if (!strcmp(interface, zdwl_ipc_manager_v2_interface.name)) {
		if (s->manager)
			return;
		s->manager = wl_registry_bind(registry, name, &zdwl_ipc_manager_v2_interface,
		                              version < 2 ? version : 2);
		zdwl_ipc_manager_v2_add_listener(s->manager, &manager_listener, s);
	} else if (!strcmp(interface, zofwc_ipc_v1_interface.name)) {
		if (s->ext)
			return;
		s->ext = wl_registry_bind(registry, name, &zofwc_ipc_v1_interface, 1);
		zofwc_ipc_v1_add_listener(s->ext, &ext_listener, s);
	} else if (!strcmp(interface, wl_output_interface.name)) {
		struct monitor *m = calloc(1, sizeof(*m));

		if (!m)
			die("out of memory");
		m->wl_output = wl_registry_bind(registry, name, &wl_output_interface,
		                                version < 3 ? version : 3);
		m->registry_name = name;
		s->monitors = xrealloc(s->monitors, (s->nmonitors + 1) * sizeof(*s->monitors));
		s->monitors[s->nmonitors++] = m;
		if (s->manager)
			bind_monitor(s, m);
	}
}

static void
registry_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
	struct state *s = data;
	size_t i;

	(void)registry;
	for (i = 0; i < s->nmonitors; i++) {
		if (s->monitors[i]->registry_name != name)
			continue;
		monitor_free(s->monitors[i]);
		memmove(&s->monitors[i], &s->monitors[i + 1],
		        (s->nmonitors - i - 1) * sizeof(*s->monitors));
		s->nmonitors--;
		s->dirty = 1;
		return;
	}
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_global,
	.global_remove = registry_global_remove,
};

static void
roundtrip(struct state *s)
{
	if (wl_display_roundtrip(s->display) < 0)
		die("connection to the compositor lost");
}

static void
connect_ipc(struct state *s, int need_ext)
{
	size_t i;

	s->display = wl_display_connect(NULL);
	if (!s->display)
		die("cannot connect to the Wayland display");
	s->registry = wl_display_get_registry(s->display);
	wl_registry_add_listener(s->registry, &registry_listener, s);
	roundtrip(s);

	if (!s->manager || (need_ext && !s->ext))
		die("ofwc IPC is unavailable (is this ofwc?)");
	for (i = 0; i < s->nmonitors; i++)
		if (!s->monitors[i]->ipc_output)
			bind_monitor(s, s->monitors[i]);
	roundtrip(s);
}

static void
set_workspace(const char *arg)
{
	struct state s = {0};
	struct monitor *m;
	char *end;
	long n;

	errno = 0;
	n = strtol(arg, &end, 10);
	if (errno || end == arg || *end || n < 1)
		die("workspace '%s' does not exist", arg);

	connect_ipc(&s, 0);
	if (n > (long)s.ntags)
		die("workspace '%s' does not exist", arg);
	m = active_monitor(&s);
	if (!m || !m->ipc_output)
		die("no active monitor");

	zdwl_ipc_output_v2_set_tags(m->ipc_output, 1u << (n - 1), 0);
	roundtrip(&s);
	state_free(&s);
}

static void
set_kblayout(void)
{
	struct state s = {0};
	uint32_t n;

	connect_ipc(&s, 1);
	n = kb_count(s.kb_layouts);
	if (n > 1) {
		zofwc_ipc_v1_set_keyboard_layout(s.ext, (s.kb_index + 1) % n);
		roundtrip(&s);
	}
	state_free(&s);
}

static void __attribute__((noreturn))
watch(enum mode mode, uint32_t workspace)
{
	struct state s = {0};

	s.mode = mode;
	s.workspace = workspace;
	connect_ipc(&s, mode == MODE_KBLAYOUT);
	if (mode == MODE_WORKSPACE && (workspace < 1 || workspace > s.ntags))
		die("workspace '%u' does not exist", workspace);

	emit(&s);
	s.dirty = 0;
	while (wl_display_dispatch(s.display) >= 0) {
		if (s.dirty) {
			s.dirty = 0;
			emit(&s);
		}
	}
	state_free(&s);
	die("connection to the compositor lost");
}

static void
usage(FILE *f)
{
	fputs("ofwcctl - inspect and control ofwc\n"
	      "\n"
	      "Usage:\n"
	      "  ofwcctl help\n"
	      "  ofwcctl set-workspace <number>\n"
	      "  ofwcctl set-kblayout            next keyboard layout (us -> ru -> us)\n"
	      "  ofwcctl watch-kblayout\n"
	      "  ofwcctl watch-monitor\n"
	      "  ofwcctl watch-workspace <number>\n"
	      "  ofwcctl watch-focus\n"
	      "  ofwcctl watch-all\n",
	      f);
}

static int
want_args(int argc, int n, const char *usage_line)
{
	if (argc != n)
		die("usage: %s", usage_line);
	return 1;
}

int
main(int argc, char **argv)
{
	const char *cmd;
	char *end;
	long n;

	if (argc < 2) {
		usage(stderr);
		return 1;
	}
	cmd = argv[1];

	if (!strcmp(cmd, "help") || !strcmp(cmd, "-h") || !strcmp(cmd, "--help")) {
		usage(stdout);
		return 0;
	}
	if (!strcmp(cmd, "set-workspace") && want_args(argc, 3, "ofwcctl set-workspace <number>")) {
		set_workspace(argv[2]);
		return 0;
	}
	if (!strcmp(cmd, "set-kblayout") && want_args(argc, 2, "ofwcctl set-kblayout")) {
		set_kblayout();
		return 0;
	}
	if (!strcmp(cmd, "watch-kblayout") && want_args(argc, 2, "ofwcctl watch-kblayout"))
		watch(MODE_KBLAYOUT, 0);
	if (!strcmp(cmd, "watch-monitor") && want_args(argc, 2, "ofwcctl watch-monitor"))
		watch(MODE_MONITOR, 0);
	if (!strcmp(cmd, "watch-focus") && want_args(argc, 2, "ofwcctl watch-focus"))
		watch(MODE_FOCUS, 0);
	if (!strcmp(cmd, "watch-all") && want_args(argc, 2, "ofwcctl watch-all"))
		watch(MODE_ALL, 0);
	if (!strcmp(cmd, "watch-workspace") &&
	    want_args(argc, 3, "ofwcctl watch-workspace <number>")) {
		errno = 0;
		n = strtol(argv[2], &end, 10);
		if (errno || end == argv[2] || *end || n < 1 || n > MAXTAGS)
			die("workspace '%s' does not exist", argv[2]);
		watch(MODE_WORKSPACE, (uint32_t)n);
	}

	die("unknown command '%s' (try 'ofwcctl help')", cmd);
	return 1;
}
