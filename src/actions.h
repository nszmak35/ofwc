/* Key, button and scroll actions; config reload. */
#ifndef OFWC_ACTIONS_H
#define OFWC_ACTIONS_H

static void
warp_to(const Box *b)
{
	swc_cursor_set_position(b->x + b->w / 2, b->y + b->h / 2);
}

static void
act_spawn(const Arg *a, int pressed)
{
	(void)pressed;
	if (a->s)
		spawn_cmd(a->s);
}

static void
act_quit(const Arg *a, int pressed)
{
	(void)a;
	(void)pressed;
	running = 0;
	wl_display_terminate(dpy);
}

static void
act_killclient(const Arg *a, int pressed)
{
	Client *c = sel, *t;
	Monitor *m;
	struct swc_rectangle g;

	(void)a;
	(void)pressed;
	if (!c)
		return;
	swc_window_close(c->win);
	if (c->closing || !c->mon || c->fullscreen || c->staging)
		return;
	m = c->mon;
	if (grab.c == c)
		grab_end(0);
	wl_list_for_each(t, &clients, link) {
		t->pre_w = t->pre_h = 0;
		if (t != c && dw_tiled(t, m) && swc_window_get_geometry(t->win, &g)) {
			t->pre_w = g.width;
			t->pre_h = g.height;
		}
	}
	c->closing = 1;
	c->hold_shown = 1;
	c->hide_at = now_ms() + 60;
	c->close_at = now_ms() + 3000;
	closing_arm();
	stage_arm();
	focus_early(focus_top(m));
	arrange(m);
	focus(focus_top(m));
}

static void
act_togglefloating(const Arg *a, int pressed)
{
	(void)a;
	(void)pressed;
	if (sel && !sel->fullscreen)
		setfloating(sel, !sel->floating);
}

static void
act_togglefullscreen(const Arg *a, int pressed)
{
	(void)a;
	(void)pressed;
	if (sel)
		setfullscreen(sel, !sel->fullscreen);
}

static void
act_togglegaps(const Arg *a, int pressed)
{
	(void)a;
	(void)pressed;
	if (selmon) {
		selmon->gaps = !selmon->gaps;
		arrange(selmon);
	}
}

static void
act_focusstack(const Arg *a, int pressed)
{
	struct wl_list *l;
	Client *c;
	int dir = a->i < 0 ? -1 : 1, guard;

	(void)pressed;
	if (!selmon)
		return;
	if (!sel) {
		focus(focus_top(selmon));
		return;
	}
	l = &sel->link;
	for (guard = client_count() + 1; guard > 0; guard--) {
		l = dir > 0 ? l->next : l->prev;
		if (l == &clients)
			continue;
		c = wl_container_of(l, c, link);
		if (c == sel)
			return;
		if (VISIBLEON(c, selmon)) {
			focus(c);
			return;
		}
	}
}

static Client *
client_in_dir(int dir, int tiled_only)
{
	Client *c, *best = NULL;
	long bestscore = LONG_MAX;
	int cx, cy;

	if (!sel || !selmon || sel->geom.w <= 0)
		return NULL;
	cx = sel->geom.x + sel->geom.w / 2;
	cy = sel->geom.y + sel->geom.h / 2;
	wl_list_for_each(c, &clients, link) {
		long dx, dy, primary, secondary, score;

		if (c == sel || !VISIBLEON(c, selmon) || c->geom.w <= 0)
			continue;
		if (tiled_only && (c->floating || c->fullscreen))
			continue;
		dx = (c->geom.x + c->geom.w / 2) - cx;
		dy = (c->geom.y + c->geom.h / 2) - cy;
		switch (dir) {
		case 0: primary = -dx; secondary = labs(dy); break;
		case 1: primary = dx;  secondary = labs(dy); break;
		case 2: primary = -dy; secondary = labs(dx); break;
		default: primary = dy; secondary = labs(dx); break;
		}
		if (primary <= 0)
			continue;
		score = primary + 2 * secondary;
		if (score < bestscore) {
			bestscore = score;
			best = c;
		}
	}
	return best;
}

static void
act_focusdir(const Arg *a, int pressed)
{
	Client *t;

	(void)pressed;
	if (a->i < 0 || a->i > 3 || !(t = client_in_dir((int)a->i, 0)))
		return;
	focus(t);
	warp_to(&t->geom);
}

static void
act_swapdir(const Arg *a, int pressed)
{
	Client *t;

	(void)pressed;
	if (!sel || sel->floating || sel->fullscreen || a->i < 0 || a->i > 3)
		return;
	if (!(t = client_in_dir((int)a->i, 1)))
		return;
	if (dw_swap(selmon, sel, t) < 0)
		return;
	/* The pointer is warped before the views move: sloppy focus must not steal the focus meanwhile. */
	focus_hold_until = now_ms() + 300;
	pending_focus = NULL;
	arrange(selmon);
	warp_to(&sel->geom);
	focus(sel);
	settle_touch(sel);
	settle_touch(t);
}

