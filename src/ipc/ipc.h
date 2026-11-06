/* IPC for bars: dwl-ipc-unstable-v2 and ofwc-ipc-unstable-v1. */
#ifndef OFWC_IPC_IPC_H
#define OFWC_IPC_IPC_H

#include <stdbool.h>

#include "dwl-ipc-unstable-v2-protocol.h"
#include "ofwc-ipc-unstable-v1-protocol.h"

extern struct swc_screen *swc_screen_from_output(struct wl_resource *output)
    __attribute__((weak));
extern uint32_t swc_keyboard_get_layout(void) __attribute__((weak));
extern bool swc_keyboard_set_layout(uint32_t index) __attribute__((weak));
extern void swc_keyboard_set_layout_handler(void (*handler)(void *data, uint32_t index),
                                            void *data) __attribute__((weak));

static const char *const ipc_layout_names[] = {"dwindle", "spiral"};

typedef struct {
	int active, ntags, layout, fullscreen, floating;
	uint32_t state[MAXTAGS], clients[MAXTAGS], focused[MAXTAGS];
	char *title, *appid;
} IpcState;

typedef struct IpcOut {
	struct wl_list link;
	struct wl_resource *res;
	Monitor *mon;
	IpcState last;
	int sent;
} IpcOut;

typedef struct IpcExt {
	struct wl_list link;
	struct wl_resource *res;
	char *layouts;
	uint32_t index;
	int have_index;
} IpcExt;

static struct wl_list ipc_outs, ipc_managers, ipc_exts;
static struct wl_global *ipc_dwl_global, *ipc_ext_global;
static struct wl_event_source *ipc_idle_src;
static int ipc_ready;
static int ipc_sent_ntags;

static Client *
ipc_focused(Monitor *m)
{
	if (sel && sel->mon == m && client_visible(sel))
		return sel;
	return focus_top(m);
}

static char *
ipc_clip(const char *s, size_t max)
{
	size_t n = strlen(s);

	if (n > max) {
		n = max;
		while (n > 0 && ((unsigned char)s[n] & 0xc0) == 0x80)
			n--; /* do not cut a character in two */
	}
	return strndup(s, n);
}

static void
ipc_state_free(IpcState *s)
{
	free(s->title);
	free(s->appid);
	memset(s, 0, sizeof(*s));
}

static void
ipc_collect(Monitor *m, IpcState *s)
{
	const Client *foc = ipc_focused(m);
	const Client *c;
	int t, n = MAX(1, MIN(cfg.ntags, MAXTAGS));

	memset(s, 0, sizeof(*s));
	s->active = m == selmon;
	s->ntags = n;
	for (t = 0; t < n; t++)
		if (m->tagset[m->seltags] & (1u << t))
			s->state[t] = ZDWL_IPC_OUTPUT_V2_TAG_STATE_ACTIVE;
	wl_list_for_each(c, &clients, link) {
		if (c->mon != m || c->closing)
			continue;
		for (t = 0; t < n; t++) {
			if (!(c->tags & (1u << t)))
				continue;
			s->clients[t]++;
			if (c == foc)
				s->focused[t] = 1;
		}
		if (c->fullscreen && c->shown)
			s->fullscreen = 1;
	}
	s->title = ipc_clip(foc && foc->win && foc->win->title ? foc->win->title : "", 1024);
	s->appid = ipc_clip(foc && foc->win && foc->win->app_id ? foc->win->app_id : "", 256);
	if (!s->title || !s->appid)
		die("out of memory");
	s->layout = cfg.dw_spiral ? 1 : 0;
	s->floating = foc && foc->floating;
}

static int
ipc_state_equal(const IpcState *a, const IpcState *b)
{
	return a->active == b->active && a->ntags == b->ntags && a->layout == b->layout &&
	       a->fullscreen == b->fullscreen && a->floating == b->floating &&
	       !memcmp(a->state, b->state, sizeof(a->state)) &&
	       !memcmp(a->clients, b->clients, sizeof(a->clients)) &&
	       !memcmp(a->focused, b->focused, sizeof(a->focused)) &&
	       !strcmp(a->title, b->title) && !strcmp(a->appid, b->appid);
}

