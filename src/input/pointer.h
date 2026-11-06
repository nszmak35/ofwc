/* Pointer: client under the cursor; move and resize of floating windows. */
#ifndef OFWC_INPUT_POINTER_H
#define OFWC_INPUT_POINTER_H

static struct wl_event_source *grab_timer;

static int
cursor_xy(int *x, int *y)
{
	int32_t fx, fy;

	if (!swc_cursor_position(&fx, &fy))
		return 0;
	*x = wl_fixed_to_int(fx);
	*y = wl_fixed_to_int(fy);
	return 1;
}

static Client *
client_at_cursor(void)
{
	int x, y;

	if (!cursor_xy(&x, &y))
		return NULL;
	return client_from_window(swc_window_at(x, y));
}

static int
grab_tick(void *data)
{
	Client *c = grab.c;
	int x, y;
	Box b;

	(void)data;
	if (!c || !grab.mode)
		return 0;
	if (cursor_xy(&x, &y)) {
		int dx = x - grab.cx, dy = y - grab.cy;

		b = grab.start;
		if (grab.mode == 1) {
			b.x += dx;
			b.y += dy;
		} else {
			b.w = MAX(40, b.w + dx);
			b.h = MAX(40, b.h + dy);
		}
		if (memcmp(&b, &grab.last, sizeof(b)) != 0) {
			struct swc_rectangle r = {b.x, b.y, (uint32_t)b.w, (uint32_t)b.h};

			swc_window_set_geometry(c->win, &r);
			c->geom = b;
			grab.last = b;
		}
	}
	wl_event_source_timer_update(grab_timer, 1000 / MAX(1, cfg.motion_hz));
	return 0;
}

static void
pointer_init(void)
{
	grab_timer = wl_event_loop_add_timer(loop, grab_tick, NULL);
}

static struct wl_event_source *focus_timer;
static struct swc_window *pending_focus;

static int
focus_tick(void *data)
{
	Client *c = client_from_window(pending_focus);

	(void)data;
	pending_focus = NULL;
	if (c && cfg.sloppyfocus && !grab.c && c != sel && client_visible(c)) {
		Client *old = sel;

		focus(c);
		settle_touch(c);
		settle_touch(old);
	}
	return 0;
}

static void
defer_focus(Client *c)
{
	if (!c || !focus_timer || grab.c)
		return;
	pending_focus = c->win;
	wl_event_source_timer_update(focus_timer, 1);
}

/* swc shifts CSD windows by the client offset: re-apply the geometry after mapping. */

static struct wl_event_source *settle_timer;

static int
settle_tick(void *data)
{
	Client *c;
	Monitor *m;
	int again = 0;

	(void)data;
	wl_list_for_each(m, &mons, link)
		m->dirty = 0;
	wl_list_for_each(c, &clients, link) {
		if (c->settle <= 0)
			continue;
		c->settle--;
		if (c->settle > 0)
			again = 1;
		if (!c->mon || c->fullscreen)
			continue;
		if (c->floating) {
			struct swc_rectangle r = {c->geom.x, c->geom.y, (uint32_t)MAX(1, c->geom.w),
			                          (uint32_t)MAX(1, c->geom.h)};

			swc_window_set_geometry(c->win, &r);
		} else {
			c->mon->dirty = 1;
		}
	}
	wl_list_for_each(m, &mons, link)
		if (m->dirty)
			arrange(m);
	if (again)
		wl_event_source_timer_update(settle_timer, 40);
	return 0;
}

static void
settle_start(Client *c)
{
	if (!c || !settle_timer)
		return;
	c->settle = 12;
	wl_event_source_timer_update(settle_timer, 40);
}

static void
settle_touch(Client *c)
{
	if (!c || !settle_timer)
		return;
	if (c->settle < 6)
		c->settle = 6;
	wl_event_source_timer_update(settle_timer, 40);
}

static void
timers_init(void)
{
	focus_timer = wl_event_loop_add_timer(loop, focus_tick, NULL);
	settle_timer = wl_event_loop_add_timer(loop, settle_tick, NULL);
}

static void
grab_begin(int mode)
{
	Client *c = client_at_cursor();
	int x, y;

	if (grab.c || !grab_timer)
		return;
	if (!c)
		c = sel;
	if (!c || c->fullscreen || !c->floating || !client_visible(c))
		return;
	if (!cursor_xy(&x, &y))
		return;
	focus(c);
	raise_client(c);
	grab.c = c;
	grab.mode = mode;
	grab.cx = x;
	grab.cy = y;
	grab.start = grab.last = c->geom;
	wl_event_source_timer_update(grab_timer, 1000 / MAX(1, cfg.motion_hz));
}

static void
grab_end(int moved)
{
	Client *c = grab.c;

	if (!c)
		return;
	if (grab_timer)
		wl_event_source_timer_update(grab_timer, 0);
	grab.c = NULL;
	grab.mode = 0;
	if (!moved)
		return;
	c->fgeom = c->geom;
	c->fplaced = 1;
	{
		Monitor *m = mon_at(c->geom.x + c->geom.w / 2, c->geom.y + c->geom.h / 2);

		if (m && m != c->mon) {
			Monitor *old = c->mon;

			c->mon = m;
			c->tags = m->tagset[m->seltags] & TAGMASK;
			if (!c->tags)
				c->tags = 1;
			c->shown = 0;
			arrange(old);
			arrange(m);
			focus(c);
		}
	}
}

#endif
