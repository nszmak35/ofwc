/* Session: swc-launch start, child processes, signals, clean exit. */
#ifndef OFWC_SHELL_SESSION_H
#define OFWC_SHELL_SESSION_H

#include <fcntl.h>
#ifdef __linux__
#include <linux/kd.h>
#include <linux/vt.h>
#include <sys/ioctl.h>
#endif
#ifdef __GLIBC__
#include <execinfo.h>
#endif

static pid_t *kids;
static size_t nkids, ckids;
static int shutting_down;

/* A crash must never leave a dead screen: log, then exit so swc-launch restores the VT. */

static int crash_fd = -1;

static void
crash_puts(const char *s)
{
	size_t n = strlen(s);

	if (write(2, s, n) < 0) {}
	if (crash_fd >= 0 && write(crash_fd, s, n) < 0) {}
}

static void
crash_hex(unsigned long v)
{
	char buf[2 + sizeof(v) * 2 + 1];
	int i = (int)sizeof(buf) - 1;

	buf[i] = '\0';
	do {
		buf[--i] = "0123456789abcdef"[v & 15];
		v >>= 4;
	} while (v && i > 2);
	buf[--i] = 'x';
	buf[--i] = '0';
	crash_puts(buf + i);
}

static void
crash_handler(int sig, siginfo_t *si, void *uc)
{
	static volatile sig_atomic_t entered;

	(void)uc;
	if (entered++)
		_exit(128 + sig);
	crash_puts("\nofwc: FATAL signal ");
	crash_hex((unsigned long)sig);
	crash_puts(" fault address ");
	crash_hex((unsigned long)(si ? si->si_addr : NULL));
	crash_puts("\n");
#ifdef __GLIBC__
	{
		void *bt[48];
		int n = backtrace(bt, 48);

		backtrace_symbols_fd(bt, n, 2);
		if (crash_fd >= 0)
			backtrace_symbols_fd(bt, n, crash_fd);
	}
#endif
	if (crash_fd >= 0) {
		int m = open("/proc/self/maps", O_RDONLY);
		char buf[512];
		ssize_t n;

		if (m >= 0) {
			while ((n = read(m, buf, sizeof(buf))) > 0)
				if (write(crash_fd, buf, (size_t)n) < 0)
					break;
			close(m);
		}
		if (fsync(crash_fd) < 0) {}
	}
	_exit(128 + sig);
}

static void
session_crash_init(void)
{
	static char altstack[64 * 1024];
	struct sigaction sa;
	stack_t ss;
	const char *cache = getenv("XDG_CACHE_HOME"), *home = getenv("HOME");
	char path[PATH_MAX + 16], dir[PATH_MAX];
	int sigs[] = {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT};
	size_t i;

	if (cache && cache[0] == '/')
		snprintf(dir, sizeof(dir), "%s/ofwc", cache);
	else if (home && *home)
		snprintf(dir, sizeof(dir), "%s/.cache/ofwc", home);
	else
		snprintf(dir, sizeof(dir), "/tmp");
	if (strcmp(dir, "/tmp")) {
		char parent[PATH_MAX], *slash;

		snprintf(parent, sizeof(parent), "%s", dir);
		if ((slash = strrchr(parent, '/'))) {
			*slash = '\0';
			mkdir(parent, 0755);
		}
		mkdir(dir, 0755);
	}
	snprintf(path, sizeof(path), "%s/crash.log", dir);
	crash_fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);

	ss.ss_sp = altstack;
	ss.ss_size = sizeof(altstack);
	ss.ss_flags = 0;
	sigaltstack(&ss, NULL);
	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = crash_handler;
	sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
	sigemptyset(&sa.sa_mask);
	for (i = 0; i < LENGTH(sigs); i++)
		sigaction(sigs[i], &sa, NULL);
}

static void
session_repair_vt(void)
{
#ifdef __linux__
	const char *n = getenv("XDG_VTNR");
	char path[32];
	struct vt_mode vm;
	long kb = 0, ks = 0;
	int fd;

	if (!n || !*n || strlen(n) > 3 || strspn(n, "0123456789") != strlen(n) || atoi(n) < 1)
		return;
	snprintf(path, sizeof(path), "/dev/tty%s", n);
	fd = open(path, O_RDWR | O_NOCTTY | O_CLOEXEC);
	if (fd < 0)
		return;
	if (ioctl(fd, VT_GETMODE, &vm) == 0 && vm.mode == VT_AUTO) {
		if (ioctl(fd, KDGKBMODE, &kb) == 0 &&
		    (kb == K_OFF || kb == K_RAW || kb == K_MEDIUMRAW))
			ioctl(fd, KDSKBMODE, K_UNICODE);
		if (ioctl(fd, KDGETMODE, &ks) == 0 && ks == KD_GRAPHICS)
			ioctl(fd, KDSETMODE, KD_TEXT);
	}
	close(fd);
#endif
}

