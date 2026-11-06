/* Key, button and scroll bindings. swc keeps pointers into the binding array: never change it while a bound key is held. */
#ifndef OFWC_INPUT_KEYBOARD_H
#define OFWC_INPUT_KEYBOARD_H

typedef struct {
	int type;
	uint32_t mods;
	uint32_t orig;
	uint32_t ksym;
} Reg;

static Reg *regs;
static size_t nregs, cregs;
static int held;

static struct xkb_context *xctx;
static struct xkb_keymap *xmap;

static struct wl_event_source *reload_timer;
static int reload_tries;

static const Bind *
find_bind(int type, uint32_t mods, uint32_t value)
{
	int i;

	for (i = 0; i < cfg.nbinds; i++) {
		const Bind *b = &cfg.binds[i];

		if (b->type == type && b->mods == mods && b->value == value)
			return b;
	}
	return NULL;
}

static void
binding_dispatch(void *data, uint32_t time, uint32_t value, uint32_t state)
{
	const Reg *r = data;
	const Bind *b;

	(void)time;
	(void)value;
	if (!r)
		return;
	b = find_bind(r->type, r->mods, r->orig);
	if (state) {
		held++;
		if (b && b->act && b->act->fn)
			b->act->fn(&b->arg, 1);
	} else {
		if (held > 0)
			held--;
		if (b && b->act && b->act->both && b->act->fn)
			b->act->fn(&b->arg, 0);
	}
}

static int
reg_has(int type, uint32_t mods, uint32_t ksym)
{
	size_t i;

	for (i = 0; i < nregs; i++)
		if (regs[i].type == type && regs[i].mods == mods && regs[i].ksym == ksym)
			return 1;
	return 0;
}

static void
reg_add(int type, uint32_t mods, uint32_t orig, uint32_t ksym)
{
	if (reg_has(type, mods, ksym))
		return;
	regs = grow(regs, &cregs, nregs, sizeof(Reg));
	regs[nregs].type = type;
	regs[nregs].mods = mods;
	regs[nregs].orig = orig;
	regs[nregs].ksym = ksym;
	nregs++;
}

/* A missing layout or variant never stops ofwc: fall back to "us". */

#ifndef SWC_HAS_KEYBOARD_SET_KEYMAP
bool swc_keyboard_set_keymap(const char *rules, const char *model, const char *layout,
                             const char *variant, const char *options);
#endif
extern bool swc_keyboard_set_keymap(const char *rules, const char *model, const char *layout,
                                    const char *variant, const char *options)
    __attribute__((weak));

static char *kb_sel[5];

static const char *const kb_env_name[5] = {
	"XKB_DEFAULT_RULES", "XKB_DEFAULT_MODEL", "XKB_DEFAULT_LAYOUT",
	"XKB_DEFAULT_VARIANT", "XKB_DEFAULT_OPTIONS",
};
static char *kb_env_orig[5];
static int kb_env_saved;

static void
kb_env_apply(const char *const v[5])
{
	int i;

	if (!kb_env_saved) {
		for (i = 0; i < 5; i++) {
			const char *e = getenv(kb_env_name[i]);

			kb_env_orig[i] = e ? estrdup(e) : NULL;
		}
		kb_env_saved = 1;
	}
	for (i = 0; i < 5; i++) {
		if (v[i])
			setenv(kb_env_name[i], v[i], 1);
		else if (kb_env_orig[i])
			setenv(kb_env_name[i], kb_env_orig[i], 1);
		else
			unsetenv(kb_env_name[i]);
	}
}

static int
kb_compiles(struct xkb_context *ctx, const char *const v[5])
{
	struct xkb_rule_names n;
	struct xkb_keymap *map;

	memset(&n, 0, sizeof(n));
	n.rules = v[0];
	n.model = v[1];
	n.layout = v[2];
	n.variant = v[3];
	n.options = v[4];
	map = xkb_keymap_new_from_names(ctx, &n, XKB_KEYMAP_COMPILE_NO_FLAGS);
	if (!map)
		return 0;
	if (xkb_keymap_num_layouts(map) == 0) {
		xkb_keymap_unref(map);
		return 0;
	}
	xkb_keymap_unref(map);
	return 1;
}

