# ofwc

Only Fibonacci Wayland Compositor, built on neuswc (a dwc fork).
One layout, dwindle (or spiral); `togglefloating` floats a window inside it.
Python config with hot reload, XWayland, status bar via [nibar](https://github.com/nszmak35/nibar).

## Build

Needs neuswc (bundled in `neulibs/`) with `swc-launch`,
wayland (+ `wayland-scanner`), xkbcommon, libinput, Python 3 (dev files), GNU make.

    make
    sudo make install

Installs `ofwc`, `ofwcctl`, the `ofwc` session for display managers and `/etc/ofwc/config.py`.

## Run

Pick the **ofwc** session, or run `ofwc` from a TTY.

    ofwc [-c config.py] [-s command] [-d] [-v]

## Config

`~/.config/ofwc/config.py` is created from `default/config.py` on first start.
It reloads when saved (or `pkill -HUP ofwc`); an invalid file keeps the old config.
Lists you set (`keys`, `buttons`, `rules`, `tags`, `autostart`, `monrules`) replace the built-in ones.

Actions: `spawn quit killclient togglefloating togglefullscreen togglegaps togglebar
focusstack focusdir swapdir zoom setmfact view toggleview tag toggletag cycletag
focusmon tagmon moveresize`.

## Bar and IPC

ofwc serves `dwl-ipc-unstable-v2` (tags, focused window, fullscreen/floating) and
`ofwc-ipc-unstable-v1` (keyboard layouts, compositor actions). nibar and waybar's dwl
modules use them.

`ofwcctl` is a command line client of the same IPC:

    ofwcctl set-workspace <number>
    ofwcctl set-kblayout              next keyboard layout
    ofwcctl watch-kblayout
    ofwcctl watch-monitor
    ofwcctl watch-workspace <number>
    ofwcctl watch-focus
    ofwcctl watch-all

`watch-*` print one JSON line (one layout name for `watch-kblayout`) per change.

## Troubleshooting

* Crashes are logged to `~/.cache/ofwc/crash.log`.
* Dead keyboard after a crash: `Alt+SysRq+R`, `Ctrl+Alt+F3`, then
  `sudo kbd_mode -u -C /dev/tty2; sudo chvt 1` (tty2: the stuck VT).
* Default terminal is foot (`footclient`, `foot --server` in `autostart`).

## License

MIT, based on dwc (see `LICENSE`).
