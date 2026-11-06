/* Config types and parsing helpers. */
#ifndef OFWC_CONFIG_CONFIG_UTIL_H
#define OFWC_CONFIG_CONFIG_UTIL_H

enum { BRD_ORIGINAL, BRD_START, BRD_END, BRD_STARTEND };

#define MAXTAGS 31

typedef struct {
	long i;
	double f;
	char *s;
} Arg;

typedef void (*ActionFn)(const Arg *arg, int pressed);

typedef struct {
	const char *name;
	ActionFn fn;
	int both;
} Action;

typedef struct {
	int type;
	uint32_t mods;
	uint32_t value;
	const Action *act;
	Arg arg;
} Bind;

typedef struct {
	uint32_t mods;
	int dir;
	const Action *act;
	Arg arg;
} AxisBind;

typedef struct {
	char *appid;
	char *title;
	int ws;
	int floating, fullscreen;
	int monitor;
} Rule;

typedef struct {
	int mon;
	double mfact;
} MonRule;

typedef struct Config {
	int sloppyfocus;
	double float_w, float_h;
	int motion_hz;
	uint32_t rootcolor;
	int gappx, smartgaps, gaps;
	int border_type;
	unsigned borderpx, borderspx, borderepx;
	unsigned borderspx_offset, borderepx_negative_offset;
	uint32_t bordercolor, focuscolor, urgentcolor;
	uint32_t borderscolor, borderecolor;
	uint32_t borders_focuscolor, borders_urgentcolor;
	uint32_t bordere_focuscolor, bordere_urgentcolor;
	int borders_only_floating;
	uint32_t modkey;
	char *kb_rules, *kb_layout, *kb_variant, *kb_model, *kb_options;
	int repeat_rate, repeat_delay;
	int tap, tap_drag, drag_lock, natural, dwt, left_handed, middle_emu;
	int scroll_method, click_method, send_events, accel_profile, button_map;
	double accel_speed;
	int dw_spiral, dw_attach, dw_mirror, dw_mouse;
	double mfact;
	char **tags;
	int ntags;
	char **autostart;
	int nautostart;
	Rule *rules;
	int nrules;
	MonRule *monrules;
	int nmonrules;
	Bind *binds;
	int nbinds;
	AxisBind *axbinds;
	int naxbinds;
} Config;

static void *
grow(void *p, size_t *cap, size_t len, size_t item)
{
	if (len + 1 > *cap) {
		*cap = *cap ? *cap * 2 : 16;
		p = erealloc(p, *cap * item);
	}
	return p;
}

static int
parse_color(const char *s, uint32_t *out)
{
	char *end;
	unsigned long v;
	unsigned r, g, b, a;
	size_t digits;

	if (!s)
		return -1;
	while (*s == ' ')
		s++;
	if (s[0] == '#')
		s++;
	else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
		s += 2;
	digits = strspn(s, "0123456789abcdefABCDEF");
	if (digits != 6 && digits != 8)
		return -1;
	v = strtoul(s, &end, 16);
	if (end == s)
		return -1;
	if (digits == 6) {
		r = (v >> 16) & 0xff; g = (v >> 8) & 0xff; b = v & 0xff; a = 0xff;
	} else {
		r = (v >> 24) & 0xff; g = (v >> 16) & 0xff; b = (v >> 8) & 0xff; a = v & 0xff;
	}
	if (a != 0xff) {
		r = r * a / 255; g = g * a / 255; b = b * a / 255;
	}
	*out = (a << 24) | (r << 16) | (g << 8) | b;
	return 0;
}

static int
parse_bool(const char *s, int *out)
{
	if (!strcasecmp(s, "1") || !strcasecmp(s, "true") || !strcasecmp(s, "yes") ||
	    !strcasecmp(s, "on")) {
		*out = 1;
		return 0;
	}
	if (!strcasecmp(s, "0") || !strcasecmp(s, "false") || !strcasecmp(s, "no") ||
	    !strcasecmp(s, "off")) {
		*out = 0;
		return 0;
	}
	return -1;
}

static int
parse_long(const char *s, long *out)
{
	char *end;
	long v;

	errno = 0;
	v = strtol(s, &end, 0);
	if (end == s || *end || errno)
		return -1;
	*out = v;
	return 0;
}

