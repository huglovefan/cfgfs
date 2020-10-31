#include "attention.h"
#include <pthread.h>
#include <string.h>
#include <sys/prctl.h>

#include <X11/Xutil.h>

#include <lua.h>

#include "cli_output.h"
#include "macros.h"
#include "lua.h"

// -----------------------------------------------------------------------------

// this is bad but it's only called like once
__attribute__((cold))
char *get_attention(void) {
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

	if (prop.value) rv = strdup((const char *)prop.value);
out:
	if (display) XCloseDisplay(display);
	return rv;
}

// -----------------------------------------------------------------------------

__attribute__((cold))
static void *attention_main(void *ud) {
	lua_State *L = ud;
	set_thread_name("attention");

	Display *display = XOpenDisplay(NULL);
	if (display == NULL) {
		eprintln("attention: failed to open display!");
		goto out;
	}

	Atom net_active_window = XInternAtom(display, "_NET_ACTIVE_WINDOW", 0);
	Atom net_wm_name = XInternAtom(display, "_NET_WM_NAME", 0);

	XSetWindowAttributes attr;
	attr.event_mask = PropertyChangeMask;
	XChangeWindowAttributes(display, DefaultRootWindow(display), CWEventMask, &attr);

	for (;;) {
		XEvent event;
		XNextEvent(display, &event);
		if (event.type == 28 && event.xproperty.atom == net_active_window) {
			Window focus;
			int revert;
			XGetInputFocus(display, &focus, &revert);

			XTextProperty prop;
			XGetTextProperty(display, focus, &prop, net_wm_name);

			LUA_LOCK();
			 lua_getglobal(L, "_attention");
			  lua_pushstring(L, (const char *)prop.value);
			lua_call(L, 1, 0);
			LUA_UNLOCK();
		}
	}
out:
	if (display) XCloseDisplay(display);
	return NULL;
}

// -----------------------------------------------------------------------------

static pthread_t thread;

__attribute__((cold))
void attention_init(void *L) {
	if (thread != 0) return;
	int err = pthread_create(&thread, NULL, attention_main, L);
	if (err != 0) {
		thread = 0;
		eprintln("attention: pthread_create: %s", strerror(err));
	}
}
