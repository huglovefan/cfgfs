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
#include "lua.h"
#include "macros.h"

// enhancement:
// should put the window title comparison in C
// no need to use lua except for firing the event

// builtin.lua calls our C function to set this
_Atomic(enum game_window_activeness) game_window_is_active = activeness_unknown;

// -----------------------------------------------------------------------------

static int msgpipe[2] = {-1, -1};

enum msg_action {
	msg_exit = 1,
};

#define msg_write(c) ({ char c_ = (c); write(msgpipe[1], &c_, 1); })
#define msg_read()   ({ char c = 0; read(msgpipe[0], &c, 1); c; })

// -----------------------------------------------------------------------------

#define POLLNOTGOOD (POLLERR|POLLHUP|POLLNVAL)

__attribute__((cold))
static bool wait_for_event(int conn) {
	bool success = false;

	struct pollfd fds[2] = {
		{.fd = conn,       .events = POLLIN},
		{.fd = msgpipe[0], .events = POLLIN},
	};
	for (;;) {
		int rv = poll(fds, 2, -1);
		if (unlikely(rv == -1)) {
			if (likely(errno == EINTR)) continue;
			eprintln("attention: poll: %s", strerror(errno));
			break;
		}
		if (unlikely(fds[0].revents & POLLNOTGOOD)) break;
		if (unlikely(fds[1].revents & POLLNOTGOOD)) break;
		if (unlikely(fds[1].revents & POLLIN)) {
			switch (msg_read()) {
			case msg_exit:
				goto out;
			default:
				assert(false);
			}
		}
		if (likely(fds[0].revents & POLLIN)) {
			success = true;
			goto out;
		}
	}
out:
	return success;
}

static int l_update_attention(lua_State *L);
static Display *display;

__attribute__((cold))
static void do_xevents(Atom net_active_window,
                       Atom net_wm_name,
                       lua_State *L) {
	XEvent event;
	int cnt = 0;
	bool did_attention = false;
again:
	if (!XPending(display)) return;
	XNextEvent(display, &event);

	if (event.type == 28 &&
	    event.xproperty.atom == net_active_window &&
	    !did_attention++)
	{
		Window focus;
		int revert;
		XGetInputFocus(display, &focus, &revert);

		XTextProperty prop;
		XGetTextProperty(display, focus, &prop, net_wm_name);

		LUA_LOCK();
		 lua_pushcfunction(L, l_update_attention);
		  lua_getglobal(L, "_attention");
		   lua_pushstring(L, (const char *)prop.value);
		  lua_call(L, 1, 1);
		lua_call(L, 1, 0);
		LUA_UNLOCK();

		if (prop.value) XFree(prop.value);
	}

	if (++cnt <= 10) goto again;
}

__attribute__((cold))
static void *attention_main(void *ud) {
	set_thread_name("attention");
	lua_State *L = ud;

	Atom net_active_window = XInternAtom(display, "_NET_ACTIVE_WINDOW", 0);
	Atom net_wm_name = XInternAtom(display, "_NET_WM_NAME", 0);

	XSetWindowAttributes attr;
	attr.event_mask = PropertyChangeMask;
	XChangeWindowAttributes(display, DefaultRootWindow(display), CWEventMask, &attr);

	int conn = ConnectionNumber(display);
	assert(conn >= 0);

	do {
		do_xevents(net_active_window, net_wm_name, L);
	} while (wait_for_event(conn));

	return NULL;
}

// -----------------------------------------------------------------------------

static pthread_t thread;

__attribute__((cold))
void attention_init(void *L) {
	if (thread != 0) return;
	if (getenv("CFGFS_NO_ATTENTION")) return;

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
	    pthread_create(&thread, NULL, attention_main, (void *)L),
	    "attention: pthread_create",
	    goto err);

	return;
err:
	if (msgpipe[0] != -1) close(exchange(msgpipe[0], -1));
	if (msgpipe[1] != -1) close(exchange(msgpipe[1], -1));
	if (display != NULL) XCloseDisplay(exchange(display, NULL));
	thread = 0;
}

__attribute__((cold))
void attention_deinit(void) {
	if (thread == 0) return;

	msg_write(msg_exit);

	pthread_join(thread, NULL);

	close(exchange(msgpipe[0], -1));
	close(exchange(msgpipe[1], -1));

	XCloseDisplay(exchange(display, NULL));

	thread = 0;
}

// -----------------------------------------------------------------------------

#define display dpy /* silence -Wshadow. can't use the global as it is not thread-safe */

// get the title of the active window
// (not performance-critical)
__attribute__((cold))
static char *get_attention(void) {
	Display *display;
	char *rv = NULL;

	display = XOpenDisplay(NULL);
	if (display == NULL) goto out;

	Atom net_wm_name = XInternAtom(display, "_NET_WM_NAME", 0);

	Window focus;
	int revert;
	XGetInputFocus(display, &focus, &revert);

	XTextProperty prop;
	XGetTextProperty(display, focus, &prop, net_wm_name);

	if (prop.value) {
		rv = strdup((const char *)prop.value);
		XFree(prop.value);
	}
out:
	if (display) XCloseDisplay(display);
	return rv;
}

// -----------------------------------------------------------------------------

// get the title of the active window
static int l_get_attention(lua_State *L) {
	char *s = get_attention();
	lua_pushstring(L, s);
	free(s);
	return 1;
}

// set the C global "game_window_is_active" (true, false, nil)
static int l_update_attention(lua_State *L) {
	int oldval = game_window_is_active;
	int newval;
	if (!lua_isnil(L, 1)) {
		newval = (lua_toboolean(L, 1))
		         ? activeness_active
		         : activeness_inactive;
	} else {
		newval = activeness_unknown;
	}
VV	if (newval != oldval) eprintln("attention: %d -> %d", oldval, newval);
	game_window_is_active = newval;
	return 0;
}

__attribute__((cold))
void attention_init_lua(void *L) {
	if (!getenv("CFGFS_NO_ATTENTION")) {
		 lua_pushcfunction(L, l_get_attention);
		lua_setglobal(L, "_get_attention");
		 lua_pushcfunction(L, l_update_attention);
		lua_setglobal(L, "_update_attention");
	} else {
		 lua_pushcfunction(L, lua_do_nothing);
		lua_setglobal(L, "_get_attention");
		 lua_pushcfunction(L, lua_do_nothing);
		lua_setglobal(L, "_update_attention");
	}
}
