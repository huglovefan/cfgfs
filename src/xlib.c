#include "xlib.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cli_output.h"
#include "error.h"
#include "macros.h"

#if defined(WITH_D) || defined(WITH_V)
 #define should_warn 1
#else
 #define should_warn 0
#endif

int cfgfs_handle_xerror(Display *display, XErrorEvent *error) {
	char msgbuf[128];
	char thread[16];

	XGetErrorText(display, error->error_code, msgbuf, sizeof(msgbuf));
	get_thread_name(thread);

	// attention.c randomly gets this error which needs to be ignored
	/*
	X Error of failed request:  BadWindow (invalid Window parameter)
	  Major opcode of failed request:  20 (X_GetProperty)
	  Resource id in failed request:  0x0
	  Serial number of failed request:  121
	  Current serial number in output stream:  121
	*/
	if (likely(
		0 == strcmp(thread, "attention") &&
		error->request_code == 20 &&
		error->minor_code == 0
	)) {
		if (should_warn) {
			cli_lock_output();
			 fprintf(stderr, "warning: caught harmless X error: %s\n", msgbuf);
			 print_c_backtrace_unlocked();
			cli_unlock_output();
		}
		return 0;
	}

	// are there others?
	// abort on them to match the default behavior and get a core dump

	if (cli_trylock_output_nosave()) {
		cli_save_prompt_locked();
	}

	fprintf(stderr, "error: thread %s got an X error: %s\n", thread, msgbuf);
D	print_c_backtrace_unlocked();
	abort();
	cfgfs_noreturn();
}
