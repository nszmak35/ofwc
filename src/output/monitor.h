/* Monitors: arrange, focus, floating, fullscreen. */
#ifndef OFWC_OUTPUT_MONITOR_H
#define OFWC_OUTPUT_MONITOR_H

static void on_screen_destroy(void *data);
static void on_screen_geometry(void *data);
static void on_screen_entered(void *data);

static const struct swc_screen_handler screen_handler = {
	.destroy = on_screen_destroy,
	.geometry_changed = on_screen_geometry,
	.usable_geometry_changed = on_screen_geometry,
	.entered = on_screen_entered,
};

static void
mon_update_area(Monitor *m)
{
	const struct swc_rectangle *g;

	if (!m->scr)
		return;
	g = &m->scr->usable_geometry;
	m->area.x = g->x;
	m->area.y = g->y;
	m->area.w = (int)MIN(g->width, 1u << 20);
	m->area.h = (int)MIN(g->height, 1u << 20);
	if (m->area.w < 1)
		m->area.w = 1;
	if (m->area.h < 1)
		m->area.h = 1;
}

static void
mon_reset_mfact(void)
{
	Monitor *m;
	int idx = 0, i, t;

	wl_list_for_each(m, &mons, link) {
		double f = cfg.mfact;

		for (i = 0; i < cfg.nmonrules; i++)
			if (cfg.monrules[i].mon < 0 || cfg.monrules[i].mon == idx)
				f = MAX(0.1, MIN(cfg.monrules[i].mfact, 0.9));
		for (t = 0; t <= MAXTAGS; t++)
			m->pt[t].mfact = f;
		idx++;
	}
}

static Monitor *
mon_at(int x, int y)
{
	Monitor *m;

	wl_list_for_each(m, &mons, link)
		if (x >= m->area.x && x < m->area.x + m->area.w &&
		    y >= m->area.y && y < m->area.y + m->area.h)
			return m;
	return NULL;
}

static Monitor *
mon_dir(Monitor *m, int dir)
{
	struct wl_list *l = &m->link;
	Monitor *cand;

	do {
		l = dir > 0 ? l->next : l->prev;
		if (l == &mons)
			l = dir > 0 ? mons.next : mons.prev;
		cand = wl_container_of(l, cand, link);
		if (cand->scr || cand == m)
			return cand;
	} while (l != &m->link);
	return m;
}

static void
update_visibility(Monitor *m)
{
	Client *c;

	wl_list_for_each(c, &clients, link) {
		int want;

		if (c->mon != m)
			continue;
		want = VISIBLEON(c, m) && !c->staging ? 1 : 0;
		if (c->closing && c->hold_shown && c->shown)
			want = 1;
		if (want == c->shown)
			continue;
		c->shown = want;
		if (want)
			swc_window_show(c->win);
		else
			swc_window_hide(c->win);
	}
}

static void
arrange(Monitor *m)
{
	Client *c;

	if (!m) {
		Monitor *it;

		wl_list_for_each(it, &mons, link)
			arrange(it);
		return;
	}
	update_visibility(m);
	dwindle_arrange(m);
	wl_list_for_each(c, &clients, link) {
		if (c->mon == m && c->shown && c->fullscreen && m->scr)
			swc_window_set_geometry(c->win, &m->scr->geometry);
	}
	restack_floats();
	ipc_changed();
}

/* Focus before arranging: swc answers focus changes with a configure at the old size. */
static void
focus_early(Client *c)
{
	if (c && client_visible(c))
		swc_window_focus(c->win);
}

static void
focus(Client *c)
{
	Client *old = sel;

	if (c && !client_visible(c))
		c = NULL;
	if (c) {
		selmon = c->mon;
		wl_list_remove(&c->flink);
		wl_list_insert(&fstack, &c->flink);
	}
	sel = c;
	swc_window_focus(c ? c->win : NULL);
	if (old && old != c)
		apply_border(old);
	if (c)
		apply_border(c);
	restack_floats();
	ipc_changed();
}

/* Floating: restore the remembered geometry, else centre at float_width x float_height. */
static void
float_place(Client *c)
{
	Monitor *m = c->mon;
	Box a, t;
	struct swc_rectangle r;
	int recenter = 1;

	if (!m || !c->win)
		return;
	a = m->area;
	if (c->fgeom.w > 0 && c->fgeom.h > 0) {
		t = c->fgeom;
		if (t.w >= a.w - cfg.gappx) {
			t.w = a.w * 9 / 10;
			c->fplaced = 0;
		}
		if (t.h >= a.h - cfg.gappx) {
			t.h = a.h * 9 / 10;
			c->fplaced = 0;
		}
		if (c->fplaced && t.x + t.w / 2 >= a.x && t.x + t.w / 2 < a.x + a.w &&
		    t.y + t.h / 2 >= a.y && t.y + t.h / 2 < a.y + a.h)
			recenter = 0;
	} else {
		t.w = (int)(a.w * cfg.float_w);
		t.h = (int)(a.h * cfg.float_h);
		t.x = t.y = 0;
		c->fplaced = 0;
	}
	t.w = MAX(50, MIN(t.w, a.w));
	t.h = MAX(50, MIN(t.h, a.h));
	if (recenter) {
		t.x = a.x + (a.w - t.w) / 2;
		t.y = a.y + (a.h - t.h) / 2;
	}
	r.x = t.x;
	r.y = t.y;
	r.width = (uint32_t)t.w;
	r.height = (uint32_t)t.h;
	swc_window_set_geometry(c->win, &r);
	c->geom = t;
	c->fgeom = t;
}