static void
act_zoom(const Arg *a, int pressed)
{
	(void)a;
	(void)pressed;
	if (!sel || sel->floating || sel->fullscreen || !selmon)
		return;
	dw_zoom(selmon, sel);
	arrange(selmon);
}

static void
act_setmfact(const Arg *a, int pressed)
{
	double *f;

	(void)pressed;
	if (!selmon)
		return;
	f = &selmon->pt[pt_index(selmon)].mfact;
	*f = MAX(0.1, MIN(*f + a->f, 0.9));
	arrange(selmon);
}

static void
view_mask(Monitor *m, uint32_t mask)
{
	mask &= TAGMASK;
	if (mask && mask == m->tagset[m->seltags])
		return;
	m->seltags ^= 1;
	if (mask)
		m->tagset[m->seltags] = mask;
	focus_early(focus_top(m));
	arrange(m);
	focus(focus_top(m));
}

static void
act_view(const Arg *a, int pressed)
{
	(void)pressed;
	if (selmon)
		view_mask(selmon, (uint32_t)a->i);
}

static void
act_toggleview(const Arg *a, int pressed)
{
	uint32_t n;

	(void)pressed;
	if (!selmon)
		return;
	n = (selmon->tagset[selmon->seltags] ^ (uint32_t)a->i) & TAGMASK;
	if (!n)
		return;
	selmon->tagset[selmon->seltags] = n;
	focus_early(focus_top(selmon));
	arrange(selmon);
	focus(focus_top(selmon));
}

static void
act_tag(const Arg *a, int pressed)
{
	uint32_t n = (uint32_t)a->i & TAGMASK;

	(void)pressed;
	if (!sel || !n)
		return;
	sel->tags = n;
	focus_early(focus_top(selmon));
	arrange(sel->mon);
	focus(focus_top(selmon));
}

static void
act_toggletag(const Arg *a, int pressed)
{
	uint32_t n;

	(void)pressed;
	if (!sel)
		return;
	n = (sel->tags ^ (uint32_t)a->i) & TAGMASK;
	if (!n)
		return;
	sel->tags = n;
	focus_early(focus_top(selmon));
	arrange(sel->mon);
	focus(focus_top(selmon));
}

static void
act_cycletag(const Arg *a, int pressed)
{
	uint32_t cur;
	int idx, n = cfg.ntags, dir = a->i < 0 ? -1 : 1;

	(void)pressed;
	if (!selmon || n < 1)
		return;
	cur = selmon->tagset[selmon->seltags] & TAGMASK;
	idx = cur ? __builtin_ctz(cur) : 0;
	idx = ((idx + dir) % n + n) % n;
	view_mask(selmon, 1u << idx);
}

static void
act_focusmon(const Arg *a, int pressed)
{
	Monitor *o;
	Client *c;

	(void)pressed;
	if (!selmon || !(o = mon_dir(selmon, a->i < 0 ? -1 : 1)) || o == selmon || !o->scr)
		return;
	selmon = o;
	c = focus_top(o);
	focus(c);
	if (c) {
		warp_to(&c->geom);
	} else {
		Box b = o->area;

		warp_to(&b);
	}
}

static void
act_tagmon(const Arg *a, int pressed)
{
	Monitor *o, *old;

	(void)pressed;
	if (!sel || !selmon || !(o = mon_dir(selmon, a->i < 0 ? -1 : 1)) ||
	    o == sel->mon || !o->scr)
		return;
	old = sel->mon;
	dw_forget(sel);
	sel->mon = o;
	sel->tags = o->tagset[o->seltags] & TAGMASK;
	if (!sel->tags)
		sel->tags = 1;
	sel->fplaced = 0;
	if (sel->floating)
		float_place(sel);
	focus_early(sel);
	arrange(old);
	arrange(o);
	focus(sel);
}

static void
act_moveresize(const Arg *a, int pressed)
{
	if (pressed) {
		int resize = a->i == 3 || (a->s && !strcasecmp(a->s, "resize"));

		grab_begin(resize ? 2 : 1);
	} else {
		grab_end(1);
	}
}

static void
act_noop(const Arg *a, int pressed)
{
	(void)a;
	(void)pressed;
}

static void
act_togglebar(const Arg *a, int pressed)
{
	(void)a;
	(void)pressed;
	ipc_toggle_bars();
}

