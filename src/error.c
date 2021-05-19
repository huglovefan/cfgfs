#include "error.h"

#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#if defined(__linux__)
 #include <execinfo.h>
#endif

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

#if defined(__clang__) || defined(__GNUC__)
 #define try_sanitizer 1
#else
 #define try_sanitizer 0
#endif

#if defined(__TINYC__)
 #define try_tcc 1
#else
 #define try_tcc 0
#endif

void print_c_backtrace_unlocked(void) {
	// try the backtrace function from the sanitizer runtime
	// exists if cfgfs was linked with a sanitizer
	typedef void (*sanitizer_backtrace_t)(void);
	if (try_sanitizer) {
		sanitizer_backtrace_t san_bt = (sanitizer_backtrace_t)dlsym(RTLD_DEFAULT, "__sanitizer_print_stack_trace");
		if (san_bt != NULL) {
			san_bt();
			return;
		}
	}

	// try tcc's backtrace function
	// exists if cfgfs was linked by tcc with -bt
	typedef int (*tcc_backtrace_t)(const char *fmt, ...);
	if (try_tcc) {
		tcc_backtrace_t tcc_bt = (tcc_backtrace_t)dlsym(RTLD_DEFAULT, "tcc_backtrace");
		if (tcc_bt != NULL) {
			tcc_bt("");
			return;
		}
	}

#if defined(__linux__)
#define bt_depth 64
	void *buffer[bt_depth];
	int nptrs = backtrace(buffer, bt_depth);
	if (1 != nptrs) fprintf(stderr, "backtrace() returned %d addresses\n", nptrs);
	else            fprintf(stderr, "backtrace() returned 1 address\n");
	backtrace_symbols_fd(buffer, nptrs, STDERR_FILENO);
#endif
}
