/* Config reload on file change (inotify). */
#ifndef OFWC_CONFIG_CONFIG_WATCH_H
#define OFWC_CONFIG_CONFIG_WATCH_H

#include <sys/inotify.h>

#define CFGWATCH_DEBOUNCE_MS 150
#define CFGWATCH_MAXDIRS 2

static int cfgw_fd = -1;
static struct wl_event_source *cfgw_src;
static struct wl_event_source *cfgw_timer;
static int cfgw_wd[CFGWATCH_MAXDIRS] = {-1, -1};
static char *cfgw_name[CFGWATCH_MAXDIRS];
static int cfgw_n;

static int
cfgw_timer_tick(void *data)
{
	(void)data;
	request_reload();
	return 0;
}

static int
cfgw_matches(const char *name)
{
	int i;

	for (i = 0; i < cfgw_n; i++)
		if (cfgw_name[i] && !strcmp(cfgw_name[i], name))
			return 1;
	return 0;
}

static int
cfgw_readable(int fd, uint32_t mask, void *data)
{
	char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
	ssize_t n;
	char *p;
	int matched = 0;

	(void)data;
	if (!(mask & WL_EVENT_READABLE))
		return 0;
	while ((n = read(fd, buf, sizeof(buf))) > 0) {
		for (p = buf; p < buf + n;) {
			struct inotify_event *ev = (struct inotify_event *)p;

			if ((ev->mask & (IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE)) &&
			    ev->len > 0 && cfgw_matches(ev->name))
				matched = 1;
			p += sizeof(struct inotify_event) + ev->len;
		}
	}
	if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
		ofwc_log("config watch: read failed: %s", strerror(errno));
	if (matched && cfgw_timer)
		wl_event_source_timer_update(cfgw_timer, CFGWATCH_DEBOUNCE_MS);
	return 0;
}

static void
cfgw_add(const char *path)
{
	char dir[PATH_MAX];
	const char *slash = strrchr(path, '/');
	size_t dlen;
	int wd, i;

	if (!slash || cfgw_n >= CFGWATCH_MAXDIRS)
		return;
	dlen = slash == path ? 1 : (size_t)(slash - path);
	if (dlen >= sizeof(dir) || !slash[1])
		return;
	memcpy(dir, path, dlen);
	dir[dlen] = '\0';

	wd = inotify_add_watch(cfgw_fd, dir, IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE);
	if (wd < 0) {
		ofwc_log("config watch: cannot watch %s: %s", dir, strerror(errno));
		return;
	}
	for (i = 0; i < cfgw_n; i++)
		if (cfgw_wd[i] == wd && !strcmp(cfgw_name[i], slash + 1))
			return;
	cfgw_wd[cfgw_n] = wd;
	cfgw_name[cfgw_n] = estrdup(slash + 1);
	cfgw_n++;
}

static void
config_watch_stop(void)
{
	int i;

	if (cfgw_src) {
		wl_event_source_remove(cfgw_src);
		cfgw_src = NULL;
	}
	if (cfgw_timer) {
		wl_event_source_remove(cfgw_timer);
		cfgw_timer = NULL;
	}
	if (cfgw_fd >= 0) {
		close(cfgw_fd);
		cfgw_fd = -1;
	}
	for (i = 0; i < cfgw_n; i++) {
		free(cfgw_name[i]);
		cfgw_name[i] = NULL;
		cfgw_wd[i] = -1;
	}
	cfgw_n = 0;
}

static void
config_watch_init(void)
{
	char *path = user_config_path(cli_config);
	char real[PATH_MAX];

	if (!path)
		return;
	cfgw_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (cfgw_fd < 0) {
		ofwc_log("config watch: inotify_init1 failed: %s, no automatic reload",
		         strerror(errno));
		free(path);
		return;
	}
	cfgw_add(path);
	if (realpath(path, real) && strcmp(real, path))
		cfgw_add(real);
	if (cfgw_n == 0)
		goto fail;
	cfgw_timer = wl_event_loop_add_timer(loop, cfgw_timer_tick, NULL);
	if (!cfgw_timer)
		goto fail;
	cfgw_src = wl_event_loop_add_fd(loop, cfgw_fd, WL_EVENT_READABLE, cfgw_readable, NULL);
	if (!cfgw_src)
		goto fail;
	ofwc_dbg("watching %s for changes", path);
	free(path);
	return;

fail:
	ofwc_log("config watch: setup failed, no automatic reload");
	config_watch_stop();
	free(path);
}

#endif
