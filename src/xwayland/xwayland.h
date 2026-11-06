/* XWayland is run by neuswc; X11 windows are ordinary swc windows. */
#ifndef OFWC_XWAYLAND_XWAYLAND_H
#define OFWC_XWAYLAND_XWAYLAND_H

static void
xwayland_report(void)
{
	const char *d = getenv("DISPLAY");

	if (d && *d)
		ofwc_log("XWayland: DISPLAY=%s", d);
	else
		ofwc_log("XWayland: not available (neuswc built without xwayland?)");
}

#endif
