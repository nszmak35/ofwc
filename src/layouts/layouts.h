/* The only layout: dwindle (and spiral). */
#ifndef OFWC_LAYOUTS_LAYOUTS_H
#define OFWC_LAYOUTS_LAYOUTS_H

static unsigned
pt_index(const Monitor *m)
{
	uint32_t ts = m->tagset[m->seltags] & TAGMASK;

	if (ts && !(ts & (ts - 1)))
		return (unsigned)__builtin_ctz(ts) + 1;
	return 0;
}

static void
dw_tree_free(DwindleNode *n)
{
	if (!n)
		return;
	dw_tree_free(n->child[0]);
	dw_tree_free(n->child[1]);
	free(n);
}

static DwindleNode *
dw_leaf(DwindleNode *n, const Client *c)
{
	DwindleNode *r;

	if (!n)
		return NULL;
	if (!n->is_node)
		return n->client == c ? n : NULL;
	r = dw_leaf(n->child[0], c);
	return r ? r : dw_leaf(n->child[1], c);
}

static DwindleNode *
dw_first_leaf(DwindleNode *n)
{
	while (n && n->is_node)
		n = n->child[0];
	return n;
}

static int
dw_depth(const DwindleNode *n)
{
	int d = 0;

	while (n->parent) {
		n = n->parent;
		d++;
	}
	return d;
}

static void
dw_remove_leaf(DwindleNode **rootp, const Client *c)
{
	DwindleNode *leaf, *parent, *sibling, *grand;

	if (!rootp || !*rootp || !(leaf = dw_leaf(*rootp, c)))
		return;
	parent = leaf->parent;
	if (!parent) {
		free(leaf);
		*rootp = NULL;
		return;
	}
	sibling = parent->child[parent->child[0] == leaf ? 1 : 0];
	grand = parent->parent;
	if (grand) {
		if (grand->child[0] == parent)
			grand->child[0] = sibling;
		else
			grand->child[1] = sibling;
	} else {
		*rootp = sibling;
	}
	sibling->parent = grand;
	free(leaf);
	free(parent);
}

/* Must run before a Client is freed, or other trees keep a dangling pointer. */
static void
dw_forget(Client *c)
{
	Monitor *m;
	int t;

	c->dw_has = 0;
	c->dw_anchor = NULL;
	wl_list_for_each(m, &mons, link)
		for (t = 0; t <= MAXTAGS; t++)
			dw_remove_leaf(&m->pt[t].root, c);
}

static int
dw_tiled(const Client *c, const Monitor *m)
{
	return c && c->mon == m && VISIBLEON(c, m) && !c->floating && !c->fullscreen;
}

static void
dw_insert(DwindleNode **rootp, Client *newc, Client *anchor, int side, int up)
{
	DwindleNode *leaf, *at, *np;

	leaf = ecalloc(1, sizeof(*leaf));
	leaf->client = newc;
	if (!*rootp) {
		*rootp = leaf;
		return;
	}
	at = anchor ? dw_leaf(*rootp, anchor) : NULL;
	if (!at)
		at = dw_first_leaf(*rootp);
	while (up-- > 0 && at->parent)
		at = at->parent;
	np = ecalloc(1, sizeof(*np));
	np->is_node = 1;
	np->box = at->box;
	np->ratio = 1.0;
	np->parent = at->parent;
	np->child[side ? 0 : 1] = at;
	np->child[side ? 1 : 0] = leaf;
	at->parent = np;
	leaf->parent = np;
	if (np->parent) {
		if (np->parent->child[0] == at)
			np->parent->child[0] = np;
		else
			np->parent->child[1] = np;
	} else {
		*rootp = np;
	}
}

static Client *
dw_find_gone(DwindleNode *n, Monitor *m)
{
	Client *r;

	if (!n)
		return NULL;
	if (!n->is_node) {
		Client *c = n->client;

		if (!c || c->mon != m || !VISIBLEON(c, m) || c->floating)
			return c;
		return NULL;
	}
	r = dw_find_gone(n->child[0], m);
	return r ? r : dw_find_gone(n->child[1], m);
}

static void
dw_remember_slot(DwindleNode *root, Client *c, const Monitor *m)
{
	DwindleNode *leaf, *parent, *sib, *first, *n;
	int side, up = 0;

	if (!c || !c->floating || c->mon != m || !root)
		return;
	c->dw_has = 0;
	c->dw_anchor = NULL;
	leaf = dw_leaf(root, c);
	if (!leaf || !(parent = leaf->parent))
		return;
	side = parent->child[0] == leaf ? 0 : 1;
	sib = parent->child[side ? 0 : 1];
	first = dw_first_leaf(sib);
	if (!sib || !first || !first->client || first->client == c)
		return;
	for (n = first; n && n != sib; n = n->parent)
		up++;
	if (n != sib)
		return;
	c->dw_anchor = first->client;
	c->dw_side = side;
	c->dw_up = up;
	c->dw_has = 1;
}

