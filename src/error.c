#include "error.h"

#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "cli_output.h"

static inline int check_fmt(const char *s) {
	int ss = 0;
	for (const char *p = s; *p; p++) {
		if (*p == '%') {
			if (*(p+1) == 's') { p+=1; ss+=1; continue; }
			if (*(p+1) == '%') { p+=1; continue; }
			goto bad;
		}
	}
	if (ss != 2) goto bad;
	return 1;
bad:
	fprintf(stderr, "assert_fail: invalid format string!\n");
	return 0;
}

__attribute__((noreturn))
void assert_fail(const struct assertdata *dt) {
	if (cli_trylock_output_nosave()) {
		cli_save_prompt_locked();
	}

	if (!check_fmt(dt->fmt)) goto fail;
	fprintf(stderr, dt->fmt, dt->func, dt->expr);

	print_c_backtrace_unlocked();
fail:
	abort();
}

void print_c_backtrace_unlocked(void) {
	typedef void (*print_backtrace_t)(void);
	print_backtrace_t fn = (print_backtrace_t)dlsym(RTLD_DEFAULT, "__sanitizer_print_stack_trace");
	if (fn != NULL) fn();
}
