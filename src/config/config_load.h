/* Config loading: built-in defaults, then ~/.config/ofwc/config.py, validated. */
#ifndef OFWC_CONFIG_CONFIG_LOAD_H
#define OFWC_CONFIG_CONFIG_LOAD_H

typedef struct {
	char **it;
	size_t n;
} Row;

typedef struct {
	Config *cfg;
	Row *keys, *buttons;
	size_t nkeys, ckeys, nbuttons, cbuttons;
	size_t bcap, axcap;
	int user_seen[6];
	int errors;
} Builder;

enum { L_TAGS, L_AUTOSTART, L_RULES, L_MONRULES, L_KEYS, L_BUTTONS };

static const Action *find_action(const char *name);

static void
row_free(Row *r)
{
	size_t i;

	for (i = 0; i < r->n; i++)
		free(r->it[i]);
	free(r->it);
	r->it = NULL;
	r->n = 0;
}

static Row
row_copy(const OfwcEntry *e)
{
	Row r;
	size_t i;

	r.n = e->count;
	r.it = ecalloc(e->count, sizeof(char *));
	for (i = 0; i < e->count; i++)
		r.it[i] = estrdup(e->items[i]);
	return r;
}

static void
config_defaults(Config *c)
{
	memset(c, 0, sizeof(*c));
	c->sloppyfocus = 1;
	c->float_w = c->float_h = 0.6;
	c->motion_hz = 90;
	parse_color("0x000000ff", &c->rootcolor);
	c->gappx = 10;
	c->gaps = 1;
	c->border_type = BRD_STARTEND;
	c->borderpx = 1;
	c->borderspx = 3;
	c->borderepx = 2;
	parse_color("0xffffffff", &c->urgentcolor);
	parse_color("0xAFA9A5ff", &c->bordercolor);
	parse_color("0xAFA9A5ff", &c->focuscolor);
	parse_color("0x65727Aff", &c->borderscolor);
	parse_color("0x20272Aff", &c->borderecolor);
	parse_color("0x8E9EA7ff", &c->borders_focuscolor);
	parse_color("0xE8E9E8ff", &c->borders_urgentcolor);
	parse_color("0x20272Aff", &c->bordere_focuscolor);
	parse_color("0x20272Aff", &c->bordere_urgentcolor);
	c->modkey = SWC_MOD_LOGO;
	c->kb_rules = estrdup("");
	c->kb_layout = estrdup("");
	c->kb_variant = estrdup("");
	c->kb_model = estrdup("");
	c->kb_options = estrdup("");
	c->repeat_rate = 25;
	c->repeat_delay = 600;
	c->tap = c->tap_drag = c->drag_lock = c->dwt = 1;
	c->natural = c->left_handed = c->middle_emu = 0;
	c->scroll_method = c->click_method = c->send_events = -1;
	c->accel_profile = c->button_map = -1;
	c->accel_speed = 0.0;
	c->dw_mouse = 1;
	c->mfact = 0.5;
}

static void
bind_free(Bind *b)
{
	free(b->arg.s);
}

static void
config_free(Config *c)
{
	int i;

	free(c->kb_rules);
	free(c->kb_layout);
	free(c->kb_variant);
	free(c->kb_model);
	free(c->kb_options);
	for (i = 0; i < c->ntags; i++)
		free(c->tags[i]);
	free(c->tags);
	for (i = 0; i < c->nautostart; i++)
		free(c->autostart[i]);
	free(c->autostart);
	for (i = 0; i < c->nrules; i++) {
		free(c->rules[i].appid);
		free(c->rules[i].title);
	}
	free(c->rules);
	free(c->monrules);
	for (i = 0; i < c->nbinds; i++)
		bind_free(&c->binds[i]);
	free(c->binds);
	for (i = 0; i < c->naxbinds; i++)
		free(c->axbinds[i].arg.s);
	free(c->axbinds);
	memset(c, 0, sizeof(*c));
}

