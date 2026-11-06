/* Borders drawn like solux, as swc window decorations (inner part needs patch 0005). */
#ifndef OFWC_EFFECTS_DECORATIONS_H
#define OFWC_EFFECTS_DECORATIONS_H

static int
mon_staging(const Monitor *m)
{
	const Client *c;

	wl_list_for_each(c, &clients, link)
		if (c->staging && VISIBLEON(c, m))
			return 1;
	return 0;
}

#define BORDER_REACH 64

extern void swc_window_set_decor_inset(struct swc_window *window, uint32_t inset)
    __attribute__((weak));

typedef struct {
	int bw;
	int lo, hi;
	int soff, sw, eoff, ew;
	uint32_t base, scol, ecol;
	unsigned total, inset;
	int clipped;
} BorderSpec;

static void
border_spec(const Client *c, BorderSpec *b)
{
	int bws, bwe, neg, focused = (c == sel);

	memset(b, 0, sizeof(*b));
	if (c->fullscreen)
		return;
	b->bw = (int)MIN(cfg.borderpx, 4096u);
	bws = (cfg.borders_only_floating && !c->floating) ? 0 : (int)MIN(cfg.borderspx, 4096u);
	bwe = (cfg.borders_only_floating && !c->floating) ? 0 : (int)MIN(cfg.borderepx, 4096u);
	neg = (int)MIN(cfg.borderepx_negative_offset, 4096u);

	b->lo = b->hi = b->bw;
	if (b->bw > 0)
		b->lo = MIN(b->lo, 0);
	if (bws > 0) {
		b->soff = (int)MIN(cfg.borderspx_offset, 4096u);
		b->sw = bws;
		b->lo = MIN(b->lo, b->soff);
		b->hi = MAX(b->hi, b->soff + b->sw);
	}
	if (bwe > 0) {
		b->eoff = b->bw - bwe - neg;
		b->ew = bwe;
		b->lo = MIN(b->lo, b->eoff);
		b->hi = MAX(b->hi, b->eoff + b->ew);
	}
	b->lo = MAX(b->lo, b->bw - BORDER_REACH);
	b->hi = MIN(b->hi, b->bw + BORDER_REACH);
	if (b->hi > b->bw && !swc_window_set_decor_inset) {
		b->hi = b->bw; /* neuswc cannot draw over the window */
		b->clipped = 1;
	}
	if (b->hi <= b->lo || (b->bw == 0 && !b->sw && !b->ew))
		b->lo = b->hi = b->bw;
	b->total = (unsigned)(b->hi - b->lo);
	b->inset = (unsigned)(b->hi - b->bw);

	b->base = cfg.bordercolor;
	b->scol = cfg.borderscolor;
	b->ecol = cfg.borderecolor;
	switch (cfg.border_type) {
	case BRD_START:
		if (focused)
			b->scol = cfg.borders_focuscolor;
		break;
	case BRD_END:
		if (focused)
			b->ecol = cfg.bordere_focuscolor;
		break;
	case BRD_ORIGINAL:
		if (focused)
			b->base = cfg.focuscolor;
		break;
	default:
		if (focused) {
			b->scol = cfg.borders_focuscolor;
			b->ecol = cfg.bordere_focuscolor;
		}
		break;
	}
}

static uint32_t
argb_over(uint32_t dst, uint32_t src)
{
	unsigned sa = src >> 24, inv, sh;
	uint32_t out = 0;

	if (sa == 0xff)
		return src;
	if (sa == 0)
		return dst;
	inv = 0xff - sa;
	for (sh = 0; sh < 32; sh += 8) {
		unsigned s = (src >> sh) & 0xff, d = (dst >> sh) & 0xff;

		out |= (uint32_t)MIN(0xffu, s + d * inv / 255) << sh;
	}
	return out;
}

static uint32_t
border_pixel(const BorderSpec *b, unsigned d)
{
	int s = b->lo + (int)d;
	uint32_t px = 0;

	if (s >= 0 && s < b->bw)
		px = b->base;
	if (b->sw && s >= b->soff && s < b->soff + b->sw)
		px = argb_over(px, b->scol);
	if (b->ew && s >= b->eoff && s < b->eoff + b->ew)
		px = argb_over(px, b->ecol);
	return px;
}

