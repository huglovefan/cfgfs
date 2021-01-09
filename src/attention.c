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

_Atomic(_Bool) game_window_is_active = 0;

// -----------------------------------------------------------------------------

static int msgpipe[2] = {-1, -1};

enum msg {
	msg_exit = 1,
};

// -----------------------------------------------------------------------------

static Display *display;
static const char *game_window_title = NULL;
#define window_title_suffix " - OpenGL"

static bool wait_for_event(int conn) {
	switch (rdselect(msgpipe[0], conn)) {
	case 2:
		return true;
	case 1:
		switch (readch(msgpipe[0])) {
		case msg_exit:
			return false;
		}
	case 0:
		perror("attention: rdselect");
		return false;
	default:
		return false;
	}
}

static void do_xevents(Atom net_active_window,
                       Atom net_wm_name) {
	XEvent event;
	int cnt = 0;
	bool did_call_lua = false;

	while (++cnt <= 10 && XPending(display)) {
		XNextEvent(display, &event);

		if (event.type == 28 &&
		    event.xproperty.atom == net_active_window &&
		    !did_call_lua++)
		{
			Window focus;
			int revert;
			XGetInputFocus(display, &focus, &revert);

			XTextProperty prop;
			XGetTextProperty(display, focus, &prop, net_wm_name);

			const char *title = (const char *)prop.value;
VV			eprintln("attention: title=\"%s\"", title);
			bool oldattn = game_window_is_active;
			bool newattn = title != NULL && 0 == strcmp(title, game_window_title);
			if (newattn != oldattn) {
				game_window_is_active = newattn;
VV				eprintln("attention: game_window_is_active=%d", newattn);

				lua_State *L = lua_get_state();
				 lua_getglobal(L, "_attention");
				  lua_pushboolean(L, newattn);
				lua_call(L, 1, 0);
				lua_release_state_and_click(L);
			}

			if (prop.value) XFree(prop.value);
		}
	}
}

__attribute__((cold))
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

	const char *gamename = getenv("GAMENAME");
	size_t gnlen = strlen(gamename);
	char title[gnlen+strlen(window_title_suffix)+1];
	sprintf(title, "%s%s", gamename, window_title_suffix);
	game_window_title = title;
VV	eprintln("attention: game_window_title=\"%s\"", game_window_title);

	do {
		do_xevents(net_active_window, net_wm_name);
	} while (wait_for_event(conn));

	game_window_title = NULL;

	return NULL;
}

// -----------------------------------------------------------------------------

static pthread_t thread;

void attention_init(void) {
	if (thread != 0) return;
	if (!getenv("GAMENAME")) {
		eprintln("attention: unknown game, exiting");
		return;
	}

	display = XOpenDisplay(NULL);
	if (display == NULL) {
		eprintln("attention: failed to open display!");
		goto err;
	}

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

	pthread_join(thread, NULL);

	close(exchange(msgpipe[0], -1));
	close(exchange(msgpipe[1], -1));

	XCloseDisplay(exchange(display, NULL));

	thread = 0;
}
