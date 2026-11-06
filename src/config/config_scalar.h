/* Table-driven scalar settings. */
#ifndef OFWC_CONFIG_CONFIG_SCALAR_H
#define OFWC_CONFIG_CONFIG_SCALAR_H

enum { K_BOOL, K_INT, K_UINT, K_DBL, K_COLOR, K_STR, K_BRDTYPE, K_MODKEY,
       K_DWTYPE, K_SCROLL, K_CLICK, K_SENDEV, K_ACCEL, K_BTNMAP };

typedef struct {
	const char *name;
	int kind;
	size_t off;
} ScalarDef;

#define S(n, k, f) {n, k, offsetof(Config, f)}
static const ScalarDef scalar_defs[] = {
	S("sloppyfocus", K_BOOL, sloppyfocus),
	S("float_width", K_DBL, float_w),
	S("float_height", K_DBL, float_h),
	S("motion_refreshrate", K_INT, motion_hz),
	S("rootcolor", K_COLOR, rootcolor),
	S("gappx", K_INT, gappx),
	S("smartgaps", K_INT, smartgaps),
	S("gaps", K_BOOL, gaps),
	S("border_color_type", K_BRDTYPE, border_type),
	S("borderpx", K_UINT, borderpx),
	S("borderspx", K_UINT, borderspx),
	S("borderepx", K_UINT, borderepx),
	S("borderspx_offset", K_UINT, borderspx_offset),
	S("borderepx_negative_offset", K_UINT, borderepx_negative_offset),
	S("bordercolor", K_COLOR, bordercolor),
	S("focuscolor", K_COLOR, focuscolor),
	S("urgentcolor", K_COLOR, urgentcolor),
	S("borderscolor", K_COLOR, borderscolor),
	S("borderecolor", K_COLOR, borderecolor),
	S("borders_focuscolor", K_COLOR, borders_focuscolor),
	S("borders_urgentcolor", K_COLOR, borders_urgentcolor),
	S("bordere_focuscolor", K_COLOR, bordere_focuscolor),
	S("bordere_urgentcolor", K_COLOR, bordere_urgentcolor),
	S("borders_only_floating", K_BOOL, borders_only_floating),
	S("modkey", K_MODKEY, modkey),
	S("keyboard_rules", K_STR, kb_rules),
	S("keyboard_layout", K_STR, kb_layout),
	S("keyboard_variant", K_STR, kb_variant),
	S("keyboard_model", K_STR, kb_model),
	S("keyboard_options", K_STR, kb_options),
	S("repeat_rate", K_INT, repeat_rate),
	S("repeat_delay", K_INT, repeat_delay),
	S("tap_to_click", K_BOOL, tap),
	S("tap_and_drag", K_BOOL, tap_drag),
	S("drag_lock", K_BOOL, drag_lock),
	S("natural_scrolling", K_BOOL, natural),
	S("disable_while_typing", K_BOOL, dwt),
	S("left_handed", K_BOOL, left_handed),
	S("middle_button_emulation", K_BOOL, middle_emu),
	S("scroll_method", K_SCROLL, scroll_method),
	S("click_method", K_CLICK, click_method),
	S("send_events_mode", K_SENDEV, send_events),
	S("accel_profile", K_ACCEL, accel_profile),
	S("accel_speed", K_DBL, accel_speed),
	S("button_map", K_BTNMAP, button_map),
	S("dwindle_type", K_DWTYPE, dw_spiral),
	S("dwindle_attach", K_BOOL, dw_attach),
	S("dwindle_mirror", K_BOOL, dw_mirror),
	S("dwindle_mouse_client", K_BOOL, dw_mouse),
};
#undef S

typedef struct { const char *name; int value; } EnumDef;

static const EnumDef scroll_enum[] = {
	{"LIBINPUT_CONFIG_SCROLL_NO_SCROLL", LIBINPUT_CONFIG_SCROLL_NO_SCROLL},
	{"LIBINPUT_CONFIG_SCROLL_2FG", LIBINPUT_CONFIG_SCROLL_2FG},
	{"LIBINPUT_CONFIG_SCROLL_EDGE", LIBINPUT_CONFIG_SCROLL_EDGE},
	{"LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN", LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN},
	{NULL, 0}};
static const EnumDef click_enum[] = {
	{"LIBINPUT_CONFIG_CLICK_METHOD_NONE", LIBINPUT_CONFIG_CLICK_METHOD_NONE},
	{"LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS", LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS},
	{"LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER", LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER},
	{NULL, 0}};
