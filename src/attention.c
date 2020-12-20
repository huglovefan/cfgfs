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

#if defined(__cplusplus)
 #include <lua.hpp>
#else
 #include <lua.h>
#endif

#include "cli_output.h"
#include "click.h"
#include "lua.h"
#include "macros.h"

_Atomic(enum attn) game_window_is_active = attn_unknown;

// -----------------------------------------------------------------------------

static int msgpipe[2] = {-1, -1};

enum msg_action {
	msg_exit = 1,
};

#define msg_write(c) ({ char c_ = (c); write(msgpipe[1], &c_, 1); })
#define msg_read()   ({ char c = 0; read(msgpipe[0], &c, 1); c; })

// -----------------------------------------------------------------------------

#define POLLNOTGOOD (POLLERR|POLLHUP|POLLNVAL)

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
			perror("attention: poll");
			break;
		}
		if (unlikely((fds[0].revents|fds[1].revents) & POLLNOTGOOD)) break;
		if (unlikely(fds[1].revents & POLLIN)) {
			switch (msg_read()) {
			case msg_exit:
				goto out;
			default:
D				assert(0);
				__builtin_unreachable();
			}
		}
		if (likely(fds[0].revents & POLLIN)) {
			success = true;
			break;
		}
	}
out:
	return success;
}

static int l_update_attention(lua_State *L);
static Display *display;

static void do_xevents(Atom net_active_window,
                       Atom net_wm_name,
                       lua_State *L) {
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

			LUA_LOCK();
			 lua_pushcfunction(L, l_update_attention);
			  lua_getglobal(L, "_attention");
				lua_pushstring(L, (const char *)prop.value);
			  lua_call(L, 1, 1);
			lua_call(L, 1, 0);
			LUA_UNLOCK();
			// always click, in case the buffer is empty but there are timeouts to do
			do_click();

			if (prop.value) XFree(prop.value);
		}
	}
}

__attribute__((cold))
static void *attention_main(void *L) {
	set_thread_name("attention");

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

void attention_init(void *L) {
	if (thread != 0) return;

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
static char *get_attention(void) {
	char *rv = NULL;

	Display *display = XOpenDisplay(NULL);
	if (display) {
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

		XCloseDisplay(display);
	}
	return rv;
}

// -----------------------------------------------------------------------------

static int l_get_attention(lua_State *L) {
	char *s = get_attention();
	lua_pushstring(L, s);
	free(s);
	return 1;
}

static int l_update_attention(lua_State *L) {
	enum attn newval;
	if (!lua_isnil(L, 1)) {
		newval = (lua_toboolean(L, 1))
		         ? attn_active
		         : attn_inactive;
	} else {
		newval = attn_unknown;
	}
	game_window_is_active = newval;
	return 0;
}

void attention_init_lua(void *L) {
	 lua_pushcfunction(L, l_get_attention);
	lua_setglobal(L, "_get_attention");
	 lua_pushcfunction(L, l_update_attention);
	lua_setglobal(L, "_update_attention");
}