static const Action actions[] = {
	{"spawn", act_spawn, 0},
	{"quit", act_quit, 0},
	{"killclient", act_killclient, 0},
	{"togglefloating", act_togglefloating, 0},
	{"togglefullscreen", act_togglefullscreen, 0},
	{"togglegaps", act_togglegaps, 0},
	{"togglebar", act_togglebar, 0},
	{"focusstack", act_focusstack, 0},
	{"focusdir", act_focusdir, 0},
	{"swapdir", act_swapdir, 0},
	{"zoom", act_zoom, 0},
	{"setmfact", act_setmfact, 0},
	{"view", act_view, 0},
	{"toggleview", act_toggleview, 0},
	{"tag", act_tag, 0},
	{"toggletag", act_toggletag, 0},
	{"cycletag", act_cycletag, 0},
	{"focusmon", act_focusmon, 0},
	{"tagmon", act_tagmon, 0},
	{"moveresize", act_moveresize, 1},
	{"setlayout", act_noop, 0},
	{"cyclelayout", act_noop, 0},
	{"incnmaster", act_noop, 0},
};

static const Action *
find_action(const char *name)
{
	size_t i;

	for (i = 0; i < LENGTH(actions); i++)
		if (!strcasecmp(actions[i].name, name))
			return &actions[i];
	return NULL;
}

extern void swc_set_background(uint32_t color) __attribute__((weak));

static void
apply_rootcolor(int startup)
{
	if (swc_set_background)
		swc_set_background(cfg.rootcolor);
	else if (startup && cfg.rootcolor != 0xff000000u)
		ofwc_log("rootcolor: neuswc has no swc_set_background (patch 0004), "
		         "the background stays black");
}

static struct wl_event_source *closing_timer;

static int
closing_tick(void *data)
{
	Client *c;
	uint64_t now = now_ms();
	int restored = 0;

	(void)data;
	wl_list_for_each(c, &clients, link) {
		if (c->closing && now >= c->close_at) {
			c->closing = 0;
			c->hold_shown = 0;
			restored = 1;
		}
	}
	if (restored) {
		Monitor *m;

		wl_list_for_each(m, &mons, link)
			arrange(m);
		if (!sel || !client_visible(sel))
			focus(focus_top(selmon));
	}
	closing_arm();
	return 0;
}

static void
closing_arm(void)
{
	Client *c;
	uint64_t now = now_ms(), next = 0;

	wl_list_for_each(c, &clients, link)
		if (c->closing && (!next || c->close_at < next))
			next = c->close_at;
	if (!next)
		return;
	if (!closing_timer)
		closing_timer = wl_event_loop_add_timer(loop, closing_tick, NULL);
	if (closing_timer)
		wl_event_source_timer_update(closing_timer, next > now ? (int)(next - now) + 1 : 1);
}

static struct wl_event_source *stage_timer;

static int
neighbours_answered(Monitor *m)
{
	Client *t;
	struct swc_rectangle g;

	wl_list_for_each(t, &clients, link) {
		if (!t->shown || !dw_tiled(t, m) || !swc_window_get_geometry(t->win, &g))
			continue;
		if (g.width == (uint32_t)t->geom.w && g.height == (uint32_t)t->geom.h)
			continue;
		if (t->pre_w && (g.width != t->pre_w || g.height != t->pre_h))
			continue;
		return 0;
	}
	return 1;
}

static int
stage_tick(void *data)
{
	Client *c;
	uint64_t now = now_ms();
	int pending = 0;

	(void)data;
	wl_list_for_each(c, &clients, link) {
		if (c->staging) {
			struct swc_rectangle g;

			if ((swc_window_get_geometry(c->win, &g) && g.width > 0 && g.height > 0) ||
			    now >= c->stage_at + 3000) {
				c->staging = 0;
				if (c->mon)
					arrange(c->mon);
				settle_start(c);
			} else {
				pending = 1;
			}
		}
		if (c->closing && c->hold_shown) {
			if (!c->mon || now >= c->hide_at || neighbours_answered(c->mon)) {
				c->hold_shown = 0;
				if (c->mon)
					arrange(c->mon);
			} else {
				pending = 1;
			}
		}
	}
	if (pending)
		wl_event_source_timer_update(stage_timer, 4);
	return 0;
}

static void
stage_arm(void)
{
	if (!stage_timer)
		stage_timer = wl_event_loop_add_timer(loop, stage_tick, NULL);
	if (stage_timer)
		wl_event_source_timer_update(stage_timer, 4);
}