static void
setfloating(Client *c, int on)
{
	if (!c || !c->mon || c->fullscreen || c->floating == (on ? 1 : 0))
		return;
	if (on) {
		c->floating = 1;
		float_place(c);
	} else {
		if (c->geom.w > 0 && c->geom.h > 0) {
			c->fgeom = c->geom;
			c->fplaced = 1;
		}
		c->floating = 0;
	}
	apply_border(c);
	arrange(c->mon);
	if (c->floating)
		raise_client(c);
}

static void
setfullscreen(Client *c, int on)
{
	Monitor *m;

	if (!c || !c->mon || c->fullscreen == (on ? 1 : 0))
		return;
	m = c->mon;
	if (on) {
		if (!m->scr)
			return;
		if (grab.c == c)
			grab_end(0);
		if (c->floating && c->geom.w > 0 && c->geom.h > 0) {
			c->fgeom = c->geom;
			c->fplaced = 1;
		}
		c->fullscreen = 1;
		apply_border(c);
		swc_window_set_fullscreen(c->win, m->scr);
		swc_window_set_geometry(c->win, &m->scr->geometry);
		c->geom = m->area;
		raise_client(c);
	} else {
		c->fullscreen = 0;
		swc_window_set_fullscreen(c->win, m->scr);
		apply_border(c);
		if (c->floating) {
			float_place(c);
			settle_start(c);
		}
	}
	arrange(m);
}

static void
handle_parent(Client *c)
{
	Client *p;

	if (!c->win->parent)
		return;
	p = client_from_window(c->win->parent);
	if (p && p->mon && p->mon != c->mon) {
		dw_forget(c);
		c->mon = p->mon;
	}
	if (p)
		c->tags = p->tags;
	c->fgeom = (Box){0, 0, 0, 0};
	c->fplaced = 0;
	if (c->floating) {
		float_place(c);
		arrange(c->mon);
	} else {
		setfloating(c, 1);
	}
}

static void
mon_add(struct swc_screen *scr)
{
	Monitor *m = NULL, *it;
	int t;

	wl_list_for_each(it, &mons, link) {
		if (!it->scr) {
			m = it;
			break;
		}
	}
	if (m) {
		m->scr = scr;
		mon_update_area(m);
	} else {
		m = ecalloc(1, sizeof(*m));
		m->scr = scr;
		m->tagset[0] = m->tagset[1] = 1;
		m->gaps = cfg.gaps;
		for (t = 0; t <= MAXTAGS; t++)
			m->pt[t].mfact = cfg.mfact;
		mon_update_area(m);
		{
			int inserted = 0;

			wl_list_for_each(it, &mons, link) {
				if (it->area.x > m->area.x) {
					wl_list_insert(it->link.prev, &m->link);
					inserted = 1;
					break;
				}
			}
			if (!inserted)
				wl_list_insert(mons.prev, &m->link);
		}
	}
	swc_screen_set_handler(scr, &screen_handler, m);
	if (!selmon)
		selmon = m;
	mon_reset_mfact();
	arrange(NULL);
}

static void
on_screen_destroy(void *data)
{
	Monitor *m = data, *o = NULL, *it;
	Client *c;
	int t;

	if (!m)
		return;
	m->scr = NULL;
	wl_list_for_each(it, &mons, link) {
		if (it != m && it->scr) {
			o = it;
			break;
		}
	}
	if (!o) {
		ipc_changed();
		return;
	}
	if (grab.c && grab.c->mon == m)
		grab_end(0);
	wl_list_for_each(c, &clients, link) {
		if (c->mon == m) {
			dw_forget(c);
			c->mon = o;
			c->tags = o->tagset[o->seltags] & TAGMASK;
			if (!c->tags)
				c->tags = 1;
			c->shown = 0;
		}
	}
	if (selmon == m)
		selmon = o;
	wl_list_remove(&m->link);
	ipc_mon_gone(m);
	for (t = 0; t <= MAXTAGS; t++)
		dw_tree_free(m->pt[t].root);
	free(m);
	mon_reset_mfact();
	if (!sel || !client_visible(sel))
		focus_early(focus_top(selmon));
	arrange(NULL);
	if (!sel || !client_visible(sel))
		focus(focus_top(selmon));
}

static void
on_screen_geometry(void *data)
{
	Monitor *m = data;

	if (!m || !m->scr)
		return;
	mon_update_area(m);
	arrange(m);
}

static void
on_screen_entered(void *data)
{
	Monitor *m = data;

	if (m && m->scr && !grab.c)
		selmon = m;
}

#endif