static void
list_reset_if_user(Builder *b, int list, int source)
{
	Config *c = b->cfg;
	int i;

	if (source != 1 || b->user_seen[list])
		return;
	b->user_seen[list] = 1;
	switch (list) {
	case L_TAGS:
		for (i = 0; i < c->ntags; i++)
			free(c->tags[i]);
		free(c->tags);
		c->tags = NULL;
		c->ntags = 0;
		break;
	case L_AUTOSTART:
		for (i = 0; i < c->nautostart; i++)
			free(c->autostart[i]);
		free(c->autostart);
		c->autostart = NULL;
		c->nautostart = 0;
		break;
	case L_RULES:
		for (i = 0; i < c->nrules; i++) {
			free(c->rules[i].appid);
			free(c->rules[i].title);
		}
		free(c->rules);
		c->rules = NULL;
		c->nrules = 0;
		break;
	case L_MONRULES:
		free(c->monrules);
		c->monrules = NULL;
		c->nmonrules = 0;
		break;
	case L_KEYS:
		for (i = 0; (size_t)i < b->nkeys; i++)
			row_free(&b->keys[i]);
		b->nkeys = 0;
		break;
	case L_BUTTONS:
		for (i = 0; (size_t)i < b->nbuttons; i++)
			row_free(&b->buttons[i]);
		b->nbuttons = 0;
		break;
	}
}

static char *
opt_str(const char *s)
{
	return (!s || !strcmp(s, "NULL") || !*s) ? NULL : estrdup(s);
}

static int
builder_entry(const OfwcEntry *e, void *ud)
{
	Builder *b = ud;
	Config *c = b->cfg;

	if (e->type == OFWC_SCALAR) {
		int rc = config_set_scalar(c, e->key, e->value);

		if (rc < 0) {
			ofwc_log("config: bad value for '%s': %s", e->key, e->value);
			b->errors++;
		} else if (rc == 0) {
			ofwc_dbg("config: ignoring unknown setting '%s'", e->key);
		}
		return 0;
	}
	if (e->count == 0)
		return 0;

	if (!strcmp(e->key, "tags")) {
		list_reset_if_user(b, L_TAGS, e->source);
		if (c->ntags < MAXTAGS) {
			c->tags = erealloc(c->tags, (size_t)(c->ntags + 1) * sizeof(char *));
			c->tags[c->ntags++] = estrdup(e->items[0]);
		}
	} else if (!strcmp(e->key, "autostart")) {
		list_reset_if_user(b, L_AUTOSTART, e->source);
		c->autostart = erealloc(c->autostart, (size_t)(c->nautostart + 1) * sizeof(char *));
		c->autostart[c->nautostart++] = estrdup(e->items[0]);
	} else if (!strcmp(e->key, "rules") && e->count >= 6) {
		Rule *r;
		long l;

		list_reset_if_user(b, L_RULES, e->source);
		c->rules = erealloc(c->rules, (size_t)(c->nrules + 1) * sizeof(Rule));
		r = &c->rules[c->nrules++];
		memset(r, 0, sizeof(*r));
		r->appid = opt_str(e->items[0]);
		r->title = opt_str(e->items[1]);
		r->ws = parse_long(e->items[2], &l) == 0 ? (int)l : 0;
		parse_bool(e->items[3], &r->floating);
		parse_bool(e->items[4], &r->fullscreen);
		r->monitor = parse_long(e->items[5], &l) == 0 ? (int)l : -1;
	} else if (!strcmp(e->key, "monrules") && e->count >= 2) {
		MonRule *m;
		long l;
		double f;

		list_reset_if_user(b, L_MONRULES, e->source);
		c->monrules = erealloc(c->monrules, (size_t)(c->nmonrules + 1) * sizeof(MonRule));
		m = &c->monrules[c->nmonrules++];
		m->mon = parse_long(e->items[0], &l) == 0 ? (int)l : -1;
		m->mfact = parse_double(e->items[1], &f) == 0 ? f : 0.5;
	} else if (!strcmp(e->key, "keys")) {
		list_reset_if_user(b, L_KEYS, e->source);
		b->keys = grow(b->keys, &b->ckeys, b->nkeys, sizeof(Row));
		b->keys[b->nkeys++] = row_copy(e);
	} else if (!strcmp(e->key, "buttons")) {
		list_reset_if_user(b, L_BUTTONS, e->source);
		b->buttons = grow(b->buttons, &b->cbuttons, b->nbuttons, sizeof(Row));
		b->buttons[b->nbuttons++] = row_copy(e);
	}
	return 0;
}