static const EnumDef sendev_enum[] = {
	{"LIBINPUT_CONFIG_SEND_EVENTS_ENABLED", LIBINPUT_CONFIG_SEND_EVENTS_ENABLED},
	{"LIBINPUT_CONFIG_SEND_EVENTS_DISABLED", LIBINPUT_CONFIG_SEND_EVENTS_DISABLED},
	{"LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE",
	 LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE},
	{NULL, 0}};
static const EnumDef accel_enum[] = {
	{"LIBINPUT_CONFIG_ACCEL_PROFILE_NONE", LIBINPUT_CONFIG_ACCEL_PROFILE_NONE},
	{"LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT", LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT},
	{"LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE", LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE},
	{NULL, 0}};
static const EnumDef btnmap_enum[] = {
	{"LIBINPUT_CONFIG_TAP_MAP_LRM", LIBINPUT_CONFIG_TAP_MAP_LRM},
	{"LIBINPUT_CONFIG_TAP_MAP_LMR", LIBINPUT_CONFIG_TAP_MAP_LMR},
	{NULL, 0}};
static const EnumDef brdtype_enum[] = {
	{"BrdOriginal", BRD_ORIGINAL}, {"BrdStart", BRD_START},
	{"BrdEnd", BRD_END}, {"BrdStartEnd", BRD_STARTEND}, {NULL, 0}};

static int
parse_enum(const EnumDef *t, const char *s, int *out)
{
	long l;

	for (; t->name; t++)
		if (!strcasecmp(t->name, s)) {
			*out = t->value;
			return 0;
		}
	if (parse_num(s, &l) == 0 && l >= 0 && l <= 0xffff) {
		*out = (int)l;
		return 0;
	}
	return -1;
}

static int
config_set_scalar(Config *c, const char *key, const char *val)
{
	size_t i;

	for (i = 0; i < LENGTH(scalar_defs); i++) {
		const ScalarDef *d = &scalar_defs[i];
		char *base = (char *)c + d->off;
		long l;
		double f;
		int b, e = 0, rc = -1;

		if (strcmp(d->name, key))
			continue;
		switch (d->kind) {
		case K_BOOL:
			if ((rc = parse_bool(val, &b)) == 0) {
				*(int *)base = b;
			} else if (parse_num(val, &l) == 0) {
				*(int *)base = l != 0;
				rc = 0;
			}
			break;
		case K_INT:
			if ((rc = parse_num(val, &l)) == 0)
				*(int *)base = (int)MAX(INT_MIN / 2, MIN(INT_MAX / 2, l));
			break;
		case K_UINT:
			if ((rc = parse_num(val, &l)) == 0 && l >= 0)
				*(unsigned *)base = (unsigned)MIN(l, 512);
			else
				rc = -1;
			break;
		case K_DBL:
			if ((rc = parse_double(val, &f)) == 0)
				*(double *)base = f;
			break;
		case K_COLOR:
			rc = parse_color(val, (uint32_t *)base);
			break;
		case K_STR:
			free(*(char **)base);
			*(char **)base = strcmp(val, "NULL") ? estrdup(val) : estrdup("");
			rc = 0;
			break;
		case K_BRDTYPE:
			if ((rc = parse_enum(brdtype_enum, val, &e)) == 0)
				*(int *)base = e;
			break;
		case K_MODKEY:
			if ((rc = parse_modifier_name(val, 0, (uint32_t *)&e)) == 0 &&
			    e != 0)
				*(uint32_t *)base = (uint32_t)e;
			else
				rc = -1;
			break;
		case K_DWTYPE:
			if (!strcasecmp(val, "spiral")) {
				*(int *)base = 1;
				rc = 0;
			} else if (!strcasecmp(val, "dwindle")) {
				*(int *)base = 0;
				rc = 0;
			}
			break;
		case K_SCROLL:
			if ((rc = parse_enum(scroll_enum, val, &e)) == 0)
				*(int *)base = e;
			break;
		case K_CLICK:
			if ((rc = parse_enum(click_enum, val, &e)) == 0)
				*(int *)base = e;
			break;
		case K_SENDEV:
			if ((rc = parse_enum(sendev_enum, val, &e)) == 0)
				*(int *)base = e;
			break;
		case K_ACCEL:
			if ((rc = parse_enum(accel_enum, val, &e)) == 0)
				*(int *)base = e;
			break;
		case K_BTNMAP:
			if ((rc = parse_enum(btnmap_enum, val, &e)) == 0)
				*(int *)base = e;
			break;
		}
		return rc == 0 ? 1 : -1;
	}
	return 0;
}

#endif