static void
session_ensure_launcher(int argc, char **argv)
{
	char exe[PATH_MAX];
	char **nargv;
	ssize_t n;
	int i;

	if (getenv("SWC_LAUNCH_SOCKET"))
		return;
	if (getenv("OFWC_LAUNCHED"))
		die("swc-launch did not provide a launch socket");
	setenv("OFWC_LAUNCHED", "1", 1);
	session_repair_vt();
	n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
	if (n > 0)
		exe[n] = '\0';
	else
		snprintf(exe, sizeof(exe), "%s", argv[0]);
	nargv = ecalloc((size_t)argc + 4, sizeof(char *));
	nargv[0] = "swc-launch";
	nargv[1] = "--";
	nargv[2] = exe;
	for (i = 1; i < argc; i++)
		nargv[i + 2] = argv[i];
	execvp("swc-launch", nargv);
	die("cannot execute swc-launch: %s", strerror(errno));
}

static void
track_pid(pid_t p)
{
	size_t i;

	for (i = 0; i < nkids; i++)
		if (kids[i] == p)
			return;
	kids = grow(kids, &ckids, nkids, sizeof(pid_t));
	kids[nkids++] = p;
}

static void
untrack_pid(pid_t p)
{
	size_t i;

	for (i = 0; i < nkids; i++) {
		if (kids[i] == p) {
			kids[i] = kids[--nkids];
			return;
		}
	}
}

static void
child_reset(void)
{
	sigset_t set;

	sigemptyset(&set);
	sigprocmask(SIG_SETMASK, &set, NULL);
	signal(SIGPIPE, SIG_DFL);
	signal(SIGINT, SIG_DFL);
	signal(SIGTERM, SIG_DFL);
	signal(SIGHUP, SIG_DFL);
	signal(SIGQUIT, SIG_DFL);
	signal(SIGCHLD, SIG_DFL);
	signal(SIGALRM, SIG_DFL);
}

/* Children stay in our process group so swc-launch can end them with the session. */
static void
spawn_cmd(const char *cmd)
{
	pid_t pid;

	if (!cmd || !*cmd || shutting_down)
		return;
	pid = fork();
	if (pid < 0) {
		ofwc_log("fork failed: %s", strerror(errno));
		return;
	}
	if (pid == 0) {
		int nul = open("/dev/null", O_RDONLY);

		if (nul >= 0) {
			dup2(nul, 0);
			if (nul > 2)
				close(nul);
		}
		child_reset();
		execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
		_exit(127);
	}
	track_pid(pid);
}

static int
on_sigchld(int sig, void *data)
{
	pid_t pid;
	int status;

	(void)sig;
	(void)data;
	while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
		untrack_pid(pid);
	return 0;
}

static int
on_sigterm(int sig, void *data)
{
	(void)sig;
	(void)data;
	running = 0;
	wl_display_terminate(dpy);
	return 0;
}

static int
on_sighup(int sig, void *data)
{
	(void)sig;
	(void)data;
	request_reload();
	return 0;
}

static void
session_setup_signals(void)
{
	signal(SIGPIPE, SIG_IGN);
	wl_event_loop_add_signal(loop, SIGCHLD, on_sigchld, NULL);
	wl_event_loop_add_signal(loop, SIGINT, on_sigterm, NULL);
	wl_event_loop_add_signal(loop, SIGTERM, on_sigterm, NULL);
	wl_event_loop_add_signal(loop, SIGQUIT, on_sigterm, NULL);
	wl_event_loop_add_signal(loop, SIGHUP, on_sighup, NULL);
}

static void
session_autostart(const char *startup)
{
	int i;

	setenv("XDG_SESSION_TYPE", "wayland", 0);
	setenv("XDG_CURRENT_DESKTOP", "ofwc", 0);
	for (i = 0; i < cfg.nautostart; i++)
		spawn_cmd(cfg.autostart[i]);
	if (startup)
		spawn_cmd(startup);
}

static void
hard_exit(int sig)
{
	(void)sig;
	_exit(0);
}

static void
msleep(long ms)
{
	struct timespec ts = {ms / 1000, (ms % 1000) * 1000000L};

	nanosleep(&ts, NULL);
}

/* Shutdown must not block or crash: a watchdog and fault handlers force a plain exit. */
static void
session_shutdown(void)
{
	size_t i;
	int waited;

	shutting_down = 1;
	signal(SIGALRM, hard_exit);
	signal(SIGSEGV, hard_exit);
	signal(SIGBUS, hard_exit);
	signal(SIGABRT, hard_exit);
	signal(SIGFPE, hard_exit);
	signal(SIGILL, hard_exit);
	alarm(4);

	for (i = 0; i < nkids; i++)
		kill(kids[i], SIGTERM);
	for (waited = 0; waited < 40 && nkids > 0; waited++) {
		pid_t pid;
		int status;

		while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
			untrack_pid(pid);
		if (nkids == 0)
			break;
		msleep(10);
	}
	for (i = 0; i < nkids; i++)
		kill(kids[i], SIGKILL);

	grab_end(0);
	swc_finalize();
	fflush(NULL);
	/* skip wl_display_destroy(): swc already tore its globals down. */
	_exit(0);
}

#endif