static void
apply_config(Config *n)
{
	Config old = cfg;
	Monitor *m;
	Client *c;
	uint32_t mask;

	cfg = *n;
	apply_rootcolor(0);
	swc_repeat_rate = cfg.repeat_rate;
	swc_repeat_delay = cfg.repeat_delay;
	mask = TAGMASK;
	wl_list_for_each(m, &mons, link) {
		m->tagset[0] &= mask;
		m->tagset[1] &= mask;
		if (!m->tagset[0])
			m->tagset[0] = 1;
		if (!m->tagset[1])
			m->tagset[1] = 1;
		m->gaps = cfg.gaps;
	}
	wl_list_for_each(c, &clients, link) {
		c->tags &= mask;
		if (!c->tags)
			c->tags = 1;
	}
	mon_reset_mfact();
	if (strcmp(old.kb_rules, cfg.kb_rules) || strcmp(old.kb_model, cfg.kb_model) ||
	    strcmp(old.kb_layout, cfg.kb_layout) || strcmp(old.kb_variant, cfg.kb_variant) ||
	    strcmp(old.kb_options, cfg.kb_options))
		kb_apply();
	rebind();
	/* The flavours do not share state: reset the trees when the flavour changes. */
	if (old.dw_mouse != cfg.dw_mouse) {
		int t;

		wl_list_for_each(m, &mons, link)
			for (t = 0; t <= MAXTAGS; t++) {
				dw_tree_free(m->pt[t].root);
				m->pt[t].root = NULL;
			}
	}
	wl_list_for_each(c, &clients, link) {
		c->win->motion_throttle_ms = 1000 / MAX(1, cfg.motion_hz);
		apply_border(c);
	}
	arrange(NULL);
	ipc_changed();
	config_free(&old);
}

static void
do_reload(void)
{
	Config n;
	char *path = user_config_path(cli_config);

	if (config_load(&n, path, 1) < 0) {
		ofwc_log("error in the configuration, not reloaded: keeping the current one");
	} else {
		apply_config(&n);
		ofwc_log("configuration reloaded");
	}
	free(path);
}

static void
apply_libinput(struct libinput_device *d)
{
	if (!d)
		return;
	if (libinput_device_config_tap_get_finger_count(d) > 0) {
		libinput_device_config_tap_set_enabled(d, cfg.tap ? LIBINPUT_CONFIG_TAP_ENABLED
		                                                  : LIBINPUT_CONFIG_TAP_DISABLED);
		libinput_device_config_tap_set_drag_enabled(
		    d, cfg.tap_drag ? LIBINPUT_CONFIG_DRAG_ENABLED : LIBINPUT_CONFIG_DRAG_DISABLED);
		libinput_device_config_tap_set_drag_lock_enabled(
		    d, cfg.drag_lock ? LIBINPUT_CONFIG_DRAG_LOCK_ENABLED
		                     : LIBINPUT_CONFIG_DRAG_LOCK_DISABLED);
		if (cfg.button_map >= 0)
			libinput_device_config_tap_set_button_map(d, (enum libinput_config_tap_button_map)cfg.button_map);
	}
	if (libinput_device_config_scroll_has_natural_scroll(d))
		libinput_device_config_scroll_set_natural_scroll_enabled(d, cfg.natural);
	if (libinput_device_config_dwt_is_available(d))
		libinput_device_config_dwt_set_enabled(
		    d, cfg.dwt ? LIBINPUT_CONFIG_DWT_ENABLED : LIBINPUT_CONFIG_DWT_DISABLED);
	if (libinput_device_config_left_handed_is_available(d))
		libinput_device_config_left_handed_set(d, cfg.left_handed);
	if (libinput_device_config_middle_emulation_is_available(d))
		libinput_device_config_middle_emulation_set_enabled(
		    d, cfg.middle_emu ? LIBINPUT_CONFIG_MIDDLE_EMULATION_ENABLED
		                      : LIBINPUT_CONFIG_MIDDLE_EMULATION_DISABLED);
	if (cfg.scroll_method >= 0 &&
	    (cfg.scroll_method == 0 || (libinput_device_config_scroll_get_methods(d) & (uint32_t)cfg.scroll_method)))
		libinput_device_config_scroll_set_method(d, (enum libinput_config_scroll_method)cfg.scroll_method);
	if (cfg.click_method >= 0 &&
	    (cfg.click_method == 0 || (libinput_device_config_click_get_methods(d) & (uint32_t)cfg.click_method)))
		libinput_device_config_click_set_method(d, (enum libinput_config_click_method)cfg.click_method);
	if (cfg.send_events >= 0 &&
	    (cfg.send_events == 0 || (libinput_device_config_send_events_get_modes(d) & (uint32_t)cfg.send_events)))
		libinput_device_config_send_events_set_mode(d, (uint32_t)cfg.send_events);
	if (libinput_device_config_accel_is_available(d)) {
		libinput_device_config_accel_set_speed(d, cfg.accel_speed);
		if (cfg.accel_profile > 0 &&
		    (libinput_device_config_accel_get_profiles(d) & (uint32_t)cfg.accel_profile))
			libinput_device_config_accel_set_profile(d, (enum libinput_config_accel_profile)cfg.accel_profile);
	}
}

#endif