static void
fill_arg(Arg *a, const char *s)
{
	long l;
	double f;

	memset(a, 0, sizeof(*a));
	if (!s || !strcmp(s, "NULL") || !strcmp(s, "NONE"))
		return;
	a->s = estrdup(s);
	if (!strcasecmp(s, "ALL"))
		a->i = -1;
	else if (parse_long(s, &l) == 0)
		a->i = l;
	if (parse_double(s, &f) == 0)
		a->f = f;
}

static void
add_bind(Config *c, size_t *cap, Bind *b)
{
	c->binds = grow(c->binds, cap, (size_t)c->nbinds, sizeof(Bind));
	c->binds[c->nbinds++] = *b;
}

static void
resolve_keys(Builder *b)
{
	Config *c = b->cfg;
	size_t i;

	for (i = 0; i < b->nkeys; i++) {
		Row *r = &b->keys[i];
		Bind bd;
		uint32_t extra;
		const Action *a;

		if (r->n < 3) {
			ofwc_log("config: key row %zu needs [mods, key, action, arg]", i + 1);
			continue;
		}
		memset(&bd, 0, sizeof(bd));
		bd.type = SWC_BINDING_KEY;
		if (parse_mods(r->it[0], c->modkey, &bd.mods) < 0) {
			ofwc_log("config: unknown modifier in '%s' (key '%s')", r->it[0], r->it[1]);
			continue;
		}
		if (parse_keysym(r->it[1], &bd.value, &extra) < 0) {
			ofwc_log("config: unknown key '%s'", r->it[1]);
			continue;
		}
		bd.mods |= extra;
		if (!(a = find_action(r->it[2]))) {
			ofwc_log("config: unknown action '%s' (key '%s') ignored", r->it[2], r->it[1]);
			continue;
		}
		bd.act = a;
		fill_arg(&bd.arg, r->n > 3 ? r->it[3] : NULL);
		add_bind(c, &b->bcap, &bd);
	}
}

static void
resolve_buttons(Builder *b)
{
	Config *c = b->cfg;
	size_t i;

	for (i = 0; i < b->nbuttons; i++) {
		Row *r = &b->buttons[i];
		size_t o = (r->n > 0 && !strncasecmp(r->it[0], "Clk", 3)) ? 1 : 0;
		Bind bd;
		uint32_t mods;
		const Action *a;

		if (r->n < o + 3) {
			ofwc_log("config: button row %zu needs [click, mods, button, action, arg]", i + 1);
			continue;
		}
		if (parse_mods(r->it[o], c->modkey, &mods) < 0) {
			ofwc_log("config: unknown modifier '%s' in buttons", r->it[o]);
			continue;
		}
		if (!(a = find_action(r->it[o + 2]))) {
			ofwc_log("config: unknown action '%s' (buttons) ignored", r->it[o + 2]);
			continue;
		}
		if (!strcasecmp(r->it[o + 1], "SCROLL_UP") ||
		    !strcasecmp(r->it[o + 1], "SCROLL_DOWN")) {
			AxisBind ab;

			memset(&ab, 0, sizeof(ab));
			ab.mods = mods;
			ab.dir = strcasecmp(r->it[o + 1], "SCROLL_UP") == 0 ? -1 : 1;
			ab.act = a;
			fill_arg(&ab.arg, r->n > o + 3 ? r->it[o + 3] : NULL);
			c->axbinds = grow(c->axbinds, &b->axcap, (size_t)c->naxbinds, sizeof(AxisBind));
			c->axbinds[c->naxbinds++] = ab;
			continue;
		}
		memset(&bd, 0, sizeof(bd));
		bd.type = SWC_BINDING_BUTTON;
		bd.mods = mods;
		if (parse_button(r->it[o + 1], &bd.value) < 0) {
			ofwc_log("config: unknown button '%s'", r->it[o + 1]);
			continue;
		}
		bd.act = a;
		fill_arg(&bd.arg, r->n > o + 3 ? r->it[o + 3] : NULL);
		add_bind(c, &b->bcap, &bd);
	}
}