#define BORDER_PIXELS(t) ((size_t)(t) * (t) * 4 + (size_t)(t) * 4)

static void
border_build(const BorderSpec *b, uint32_t *mem, struct swc_decor_parts *p)
{
	unsigned t = b->total, x, y;
	uint32_t *tl = mem, *tr = tl + t * t, *bl = tr + t * t, *br = bl + t * t;
	uint32_t *top = br + t * t, *bottom = top + t, *left = bottom + t, *right = left + t;

	for (y = 0; y < t; y++) {
		for (x = 0; x < t; x++) {
			unsigned rx = t - 1 - x, ry = t - 1 - y;

			tl[y * t + x] = border_pixel(b, MIN(x, y));
			tr[y * t + x] = border_pixel(b, MIN(rx, y));
			bl[y * t + x] = border_pixel(b, MIN(x, ry));
			br[y * t + x] = border_pixel(b, MIN(rx, ry));
		}
	}
	for (y = 0; y < t; y++) {
		top[y] = border_pixel(b, y);
		bottom[y] = border_pixel(b, t - 1 - y);
	}
	for (x = 0; x < t; x++) {
		left[x] = border_pixel(b, x);
		right[x] = border_pixel(b, t - 1 - x);
	}
	p->top_left = (struct swc_decor_part){t, t, t * 4, tl};
	p->top_right = (struct swc_decor_part){t, t, t * 4, tr};
	p->bottom_left = (struct swc_decor_part){t, t, t * 4, bl};
	p->bottom_right = (struct swc_decor_part){t, t, t * 4, br};
	p->top = (struct swc_decor_part){1, t, 4, top};
	p->bottom = (struct swc_decor_part){1, t, 4, bottom};
	p->left = (struct swc_decor_part){t, 1, t * 4, left};
	p->right = (struct swc_decor_part){t, 1, t * 4, right};
}

static void
apply_border(Client *c)
{
	static int warned;
	BorderSpec b;
	struct swc_decor_parts parts;
	struct swc_decor decor;
	uint32_t *mem;

	if (!c || !c->win)
		return;
	border_spec(c, &b);
	c->bt = (unsigned)b.bw;
	if (b.clipped && !warned) {
		warned = 1;
		ofwc_log("borders: a ring lies over the window, but neuswc has no "
		         "swc_window_set_decor_inset (patch 0005); that part is not drawn");
	}
	swc_window_set_border(c->win, 0, 0, 0, 0);
	if (!b.total) {
		swc_window_set_decor(c->win, NULL);
		return;
	}
	mem = ecalloc(BORDER_PIXELS(b.total), sizeof(uint32_t));
	memset(&parts, 0, sizeof(parts));
	border_build(&b, mem, &parts);
	memset(&decor, 0, sizeof(decor));
	decor.color = 0;
	decor.top = decor.right = decor.bottom = decor.left = b.total;
	decor.parts = &parts;
	swc_window_set_decor(c->win, &decor);
	if (swc_window_set_decor_inset)
		swc_window_set_decor_inset(c->win, b.inset);
	free(mem);
}

static void
place_box(Client *c, Box cell)
{
	BorderSpec b;
	Box g;
	struct swc_rectangle r;

	if (!c || !c->win || c->fullscreen)
		return;
	border_spec(c, &b);
	if ((unsigned)b.bw != c->bt)
		apply_border(c);
	g.x = cell.x + b.bw;
	g.y = cell.y + b.bw;
	g.w = MAX(1, cell.w - 2 * b.bw);
	g.h = MAX(1, cell.h - 2 * b.bw);
	if (!c->staging && c->mon && mon_staging(c->mon)) {
		c->geom = g;
		return;
	}
	r.x = g.x;
	r.y = g.y;
	r.width = (uint32_t)g.w;
	r.height = (uint32_t)g.h;
	swc_window_set_geometry(c->win, &r);
	c->geom = g;
}

#endif