static int
parse_num(const char *s, long *out)
{
	double d;

	if (!s)
		return -1;
	if (parse_long(s, out) == 0)
		return 0;
	errno = 0;
	{
		char *end;

		d = strtod(s, &end);
		if (end == s || *end || errno || d != d || d > 1e9 || d < -1e9)
			return -1;
	}
	*out = (long)d;
	return 0;
}

static int
parse_double(const char *s, double *out)
{
	char *end;
	double v;

	errno = 0;
	v = strtod(s, &end);
	if (end == s || *end || errno)
		return -1;
	*out = v;
	return 0;
}

static int
parse_modifier_name(const char *tok, uint32_t modkey, uint32_t *out)
{
	if (!strcasecmp(tok, "MODKEY"))
		*out = modkey;
	else if (!strcasecmp(tok, "SUPER") || !strcasecmp(tok, "LOGO") ||
	         !strcasecmp(tok, "MOD4") || !strcasecmp(tok, "WIN") ||
	         !strcasecmp(tok, "META"))
		*out = SWC_MOD_LOGO;
	else if (!strcasecmp(tok, "ALT") || !strcasecmp(tok, "MOD1"))
		*out = SWC_MOD_ALT;
	else if (!strcasecmp(tok, "CTRL") || !strcasecmp(tok, "CONTROL"))
		*out = SWC_MOD_CTRL;
	else if (!strcasecmp(tok, "SHIFT"))
		*out = SWC_MOD_SHIFT;
	else if (!strcasecmp(tok, "NONE") || !strcasecmp(tok, "") ||
	         !strcasecmp(tok, "0"))
		*out = 0;
	else
		return -1;
	return 0;
}

static int
parse_mods(const char *s, uint32_t modkey, uint32_t *out)
{
	char buf[128], *tok, *save = NULL;
	uint32_t m, acc = 0;

	if (strlen(s) >= sizeof(buf))
		return -1;
	strcpy(buf, s);
	for (tok = strtok_r(buf, "|+, ", &save); tok; tok = strtok_r(NULL, "|+, ", &save)) {
		if (parse_modifier_name(tok, modkey, &m) < 0)
			return -1;
		acc |= m;
	}
	*out = acc;
	return 0;
}

static int
parse_keysym(const char *name, uint32_t *ksym, uint32_t *extra_mods)
{
	xkb_keysym_t k;

	*extra_mods = 0;
	if (!name || !*name)
		return -1;
	if (name[1] == '\0' && name[0] >= 'A' && name[0] <= 'Z') {
		*ksym = (uint32_t)(name[0] - 'A' + 'a');
		*extra_mods = SWC_MOD_SHIFT;
		return 0;
	}
	k = xkb_keysym_from_name(name, XKB_KEYSYM_NO_FLAGS);
	if (k == XKB_KEY_NoSymbol)
		k = xkb_keysym_from_name(name, XKB_KEYSYM_CASE_INSENSITIVE);
	if (k == XKB_KEY_NoSymbol && (unsigned char)name[0] < 0x80 && name[1] == '\0')
		k = xkb_utf32_to_keysym((uint32_t)(unsigned char)name[0]);
	if (k == XKB_KEY_NoSymbol)
		return -1;
	*ksym = (uint32_t)k;
	return 0;
}

static int
parse_button(const char *name, uint32_t *code)
{
	long v;

	if (!strcasecmp(name, "BTN_LEFT"))
		*code = 0x110;
	else if (!strcasecmp(name, "BTN_RIGHT"))
		*code = 0x111;
	else if (!strcasecmp(name, "BTN_MIDDLE"))
		*code = 0x112;
	else if (!strcasecmp(name, "BTN_SIDE"))
		*code = 0x113;
	else if (!strcasecmp(name, "BTN_EXTRA"))
		*code = 0x114;
	else if (!strcasecmp(name, "BTN_FORWARD"))
		*code = 0x115;
	else if (!strcasecmp(name, "BTN_BACK"))
		*code = 0x116;
	else if (parse_long(name, &v) == 0 && v > 0)
		*code = (uint32_t)v;
	else
		return -1;
	return 0;
}

#endif