static int
kb_select(const Config *c, const char *v[5])
{
	const char *lv[3][5];
	struct xkb_context *ctx;
	int level;

	lv[0][0] = *c->kb_rules ? c->kb_rules : NULL;
	lv[0][1] = *c->kb_model ? c->kb_model : NULL;
	lv[0][2] = *c->kb_layout ? c->kb_layout : NULL;
	lv[0][3] = *c->kb_variant ? c->kb_variant : NULL;
	lv[0][4] = *c->kb_options ? c->kb_options : NULL;
	lv[1][0] = lv[0][0];
	lv[1][1] = lv[0][1];
	lv[1][2] = "us";
	lv[1][3] = "";
	lv[1][4] = lv[0][4];
	lv[2][0] = "evdev";
	lv[2][1] = "pc105";
	lv[2][2] = "us";
	lv[2][3] = "";
	lv[2][4] = "";

	ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	for (level = 0; level < 3; level++) {
		kb_env_apply(lv[level]);
		if (!ctx || level == 2 || kb_compiles(ctx, lv[level]))
			break;
	}
	if (ctx)
		xkb_context_unref(ctx);
	memcpy(v, lv[level], sizeof(lv[level]));
	return level;
}

static void
kb_store(const char *const v[5])
{
	int i;

	for (i = 0; i < 5; i++) {
		free(kb_sel[i]);
		kb_sel[i] = v[i] ? estrdup(v[i]) : NULL;
	}
}

static void
kb_apply(void)
{
	const char *v[5];
	int level;

	if (!swc_keyboard_set_keymap) {
		ofwc_log("keyboard layout changes take effect after a restart "
		         "(neuswc without patch 0016)");
		return;
	}
	level = kb_select(&cfg, v);
	if (level)
		ofwc_log("keyboard: the layout in the configuration does not work, using \"us\"");
	if (swc_keyboard_set_keymap(v[0], v[1], v[2], v[3], v[4])) {
		kb_store(v);
		if (xmap) {
			xkb_keymap_unref(xmap);
			xmap = NULL;
		}
		ofwc_log("keyboard layout reloaded");
	} else {
		ofwc_log("keyboard: cannot rebuild the keymap, keeping the old one");
	}
}

static void
kb_keymap_init(void)
{
	struct xkb_rule_names n;

	if (xmap)
		return;
	if (!xctx)
		xctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (!xctx)
		return;
	memset(&n, 0, sizeof(n));
	n.rules = kb_sel[0];
	n.model = kb_sel[1];
	n.layout = kb_sel[2];
	n.variant = kb_sel[3];
	n.options = kb_sel[4];
	xmap = xkb_keymap_new_from_names(xctx, &n, XKB_KEYMAP_COMPILE_NO_FLAGS);
	if (!xmap)
		ofwc_log("keymap: cannot compile it; shortcuts work in the first layout only");
}