static Client *
dw_slot_anchor(const Client *c, Monitor *m, DwindleNode *root)
{
	Client *t;

	if (!c->dw_has || !c->dw_anchor || !root)
		return NULL;
	wl_list_for_each(t, &clients, link)
		if (t == c->dw_anchor)
			return t != c && dw_tiled(t, m) && dw_leaf(root, t) ? t : NULL;
	return NULL;
}

static void
dw_sync(Monitor *m)
{
	DwindleNode **rootp = &m->pt[pt_index(m)].root;
	Client *c, *gone, *anchor = NULL, *mouse;
	int need = 0;

	while ((gone = dw_find_gone(*rootp, m)) != NULL) {
		dw_remember_slot(*rootp, gone, m);
		dw_remove_leaf(rootp, gone);
	}

	wl_list_for_each(c, &clients, link)
		if (dw_tiled(c, m) && !dw_leaf(*rootp, c))
			need = 1;
	if (!need)
		return;

	if (sel && sel->mon == m && dw_tiled(sel, m) && dw_leaf(*rootp, sel))
		anchor = sel;
	if (cfg.dw_mouse) {
		mouse = client_at_cursor();
		if (mouse && dw_tiled(mouse, m) && dw_leaf(*rootp, mouse))
			anchor = mouse;
	}
	wl_list_for_each(c, &clients, link) {
		Client *at = anchor, *slot;
		int side = 1, up = 0;

		if (!dw_tiled(c, m) || dw_leaf(*rootp, c))
			continue;
		if ((slot = dw_slot_anchor(c, m, *rootp)) != NULL) {
			at = slot;
			side = c->dw_side;
			up = c->dw_up;
		}
		c->dw_has = 0;
		c->dw_anchor = NULL;
		dw_insert(rootp, c, at, side, up);
		anchor = c;
	}
}

static void
dw_recalc(DwindleNode *n, int gap, const Box *area, int mirror_h, int mirror_v)
{
	Box *b0, *b1;
	int a, f;

	if (!n)
		return;
	if (!n->is_node) {
		if (n->client) {
			Box b = n->box;

			if (mirror_h)
				b.x = area->x + area->w - (b.x - area->x) - b.w;
			if (mirror_v)
				b.y = area->y + area->h - (b.y - area->y) - b.h;
			place_box(n->client, b);
		}
		return;
	}
	f = cfg.dw_spiral ? ((dw_depth(n) >> 1) & 1) : 0;
	b0 = &n->child[f]->box;
	b1 = &n->child[!f]->box;
	n->split_top = n->box.h > n->box.w;
	if (!n->split_top) {
		a = MAX(1, (int)(n->box.w / 2.0 * n->ratio) - gap / 2);
		a = MIN(a, MAX(1, n->box.w - gap - 1));
		*b0 = (Box){n->box.x, n->box.y, a, n->box.h};
		*b1 = (Box){n->box.x + a + gap, n->box.y, MAX(1, n->box.w - a - gap), n->box.h};
	} else {
		a = MAX(1, (int)(n->box.h / 2.0 * n->ratio) - gap / 2);
		a = MIN(a, MAX(1, n->box.h - gap - 1));
		*b0 = (Box){n->box.x, n->box.y, n->box.w, a};
		*b1 = (Box){n->box.x, n->box.y + a + gap, n->box.w, MAX(1, n->box.h - a - gap)};
	}
	dw_recalc(n->child[0], gap, area, mirror_h, mirror_v);
	dw_recalc(n->child[1], gap, area, mirror_h, mirror_v);
}

static Box
dw_mirror_box(const Monitor *m, Box b)
{
	if (cfg.dw_mirror)
		b.x = m->area.x + m->area.w - (b.x - m->area.x) - b.w;
	if (cfg.dw_attach)
		b.y = m->area.y + m->area.h - (b.y - m->area.y) - b.h;
	return b;
}

