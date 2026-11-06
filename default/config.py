# ofwc config: reloaded when saved (an error keeps the old config). Colors are "0xRRGGBBAA".
# A list you set (keys, buttons, rules, tags, autostart, monrules) replaces the built-in one.

class Appearance:
    sloppyfocus = True
    rootcolor = "0x222222ff"
    float_width = 0.60
    float_height = 0.60
    motion_refreshrate = 90

class Gaps:
    gappx = 10
    smartgaps = 0
    gaps = True

class Border_Details:
    border_color_type = "BrdStartEnd"
    borderpx = 7
    urgentcolor = "0xffffffff"
    bordercolor = "0xAFA9A5ff"
    focuscolor = "0xAFA9A5ff"
    borderspx = 3
    borderepx = 2
    borderspx_offset = 2
    borderepx_negative_offset = 0
    borderscolor = "0x65727Aff"
    borderecolor = "0x20272Aff"
    borders_focuscolor = "0x8E9EA7ff"
    borders_urgentcolor = "0xE8E9E8ff"
    bordere_focuscolor = "0x20272Aff"
    bordere_urgentcolor = "0x20272Aff"
    borders_only_floating = False

class Input:
    modkey = "super"
    keyboard_rules = ""
    keyboard_layout = "us,ru"
    keyboard_variant = ""
    keyboard_model = ""
    keyboard_options = "grp:win_space_toggle"
    repeat_rate = 25
    repeat_delay = 600
    tap_to_click = True
    tap_and_drag = True
    drag_lock = True
    natural_scrolling = False
    disable_while_typing = True
    left_handed = False
    middle_button_emulation = False
    scroll_method = "LIBINPUT_CONFIG_SCROLL_2FG"
    click_method = "LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS"
    send_events_mode = "LIBINPUT_CONFIG_SEND_EVENTS_ENABLED"
    accel_profile = "LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE"
    accel_speed = 0.0
    button_map = "LIBINPUT_CONFIG_TAP_MAP_LRM"

class Dwindle_Settings:
    dwindle_type = "dwindle"
    dwindle_attach = False
    dwindle_mirror = False
    dwindle_mouse_client = True   # False: layout follows window order only

# The bar is a separate program: nibar (see autostart).

tags = ["1", "2", "3", "4", "5", "6", "7", "8", "9"]

autostart = [
    "swall -m scaled ~/wallpapers/wallpaper.png",
    "foot --server",
    "nibar",
]

rules = [
    {"appid": "Gimp_EXAMPLE", "workspace": 0, "floating": True, "fullscreen": False, "monitor": -1},
]

# [monitor index or None, mfact]
monrules = [
    [None, 0.5],
]

# [modifiers, key, action, argument]
keys = [
    ["MODKEY", "Return", "spawn", "footclient"],
    ["MODKEY", "p", "spawn", "neumenu_run"],
    ["MODKEY|SHIFT", "e", "quit"],
    ["CTRL|ALT", "Terminate_Server", "quit"],
    ["MODKEY|SHIFT", "q", "killclient"],

    ["MODKEY", "v", "togglefloating"],
    ["MODKEY", "f", "togglefullscreen"],
    ["MODKEY", "g", "togglegaps"],

    ["MODKEY", "j", "focusstack", 1],
    ["MODKEY", "k", "focusstack", -1],
    ["MODKEY", "Left", "focusdir", 0],
    ["MODKEY", "Right", "focusdir", 1],
    ["MODKEY", "Up", "focusdir", 2],
    ["MODKEY", "Down", "focusdir", 3],
    ["MODKEY|CTRL", "Left", "swapdir", 0],
    ["MODKEY|CTRL", "Right", "swapdir", 1],
    ["MODKEY|CTRL", "Up", "swapdir", 2],
    ["MODKEY|CTRL", "Down", "swapdir", 3],
    ["MODKEY|SHIFT", "Return", "zoom"],

    ["MODKEY", "h", "setmfact", -0.05],
    ["MODKEY", "l", "setmfact", 0.05],

    ["MODKEY", "Tab", "view", 0],
    ["MODKEY", "0", "view", "ALL"],
    ["MODKEY|SHIFT", "parenright", "tag", "ALL"],
    ["MODKEY", "comma", "focusmon", -1],
    ["MODKEY", "period", "focusmon", 1],
    ["MODKEY|SHIFT", "less", "tagmon", -1],
    ["MODKEY|SHIFT", "greater", "tagmon", 1],
]

_shifted = ["exclam", "at", "numbersign", "dollar", "percent",
            "asciicircum", "ampersand", "asterisk", "parenleft"]
for i in range(1, 10):
    mask = 1 << (i - 1)
    keys.extend([
        ["MODKEY", str(i), "view", mask],
        ["MODKEY|CTRL", str(i), "toggleview", mask],
        ["MODKEY|SHIFT", _shifted[i - 1], "tag", mask],
        ["MODKEY|CTRL|SHIFT", _shifted[i - 1], "toggletag", mask],
    ])

buttons = [
    ["ClkClient", "MODKEY", "BTN_LEFT", "moveresize", 2],
    ["ClkClient", "MODKEY", "BTN_RIGHT", "moveresize", 3],
    ["ClkClient", "MODKEY", "SCROLL_UP", "cycletag", -1],
    ["ClkClient", "MODKEY", "SCROLL_DOWN", "cycletag", 1],
]