static void
ipc_send(IpcOut *o, const IpcState *s)
{
	struct wl_resource *r = o->res;
	uint32_t ver = wl_resource_get_version(r);
	int t;

	zdwl_ipc_output_v2_send_active(r, (uint32_t)s->active);
	for (t = 0; t < s->ntags; t++)
		zdwl_ipc_output_v2_send_tag(r, (uint32_t)t, s->state[t], s->clients[t], s->focused[t]);
	zdwl_ipc_output_v2_send_layout(r, (uint32_t)s->layout);
	zdwl_ipc_output_v2_send_title(r, s->title);
	zdwl_ipc_output_v2_send_appid(r, s->appid);
	zdwl_ipc_output_v2_send_layout_symbol(r, ipc_layout_names[s->layout]);
	if (ver >= ZDWL_IPC_OUTPUT_V2_FULLSCREEN_SINCE_VERSION)
		zdwl_ipc_output_v2_send_fullscreen(r, (uint32_t)s->fullscreen);
	if (ver >= ZDWL_IPC_OUTPUT_V2_FLOATING_SINCE_VERSION)
		zdwl_ipc_output_v2_send_floating(r, (uint32_t)s->floating);
	zdwl_ipc_output_v2_send_frame(r);
}

static void
ipc_update_out(IpcOut *o, int force)
{
	IpcState now;

	if (!o->mon || !o->mon->scr)
		return;
	ipc_collect(o->mon, &now);
	if (!force && o->sent && ipc_state_equal(&o->last, &now)) {
		ipc_state_free(&now);
		return;
	}
	ipc_send(o, &now);
	ipc_state_free(&o->last);
	o->last = now;
	o->sent = 1;
}

static const char *
ipc_kb_layouts(void)
{
	const char *l = kb_sel[2];

	if (!l || !*l)
		l = getenv("XKB_DEFAULT_LAYOUT");
	return (l && *l) ? l : "us";
}

static uint32_t
ipc_kb_index(void)
{
	return swc_keyboard_get_layout ? swc_keyboard_get_layout() : 0;
}

static void
ipc_update_ext(IpcExt *x)
{
	const char *l = ipc_kb_layouts();
	uint32_t i = ipc_kb_index();

	if (!x->layouts || strcmp(x->layouts, l)) {
		zofwc_ipc_v1_send_keyboard_layouts(x->res, l);
		free(x->layouts);
		x->layouts = estrdup(l);
		x->have_index = 0;
	}
	if (!x->have_index || x->index != i) {
		zofwc_ipc_v1_send_keyboard_layout(x->res, i);
		x->index = i;
		x->have_index = 1;
	}
}

static void
ipc_flush(void *data)
{
	IpcOut *o, *ot;
	IpcExt *x;
	struct wl_resource *r;

	(void)data;
	ipc_idle_src = NULL;
	if (!ipc_ready)
		return;
	if (cfg.ntags != ipc_sent_ntags) {
		ipc_sent_ntags = cfg.ntags;
		wl_resource_for_each(r, &ipc_managers)
			zdwl_ipc_manager_v2_send_tags(r, (uint32_t)MAX(1, MIN(cfg.ntags, MAXTAGS)));
	}
	wl_list_for_each_safe(o, ot, &ipc_outs, link)
		ipc_update_out(o, 0);
	wl_list_for_each(x, &ipc_exts, link)
		ipc_update_ext(x);
}

static void
ipc_changed(void)
{
	if (!ipc_ready || ipc_idle_src || !loop)
		return;
	ipc_idle_src = wl_event_loop_add_idle(loop, ipc_flush, NULL);
}

static void
ipc_on_layout(void *data, uint32_t index)
{
	(void)data;
	(void)index;
	ipc_changed();
}

/* The monitor is about to be freed: its outputs go quiet. */
static void
ipc_mon_gone(Monitor *m)
{
	IpcOut *o;

	if (!ipc_ready)
		return;
	wl_list_for_each(o, &ipc_outs, link)
		if (o->mon == m)
			o->mon = NULL;
}

static void
ipc_toggle_bars(void)
{
	IpcOut *o;

	if (!ipc_ready)
		return;
	wl_list_for_each(o, &ipc_outs, link)
		if (o->mon && o->mon->scr)
			zdwl_ipc_output_v2_send_toggle_visibility(o->res);
}

