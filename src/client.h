/* Clients: lookup, focus stack, stacking, rules. */
#ifndef OFWC_CLIENT_H
#define OFWC_CLIENT_H

static Client *
client_from_window(const struct swc_window *w)
{
	Client *c;

	if (!w)
		return NULL;
	wl_list_for_each(c, &clients, link)
		if (c->win == w)
			return c;
	return NULL;
}

static int
client_visible(const Client *c)
{
	return c && c->mon && VISIBLEON(c, c->mon);
}

static Client *
focus_top(Monitor *m)
{
	Client *c;

	if (!m)
		return NULL;
	wl_list_for_each(c, &fstack, flink)
		if (VISIBLEON(c, m))
			return c;
	return NULL;
}

static int
client_count(void)
{
	int n = 0;
	struct wl_list *l;

	for (l = clients.next; l != &clients; l = l->next)
		n++;
	return n;
}

static void
raise_client(Client *c)
{
	int i, n;

	if (!c || !c->shown)
		return;
	n = client_count();
	for (i = 0; i < n; i++)
		swc_window_stack(c->win, -1);
}

static void
restack_floats(void)
{
	Client *c;
	int any = 0;

	wl_list_for_each(c, &clients, link) {
		if (c->floating && c->shown && c != sel && !c->fullscreen) {
			raise_client(c);
			any = 1;
		}
	}
	if (sel && sel->shown && (sel->floating || sel->fullscreen))
		raise_client(sel);
	(void)any;
}

static int
glob_match(const char *pattern, const char *text)
{
	if (!pattern)
		return 1;
	if (!text)
		return 0;
	return fnmatch(pattern, text, FNM_CASEFOLD) == 0;
}

static void
apply_rules(Client *c)
{
	int i;

	if (!c || !c->win)
		return;
	if (!c->win->app_id && !c->win->title)
		return;
	for (i = 0; i < cfg.nrules; i++) {
		const Rule *r = &cfg.rules[i];
		Monitor *m = c->mon, *t;
		int idx;

		if (!r->appid && !r->title)
			continue;
		if (r->appid && !c->win->app_id)
			continue;
		if (r->title && !c->win->title)
			continue;
		if (!glob_match(r->appid, c->win->app_id) ||
		    !glob_match(r->title, c->win->title))
			continue;

		c->rules_done = 1;
		if (r->monitor >= 0) {
			idx = 0;
			wl_list_for_each(t, &mons, link) {
				if (idx++ == r->monitor) {
					m = t;
					break;
				}
			}
		}
		if (m != c->mon) {
			dw_forget(c);
			c->mon = m;
			c->tags = m->tagset[m->seltags] & TAGMASK;
		}
		if (r->ws > 0 && r->ws <= cfg.ntags)
			c->tags = 1u << (r->ws - 1);
		if (r->floating && !c->floating)
			setfloating(c, 1);
		if (r->fullscreen)
			setfullscreen(c, 1);
		else
			arrange(c->mon);
		return;
	}
}

#endif