static void
kb_add_aliases(const Bind *b)
{
	xkb_keycode_t kc, kmin, kmax;
	xkb_layout_index_t nl, l, l2;
	int lv, lv2;

	if (!xmap)
		return;
	kmin = xkb_keymap_min_keycode(xmap);
	kmax = xkb_keymap_max_keycode(xmap);
	nl = xkb_keymap_num_layouts(xmap);
	if (nl < 2)
		return;
	for (kc = kmin; kc <= kmax; kc++) {
		int match = 0;

		for (l = 0; l < nl && !match; l++) {
			for (lv = 0; lv < 2 && !match; lv++) {
				const xkb_keysym_t *syms;

				if (lv >= (int)xkb_keymap_num_levels_for_key(xmap, kc, l))
					continue;
				if (xkb_keymap_key_get_syms_by_level(xmap, kc, l, lv, &syms) >= 1 &&
				    syms[0] == b->value)
					match = 1;
			}
		}
		if (!match)
			continue;
		for (l2 = 0; l2 < nl; l2++) {
			for (lv2 = 0; lv2 < 2; lv2++) {
				const xkb_keysym_t *syms;

				if (lv2 >= (int)xkb_keymap_num_levels_for_key(xmap, kc, l2))
					continue;
				if (xkb_keymap_key_get_syms_by_level(xmap, kc, l2, lv2, &syms) < 1 ||
				    syms[0] == XKB_KEY_NoSymbol || syms[0] == b->value)
					continue;
				reg_add(SWC_BINDING_KEY, b->mods, b->value, syms[0]);
			}
		}
	}
}

static uint32_t axis_slot[16];
static int axis_registered[16];

static void
axis_dispatch(void *data, uint32_t time, uint32_t axis, int32_t value120)
{
	static int acc;
	uint32_t mods = *(const uint32_t *)data;
	const AxisBind *ab = NULL;
	int i, dir = value120 < 0 ? -1 : 1;

	for (i = 0; i < cfg.naxbinds; i++) {
		if (cfg.axbinds[i].mods == mods && cfg.axbinds[i].dir == dir) {
			ab = &cfg.axbinds[i];
			break;
		}
	}
	if (!ab || !ab->act || !ab->act->fn) {
		acc = 0;
		swc_pointer_send_axis(time, axis, value120);
		return;
	}
	acc += value120;
	if (acc * dir < 120)
		return;
	acc -= dir * 120;
	ab->act->fn(&ab->arg, 1);
}

static void
rebind(void)
{
	size_t i;
	int k;

	for (i = 0; i < nregs; i++)
		swc_remove_binding(regs[i].type, regs[i].mods, regs[i].ksym);
	free(regs);
	regs = NULL;
	nregs = cregs = 0;

	kb_keymap_init();
	for (k = 0; k < cfg.nbinds; k++)
		reg_add(cfg.binds[k].type, cfg.binds[k].mods, cfg.binds[k].value,
		        cfg.binds[k].value);
	for (k = 0; k < cfg.nbinds; k++)
		if (cfg.binds[k].type == SWC_BINDING_KEY)
			kb_add_aliases(&cfg.binds[k]);

	for (i = 0; i < nregs; i++) {
		int rc = swc_add_binding(regs[i].type, regs[i].mods, regs[i].ksym,
		                         binding_dispatch, &regs[i]);

		if (rc < 0)
			ofwc_log("cannot register a binding (%d)", rc);
	}

	/* swc cannot remove axis bindings: register each modifier set once, statically. */
	for (k = 0; k < cfg.naxbinds; k++) {
		uint32_t mods = cfg.axbinds[k].mods & 15u;

		if (axis_registered[mods])
			continue;
		axis_slot[mods] = mods;
		if (swc_add_axis_binding(mods, WL_POINTER_AXIS_VERTICAL_SCROLL,
		                         axis_dispatch, &axis_slot[mods]) == 0)
			axis_registered[mods] = 1;
	}
	ofwc_dbg("registered %zu key/button bindings", nregs);
}

static int
reload_tick(void *data)
{
	(void)data;
	if (held > 0 && reload_tries++ < 40) {
		wl_event_source_timer_update(reload_timer, 25);
		return 0;
	}
	held = 0;
	do_reload();
	return 0;
}

static void
request_reload(void)
{
	if (!reload_timer)
		reload_timer = wl_event_loop_add_timer(loop, reload_tick, NULL);
	if (!reload_timer)
		return;
	reload_tries = 0;
	wl_event_source_timer_update(reload_timer, 10);
}

#endif
