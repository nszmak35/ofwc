/* ofwc - Only Fibonacci Wayland Compositor, built on neuswc (single translation unit). */
#include <errno.h>
#include <fnmatch.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <libinput.h>
#include <swc.h>
#include <wayland-server.h>
#include <xkbcommon/xkbcommon.h>

#include "default_config.h"
#include "parser/parserconf.h"
#include "util.h"

#ifndef VERSION
#define VERSION "0.1"
#endif

typedef struct { int x, y, w, h; } Box;
typedef struct Client Client;
typedef struct Monitor Monitor;
typedef struct DwindleNode DwindleNode;

#include "config/config_util.h"

struct DwindleNode {
	DwindleNode *parent;
	DwindleNode *child[2];
	Client *client;
	int is_node;
	int split_top;
	double ratio;
	Box box;
};

struct Client {
	struct wl_list link;
	struct wl_list flink;
	struct swc_window *win;
	Monitor *mon;
	uint32_t tags;
	int floating;
	int fullscreen;
	int shown;
	int rules_done;
	Box geom;
	Box fgeom;
	int fplaced;
	/* Dwindle slot before floating: neighbour (only compared), split side, depth. */
	Client *dw_anchor;
	int dw_has, dw_side, dw_up;
	unsigned bt;
	int settle;
	int closing;
	uint64_t close_at;
	int hold_shown;
	uint64_t hide_at;
	uint32_t pre_w, pre_h;
	int staging;
	uint64_t stage_at;
};

struct Monitor {
	struct wl_list link;
	struct swc_screen *scr;
	Box area;
	uint32_t tagset[2];
	unsigned seltags;
	int gaps;
	int dirty;
	struct {
		DwindleNode *root;
		double mfact;
	} pt[MAXTAGS + 1];
};

typedef struct {
	Client *c;
	int mode;
	int cx, cy;
	Box start;
	Box last;
} Grab;

static struct wl_display *dpy;
static struct wl_event_loop *loop;
static struct wl_list clients, fstack, mons;
static Monitor *selmon;
static Client *sel;
static Config cfg;
static Grab grab;
static const char *cli_config;
static int running = 1;
static uint64_t focus_hold_until;

static uint64_t
now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

#define TAGMASK ((cfg.ntags >= 31) ? 0x7fffffffu : ((1u << cfg.ntags) - 1))
#define VISIBLEON(c, m) \
	((c)->mon == (m) && !(c)->closing && \
	 ((c)->tags & (m)->tagset[(m)->seltags] & TAGMASK))

static void arrange(Monitor *m);
static void focus(Client *c);
static void setfloating(Client *c, int floating);
static void setfullscreen(Client *c, int on);
static void apply_border(Client *c);
static void place_box(Client *c, Box cell);
static Client *client_at_cursor(void);
static void request_reload(void);
static void do_reload(void);
static void config_watch_init(void);
static void config_watch_stop(void);
static void grab_end(int moved);
static void apply_libinput(struct libinput_device *dev);
static void settle_start(Client *c);
static void settle_touch(Client *c);
static void defer_focus(Client *c);
static void focus_early(Client *c);
static void closing_arm(void);
static void stage_arm(void);
static void apply_rootcolor(int startup);
static void ipc_changed(void);
static void ipc_mon_gone(Monitor *m);
static void ipc_toggle_bars(void);

#include "layouts/layouts.h"
#include "client.h"
#include "effects/decorations.h"
#include "output/monitor.h"
#include "xwayland/xwayland.h"
#include "input/pointer.h"
#include "shell/session.h"
#include "input/keyboard.h"
#include "config/config_scalar.h"
#include "config/config_load.h"
#include "actions.h"
#include "config/config_watch.h"
#include "ipc/ipc.h"

static void
on_win_destroy(void *data)
{
	Client *c = data;
	Monitor *m;
	int was_sel;

	if (!c)
		return;
	if (grab.c == c)
		grab_end(0);
	if (pending_focus == c->win)
		pending_focus = NULL;
	dw_forget(c);
	wl_list_remove(&c->link);
	wl_list_remove(&c->flink);
	was_sel = (sel == c);
	if (was_sel)
		sel = NULL;
	m = c->mon;
	free(c);
	if (was_sel && m)
		focus(focus_top(m));
	if (m)
		arrange(m);
}

static void
on_win_entered(void *data)
{
	Client *c = data;

	if (c && cfg.sloppyfocus && now_ms() >= focus_hold_until)
		defer_focus(c);
}

static void
on_win_app_id(void *data)
{
	Client *c = data;

	if (c && !c->rules_done)
		apply_rules(c);
	ipc_changed();
}

static void
on_win_title(void *data)
{
	Client *c = data;

	if (c && !c->rules_done)
		apply_rules(c);
	ipc_changed();
}

static void
on_win_parent(void *data)
{
	Client *c = data;

	if (c)
		handle_parent(c);
}

extern void swc_window_set_fullscreen_handler(struct swc_window *window,
                                              void (*request)(void *data,
                                                              struct swc_screen *screen,
                                                              bool fullscreen),
                                              void *data) __attribute__((weak));

static void
on_win_fullscreen_request(void *data, struct swc_screen *screen, bool fullscreen)
{
	Client *c = data;

	(void)screen;
	if (c)
		setfullscreen(c, fullscreen ? 1 : 0);
}

extern void swc_window_set_activate_handler(struct swc_window *window,
                                            void (*request)(void *data),
                                            void *data) __attribute__((weak));