static Monitor *
ipc_mon_of_output(struct wl_resource *output)
{
	struct swc_screen *scr;
	Monitor *m;

	if (!output || !swc_screen_from_output || !(scr = swc_screen_from_output(output)))
		return NULL;
	wl_list_for_each(m, &mons, link)
		if (m->scr == scr)
			return m;
	return NULL;
}

static void
ipc_output_release(struct wl_client *client, struct wl_resource *resource)
{
	(void)client;
	wl_resource_destroy(resource);
}

static void
ipc_output_destroy(struct wl_resource *resource)
{
	IpcOut *o = wl_resource_get_user_data(resource);

	if (!o)
		return;
	wl_list_remove(&o->link);
	ipc_state_free(&o->last);
	free(o);
}

static void
ipc_output_set_tags(struct wl_client *client, struct wl_resource *resource,
                    uint32_t tagmask, uint32_t toggle_tagset)
{
	IpcOut *o = wl_resource_get_user_data(resource);
	Monitor *m;
	uint32_t n = tagmask & TAGMASK;

	(void)client;
	if (!o || !(m = o->mon) || !m->scr || !n || n == m->tagset[m->seltags])
		return;
	selmon = m;
	if (toggle_tagset)
		m->seltags ^= 1;
	m->tagset[m->seltags] = n;
	focus_early(focus_top(m));
	arrange(m);
	focus(focus_top(m));
}

static void
ipc_output_set_client_tags(struct wl_client *client, struct wl_resource *resource,
                           uint32_t and_tags, uint32_t xor_tags)
{
	IpcOut *o = wl_resource_get_user_data(resource);
	Monitor *m;
	Client *c;
	uint32_t n;

	(void)client;
	if (!o || !(m = o->mon) || !m->scr || !(c = ipc_focused(m)))
		return;
	n = ((c->tags & and_tags) ^ xor_tags) & TAGMASK;
	if (!n)
		return;
	selmon = m;
	c->tags = n;
	focus_early(focus_top(m));
	arrange(m);
	focus(focus_top(m));
}

static void
ipc_output_set_layout(struct wl_client *client, struct wl_resource *resource, uint32_t index)
{
	(void)client;
	(void)resource;
	(void)index;
}

static const struct zdwl_ipc_output_v2_interface ipc_output_impl = {
	.release = ipc_output_release,
	.set_tags = ipc_output_set_tags,
	.set_client_tags = ipc_output_set_client_tags,
	.set_layout = ipc_output_set_layout,
};

static void
ipc_manager_release(struct wl_client *client, struct wl_resource *resource)
{
	(void)client;
	wl_resource_destroy(resource);
}

static void
ipc_manager_get_output(struct wl_client *client, struct wl_resource *manager, uint32_t id,
                       struct wl_resource *output)
{
	struct wl_resource *res;
	IpcOut *o;

	res = wl_resource_create(client, &zdwl_ipc_output_v2_interface,
	                         wl_resource_get_version(manager), id);
	if (!res) {
		wl_client_post_no_memory(client);
		return;
	}
	o = ecalloc(1, sizeof(*o));
	o->res = res;
	o->mon = ipc_mon_of_output(output);
	wl_resource_set_implementation(res, &ipc_output_impl, o, ipc_output_destroy);
	wl_list_insert(&ipc_outs, &o->link);
	ipc_update_out(o, 1);
}

static const struct zdwl_ipc_manager_v2_interface ipc_manager_impl = {
	.release = ipc_manager_release,
	.get_output = ipc_manager_get_output,
};

static void
ipc_manager_destroy(struct wl_resource *resource)
{
	wl_list_remove(wl_resource_get_link(resource));
}

static void
ipc_manager_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
	struct wl_resource *res;
	size_t i;

	(void)data;
	res = wl_resource_create(client, &zdwl_ipc_manager_v2_interface, version, id);
	if (!res) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(res, &ipc_manager_impl, NULL, ipc_manager_destroy);
	wl_list_insert(&ipc_managers, wl_resource_get_link(res));
	zdwl_ipc_manager_v2_send_tags(res, (uint32_t)MAX(1, MIN(cfg.ntags, MAXTAGS)));
	for (i = 0; i < LENGTH(ipc_layout_names); i++)
		zdwl_ipc_manager_v2_send_layout(res, ipc_layout_names[i]);
}

