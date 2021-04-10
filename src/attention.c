#include "attention.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/prctl.h>
#include <unistd.h>

#include <X11/Xutil.h>

#include <lua.h>

#include "cli_output.h"
#include "click.h"
#include "lua.h"
#include "macros.h"
#include "pipe_io.h"
#include "xlib.h"

_Atomic(_Bool) game_window_is_active = 0;

// -----------------------------------------------------------------------------

static int msgpipe[2] = {-1, -1};

enum msg {
	msg_exit = 1,
};

// -----------------------------------------------------------------------------

static Display *display;
static const char *game_window_title = NULL;
#define window_title_fmt "%s - OpenGL"

static bool wait_for_event(int conn) {
	switch (rdselect(msgpipe[0], conn)) {
	case 2:
		return true;
	case 1:
		switch (readch(msgpipe[0])) {
		case msg_exit:
			return false;
		}
		assert_unreachable();
	case 0:
		perror("attention: rdselect");
		return false;
	default:
		return false;
	}
}

static void check_attention(Atom net_wm_name) {
	Window focus;
	int revert;
	XGetInputFocus(display, &focus, &revert);

	XTextProperty prop;
	XGetTextProperty(display, focus, &prop, net_wm_name);

	const char *title = (const char *)prop.value;
VV	eprintln("attention: title=\"%s\"", title);
	bool oldattn = game_window_is_active;
	bool newattn = title != NULL && 0 == strcmp(title, game_window_title);
	if (newattn != oldattn) {
		game_window_is_active = newattn;
V		eprintln("attention: game_window_is_active=%d", newattn);

		lua_State *L = lua_get_state("attention");
		if (L == NULL) goto lua_done;
		 lua_getglobal(L, "_attention");
		  lua_pushboolean(L, newattn);
		lua_call(L, 1, 0);
		lua_release_state_and_click(L);
lua_done:;
	}

	if (prop.value) XFree(prop.value);
}

static bool did_active_window_change(Atom net_active_window) {
	int cnt = 0;
	bool attention_changed = false;

	while (++cnt <= 10 && XPending(display)) {
		XEvent event;
		XNextEvent(display, &event);
		if (event.type == 28 && event.xproperty.atom == net_active_window) {
			attention_changed = true;
		}
	}

	return attention_changed;
}

static void *attention_main(void *ud) {
	(void)ud;
	set_thread_name("attention");

	Atom net_active_window = XInternAtom(display, "_NET_ACTIVE_WINDOW", 0);
	Atom net_wm_name = XInternAtom(display, "_NET_WM_NAME", 0);

	XSetWindowAttributes attr;
	attr.event_mask = PropertyChangeMask;
	XChangeWindowAttributes(display, DefaultRootWindow(display), CWEventMask, &attr);

	int conn = ConnectionNumber(display);
	assert(conn >= 0);

	char *title;
	if (unlikely(-1 == asprintf(&title, window_title_fmt, getenv("GAMENAME")))) {
		perror("attention: malloc");
		goto out_no_title;
	}
	game_window_title = title;
VV	eprintln("attention: game_window_title=\"%s\"", game_window_title);

	check_attention(net_wm_name);
	while (wait_for_event(conn)) {
		if (did_active_window_change(net_active_window)) {
			check_attention(net_wm_name);
		}
	}
	free(exchange(title, NULL));
	game_window_title = NULL;
out_no_title:

	return NULL;
}

// -----------------------------------------------------------------------------

static pthread_t thread;

void attention_init(void) {
	if (thread != 0) return;
	if (!getenv("GAMENAME")) return;

	display = XOpenDisplay(NULL);
	if (display == NULL) {
		eprintln("attention: failed to open display!");
		goto err;
	}
	XSetErrorHandler(cfgfs_handle_xerror);

	check_minus1(
	    pipe(msgpipe),
	    "attention: pipe",
	    goto err);

	check_errcode(
	    pthread_create(&thread, NULL, attention_main, NULL),
	    "attention: pthread_create",
	    goto err);

	return;
err:
	if (msgpipe[0] != -1) close(exchange(msgpipe[0], -1));
	if (msgpipe[1] != -1) close(exchange(msgpipe[1], -1));
	if (display != NULL) XCloseDisplay(exchange(display, NULL));
	thread = 0;
}

void attention_deinit(void) {
	if (thread == 0) return;

	writech(msgpipe[1], msg_exit);

	struct timespec ts = {0};
	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec += 1;
	int err = pthread_timedjoin_np(thread, NULL, &ts);
	if (err != 0) {
		return;
	}

	close(exchange(msgpipe[0], -1));
	close(exchange(msgpipe[1], -1));

	XCloseDisplay(exchange(display, NULL));

	thread = 0;
}