static void
builder_finish(Builder *b)
{
	Config *c = b->cfg;
	size_t i;

	if (c->ntags == 0) {
		int n;

		for (n = 1; n <= 9; n++) {
			char buf[16];

			snprintf(buf, sizeof(buf), "%d", n);
			c->tags = erealloc(c->tags, (size_t)(c->ntags + 1) * sizeof(char *));
			c->tags[c->ntags++] = estrdup(buf);
		}
	}
	/* Clamp values so a bad number cannot break the layout. */
	c->gappx = MAX(0, MIN(c->gappx, 200));
	c->mfact = MAX(0.1, MIN(c->mfact, 0.9));
	c->float_w = MAX(0.1, MIN(c->float_w, 1.0));
	c->float_h = MAX(0.1, MIN(c->float_h, 1.0));
	c->motion_hz = MAX(10, MIN(c->motion_hz, 500));
	c->repeat_rate = MAX(0, MIN(c->repeat_rate, 1000));
	c->repeat_delay = MAX(0, MIN(c->repeat_delay, 10000));
	c->accel_speed = MAX(-1.0, MIN(c->accel_speed, 1.0));

	resolve_keys(b);
	resolve_buttons(b);

	for (i = 0; i < b->nkeys; i++)
		row_free(&b->keys[i]);
	for (i = 0; i < b->nbuttons; i++)
		row_free(&b->buttons[i]);
	free(b->keys);
	free(b->buttons);
	b->keys = b->buttons = NULL;
}

static char *
user_config_path(const char *override)
{
	const char *home, *xdg;
	char buf[PATH_MAX];

	if (override && *override)
		return estrdup(override);
	xdg = getenv("XDG_CONFIG_HOME");
	if (xdg && *xdg && xdg[0] == '/') {
		snprintf(buf, sizeof(buf), "%s/ofwc/config.py", xdg);
		return estrdup(buf);
	}
	home = getenv("HOME");
	if (!home || !*home)
		return NULL;
	snprintf(buf, sizeof(buf), "%s/.config/ofwc/config.py", home);
	return estrdup(buf);
}

static void
config_seed_user_file(const char *path)
{
	char dir[PATH_MAX], *slash;
	FILE *fp;

	if (!path || access(path, F_OK) == 0)
		return;
	if (strlen(path) >= sizeof(dir))
		return;
	strcpy(dir, path);
	slash = strrchr(dir, '/');
	if (!slash)
		return;
	*slash = '\0';
	{
		char *p;

		for (p = dir + 1; *p; p++) {
			if (*p == '/') {
				*p = '\0';
				mkdir(dir, 0755);
				*p = '/';
			}
		}
		mkdir(dir, 0755);
	}
	fp = fopen(path, "wx");
	if (!fp)
		return;
	fputs(ofwc_default_config, fp);
	fclose(fp);
	ofwc_log("created %s", path);
}

static int
config_load(Config *out, const char *user_path, int strict)
{
	Config c;
	Builder b;
	int found = 0;

	config_defaults(&c);
	memset(&b, 0, sizeof(b));
	b.cfg = &c;

	if (ofwc_parse_buffer(ofwc_default_config, "<ofwc built-in defaults>", 0,
	                      builder_entry, &b) < 0) {
		ofwc_log("internal error: built-in default configuration failed");
		goto fail;
	}
	if (user_path) {
		if (ofwc_parse_file(user_path, 1, builder_entry, &b, &found) < 0) {
			ofwc_log("config: error in %s", user_path);
			goto fail;
		}
	}
	if (strict && b.errors) {
		ofwc_log("config: %d bad value(s) in %s", b.errors, user_path ? user_path : "config");
		goto fail;
	}
	builder_finish(&b);
	*out = c;
	return 0;

fail:
	{
		size_t i;

		for (i = 0; i < b.nkeys; i++)
			row_free(&b.keys[i]);
		for (i = 0; i < b.nbuttons; i++)
			row_free(&b.buttons[i]);
		free(b.keys);
		free(b.buttons);
	}
	config_free(&c);
	return -1;
}

#endif