static void
on_win_activate_request(void *data)
{
	Client *c = data;
	Monitor *m;

	if (!c || c->closing || !(m = c->mon) || !m->scr)
		return;
	selmon = m;
	wl_list_remove(&c->flink);
	wl_list_insert(&fstack, &c->flink);
	if (!VISIBLEON(c, m)) {
		uint32_t mask = c->tags & TAGMASK;

		if (!mask)
			return;
		view_mask(m, mask);
	}
	if (client_visible(c)) {
		focus_early(c);
		focus(c);
		if (c->floating)
			raise_client(c);
	}
}

static const struct swc_window_handler window_handler = {
	.destroy = on_win_destroy,
	.title_changed = on_win_title,
	.app_id_changed = on_win_app_id,
	.parent_changed = on_win_parent,
	.entered = on_win_entered,
};

static void
new_window(struct swc_window *win)
{
	Client *c, *o;
	Monitor *m = selmon;

	if (!win || !m)
		return;
	c = ecalloc(1, sizeof(*c));
	c->win = win;
	c->mon = m;
	c->tags = m->tagset[m->seltags] & TAGMASK;
	if (!c->tags)
		c->tags = 1;
	win->motion_throttle_ms = 1000 / MAX(1, cfg.motion_hz);
	win->min_width = 20;
	win->min_height = 20;
	win->max_width = 0;
	win->max_height = 0;

	/* Always tiled mode: clients draw no shadows or decorations; ofwc owns the geometry. */
	swc_window_set_tiled(win);
	wl_list_insert(clients.prev, &c->link);
	wl_list_insert(&fstack, &c->flink);
	swc_window_set_handler(win, &window_handler, c);
	if (swc_window_set_fullscreen_handler)
		swc_window_set_fullscreen_handler(win, on_win_fullscreen_request, c);
	if (swc_window_set_activate_handler)
		swc_window_set_activate_handler(win, on_win_activate_request, c);

	c->staging = 1;
	c->stage_at = now_ms();
	stage_arm();

	wl_list_for_each(o, &clients, link)
		if (o != c && o->mon == m && o->fullscreen && VISIBLEON(o, m))
			setfullscreen(o, 0);

	apply_rules(c);
	apply_border(c);
	if (c->floating)
		float_place(c);
	focus_early(c);
	arrange(m);
	focus(c);
	settle_start(c);
	ofwc_dbg("new window %p", (void *)win);
}

static void
new_screen(struct swc_screen *scr)
{
	mon_add(scr);
}

static void
new_device(struct libinput_device *dev)
{
	apply_libinput(dev);
}

static void
on_activate(void)
{
	held = 0;
}

static void
on_deactivate(void)
{
	held = 0;
	grab_end(0);
}

static const struct swc_manager manager = {
	.new_screen = new_screen,
	.new_window = new_window,
	.new_device = new_device,
	.activate = on_activate,
	.deactivate = on_deactivate,
};

static void
usage(const char *name, int rc)
{
	fprintf(rc ? stderr : stdout,
	        "usage: %s [-v] [-d] [-c config.py] [-s command]\n"
	        "  -v  print version\n  -d  debug logging\n"
	        "  -c  use this configuration file\n  -s  run a command at start-up\n",
	        name);
	exit(rc);
}

int
main(int argc, char **argv)
{
	const char *startup = NULL;
	char *upath;
	const char *sock;
	int opt;

	while ((opt = getopt(argc, argv, "vdc:s:h")) != -1) {
		switch (opt) {
		case 'v':
			printf("ofwc %s\n", VERSION);
			return 0;
		case 'd':
			ofwc_debug = 1;
			break;
		case 'c':
			cli_config = optarg;
			break;
		case 's':
			startup = optarg;
			break;
		default:
			usage(argv[0], opt == 'h' ? 0 : 1);
		}
	}

	session_crash_init();
	session_ensure_launcher(argc, argv);

	wl_list_init(&clients);
	wl_list_init(&fstack);
	wl_list_init(&mons);

	upath = user_config_path(cli_config);
	if (!cli_config)
		config_seed_user_file(upath);
	if (config_load(&cfg, upath, 0) < 0) {
		ofwc_log("using built-in defaults");
		if (config_load(&cfg, NULL, 0) < 0)
			die("cannot load the built-in configuration");
	}
	free(upath);

	/* A missing keyboard layout falls back to "us". */
	{
		const char *kv[5];

		if (kb_select(&cfg, kv))
			ofwc_log("keyboard: the layout in the configuration does not work, using \"us\"");
		kb_store(kv);
	}
	swc_repeat_rate = cfg.repeat_rate;
	swc_repeat_delay = cfg.repeat_delay;

	dpy = wl_display_create();
	if (!dpy)
		die("wl_display_create failed");
	loop = wl_display_get_event_loop(dpy);

	if (!swc_initialize(dpy, loop, &manager))
		die("swc_initialize failed (is neuswc built and swc-launch installed?)");

	sock = wl_display_add_socket_auto(dpy);
	if (!sock)
		die("cannot create the Wayland socket");
	setenv("WAYLAND_DISPLAY", sock, 1);
	ofwc_log("WAYLAND_DISPLAY=%s", sock);

	apply_rootcolor(1);
	ofwc_log("neuswc patches seen: 0004 rootcolor=%s, 0005 decor-inset=%s, "
	         "0010 fullscreen-requests=%s, 0012 keyboard-layout=%s, "
	         "0013 screen-from-output=%s",
	         swc_set_background ? "yes" : "no",
	         swc_window_set_decor_inset ? "yes" : "no",
	         swc_window_set_fullscreen_handler ? "yes" : "no",
	         swc_keyboard_get_layout ? "yes" : "no",
	         swc_screen_from_output ? "yes" : "no");
	xwayland_report();
	session_setup_signals();
	pointer_init();
	timers_init();
	rebind();
	ipc_init();
	config_watch_init();
	session_autostart(startup);

	wl_display_run(dpy);

	config_watch_stop();
	ipc_finish();
	session_shutdown();
	return 0;
}