static void
ipc_ext_destroy_request(struct wl_client *client, struct wl_resource *resource)
{
	(void)client;
	wl_resource_destroy(resource);
}

static void
ipc_ext_set_keyboard_layout(struct wl_client *client, struct wl_resource *resource,
                            uint32_t index)
{
	(void)client;
	(void)resource;
	if (swc_keyboard_set_layout)
		swc_keyboard_set_layout(index);
}

static void
ipc_ext_action(struct wl_client *client, struct wl_resource *resource, const char *name,
               const char *argument, struct wl_resource *output)
{
	const Action *a;
	Monitor *m;
	Arg ar;

	(void)client;
	(void)resource;
	if (!name || !*name)
		return;
	if (!strcasecmp(name, "spawn") || !strcasecmp(name, "moveresize")) {
		ofwc_log("ipc: the action '%s' is not available to clients", name);
		return;
	}
	if (!(a = find_action(name))) {
		ofwc_log("ipc: unknown action '%s'", name);
		return;
	}
	if ((m = ipc_mon_of_output(output)) && m->scr)
		selmon = m;
	fill_arg(&ar, argument && *argument ? argument : NULL);
	a->fn(&ar, 1);
	free(ar.s);
}

static const struct zofwc_ipc_v1_interface ipc_ext_impl = {
	.destroy = ipc_ext_destroy_request,
	.set_keyboard_layout = ipc_ext_set_keyboard_layout,
	.action = ipc_ext_action,
};

static void
ipc_ext_destroy(struct wl_resource *resource)
{
	IpcExt *x = wl_resource_get_user_data(resource);

	if (!x)
		return;
	wl_list_remove(&x->link);
	free(x->layouts);
	free(x);
}

static void
ipc_ext_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
	struct wl_resource *res;
	IpcExt *x;

	(void)data;
	res = wl_resource_create(client, &zofwc_ipc_v1_interface, version, id);
	if (!res) {
		wl_client_post_no_memory(client);
		return;
	}
	x = ecalloc(1, sizeof(*x));
	x->res = res;
	wl_resource_set_implementation(res, &ipc_ext_impl, x, ipc_ext_destroy);
	wl_list_insert(&ipc_exts, &x->link);
	ipc_update_ext(x);
}

static void
ipc_init(void)
{
	if (!swc_screen_from_output) {
		ofwc_log("ipc: neuswc has no swc_screen_from_output (patch 0013), "
		         "bars (nibar) can not work");
		return;
	}
	wl_list_init(&ipc_outs);
	wl_list_init(&ipc_managers);
	wl_list_init(&ipc_exts);
	ipc_dwl_global = wl_global_create(dpy, &zdwl_ipc_manager_v2_interface, 2, NULL,
	                                  ipc_manager_bind);
	ipc_ext_global = wl_global_create(dpy, &zofwc_ipc_v1_interface, 1, NULL, ipc_ext_bind);
	if (!ipc_dwl_global) {
		ofwc_log("ipc: cannot create the dwl-ipc global");
		return;
	}
	if (!ipc_ext_global)
		ofwc_log("ipc: cannot create the ofwc-ipc global");
	if (swc_keyboard_set_layout_handler)
		swc_keyboard_set_layout_handler(ipc_on_layout, NULL);
	ipc_sent_ntags = cfg.ntags;
	ipc_ready = 1;
}

static void
ipc_finish(void)
{
	if (!ipc_ready)
		return;
	ipc_ready = 0;
	if (swc_keyboard_set_layout_handler)
		swc_keyboard_set_layout_handler(NULL, NULL);
	if (ipc_idle_src) {
		wl_event_source_remove(ipc_idle_src);
		ipc_idle_src = NULL;
	}
	if (ipc_dwl_global)
		wl_global_destroy(ipc_dwl_global);
	if (ipc_ext_global)
		wl_global_destroy(ipc_ext_global);
	ipc_dwl_global = ipc_ext_global = NULL;
}

#endif