static void
fibonacci_arrange(Monitor *m)
{
	Client *c, **order;
	unsigned i = 0, n = 0, idx = pt_index(m);
	int nx, ny, nw, nh, g, split_min, minsize, s = cfg.dw_spiral ? 0 : 1;

	wl_list_for_each(c, &clients, link)
		if (dw_tiled(c, m))
			n++;
	if (!n)
		return;
	g = cfg.gappx * (m->gaps ? 1 : 0);
	if (cfg.smartgaps > 0 && (unsigned)cfg.smartgaps == n)
		g = 0;
	order = ecalloc(n, sizeof(*order));
	wl_list_for_each(c, &clients, link)
		if (dw_tiled(c, m))
			order[i++] = c;

	minsize = MAX(1 + 2 * (int)cfg.borderpx,
	              2 * (int)cfg.borderspx + 2 * (int)cfg.borderspx_offset);
	split_min = 2 * minsize + g;
	nx = m->area.x + g;
	ny = m->area.y + g;
	nw = MAX(0, m->area.w - 2 * g);
	nh = MAX(0, m->area.h - 2 * g);

	for (i = 0; i < n; i++) {
		if ((i % 2 && nh > split_min) || (!(i % 2) && nw > split_min) || i == n - 1) {
			if (i < n - 1) {
				if (i % 2)
					nh = (nh - g) / 2;
				else
					nw = (nw - g) / 2;
				if ((i % 4) == 2 && !s)
					nx += nw + g;
				else if ((i % 4) == 3 && !s)
					ny += nh + g;
			}
			if ((i % 4) == 0) {
				if (s)
					ny += nh + g;
				else
					ny -= nh + g;
			} else if ((i % 4) == 1) {
				nx += nw + g;
			} else if ((i % 4) == 2) {
				ny += nh + g;
			} else if ((i % 4) == 3) {
				if (s)
					nx += nw + g;
				else
					nx -= nw + g;
			}
			if (i == 0) {
				if (n != 1)
					nw = MAX(0, (int)((m->area.w - 2 * g - g) * m->pt[idx].mfact));
				ny = m->area.y + g;
			} else if (i == 1) {
				nw = MAX(0, m->area.w - 2 * g - nw - g);
			}
		}
		place_box(order[i], dw_mirror_box(m, (Box){nx, ny, MAX(1, nw), MAX(1, nh)}));
	}
	free(order);
}

static void
dwindle_arrange(Monitor *m)
{
	unsigned idx;
	DwindleNode *root;
	Client *c;
	int n = 0, e;
	Box area;

	if (!cfg.dw_mouse) {
		fibonacci_arrange(m);
		return;
	}
	dw_sync(m);
	idx = pt_index(m);
	root = m->pt[idx].root;
	if (!root)
		return;

	wl_list_for_each(c, &clients, link)
		if (dw_tiled(c, m))
			n++;
	e = cfg.gappx * (m->gaps ? 1 : 0);
	if (cfg.smartgaps > 0 && cfg.smartgaps == n)
		e = 0;

	area = (Box){m->area.x + e, m->area.y + e, MAX(1, m->area.w - 2 * e),
	             MAX(1, m->area.h - 2 * e)};
	root->box = area;
	if (root->is_node)
		root->ratio = 2.0 * m->pt[idx].mfact;
	dw_recalc(root, e, &area, cfg.dw_mirror, cfg.dw_attach);
}

static void
clients_swap(Client *a, Client *b)
{
	struct wl_list *pa, *pb;

	if (a == b)
		return;
	if (a->link.next == &b->link) {
		wl_list_remove(&b->link);
		wl_list_insert(a->link.prev, &b->link);
	} else if (b->link.next == &a->link) {
		wl_list_remove(&a->link);
		wl_list_insert(b->link.prev, &a->link);
	} else {
		pa = a->link.prev;
		pb = b->link.prev;
		wl_list_remove(&a->link);
		wl_list_remove(&b->link);
		wl_list_insert(pb, &a->link);
		wl_list_insert(pa, &b->link);
	}
}

static int
dw_swap(Monitor *m, Client *a, Client *b)
{
	DwindleNode *root, *la, *lb;

	if (!a || !b || !dw_tiled(a, m) || !dw_tiled(b, m))
		return -1;
	if (!cfg.dw_mouse) {
		clients_swap(a, b);
		return 0;
	}
	root = m->pt[pt_index(m)].root;
	if (!root || !(la = dw_leaf(root, a)) || !(lb = dw_leaf(root, b)))
		return -1;
	la->client = b;
	lb->client = a;
	return 0;
}

static void
dw_zoom(Monitor *m, Client *c)
{
	DwindleNode *root, *first;
	Client *t, *other = NULL, *top = NULL;

	if (!c || !dw_tiled(c, m))
		return;
	if (!cfg.dw_mouse) {
		wl_list_for_each(t, &clients, link) {
			if (!dw_tiled(t, m))
				continue;
			if (!top)
				top = t;
			else if (!other)
				other = t;
		}
		if (top == c)
			c = other;
		if (!c)
			return;
		wl_list_remove(&c->link);
		wl_list_insert(&clients, &c->link);
		return;
	}
	root = m->pt[pt_index(m)].root;
	if (!root || !dw_leaf(root, c))
		return;
	first = dw_first_leaf(root);
	if (first && first->client && first->client != c)
		dw_swap(m, first->client, c);
}

#endif
