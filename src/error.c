#include "error.h"

#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#if defined(__linux__) || defined(__FreeBSD__)
 #include <execinfo.h>
#endif

#if defined(__CYGWIN__)
 #define WIN32_LEAN_AND_MEAN
 #include <windows.h>
 #include <dbghelp.h>
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

// backtrace() returns int on linux, size_t on freebsd
#if defined(__linux__)
 #define BT_RV_T   int
 #define BT_RV_FMT "%d"
#elif defined(__FreeBSD__)
 #define BT_RV_T   size_t
 #define BT_RV_FMT "%zu"
#endif

#if defined(__linux__) || defined(__FreeBSD__)
#define bt_depth 64
	void *buffer[bt_depth];
	BT_RV_T nptrs = backtrace(buffer, bt_depth);
	if (1 != nptrs) fprintf(stderr, "backtrace() returned " BT_RV_FMT " addresses\n", nptrs);
	else            fprintf(stderr, "backtrace() returned 1 address\n");
	backtrace_symbols_fd(buffer, nptrs, STDERR_FILENO);
#endif

#if defined(__CYGWIN__)
	// https://stackoverflow.com/a/5699483
	// almost useful but it doesn't show function names from cfgfs.exe
	// apparently it needs debug info in .pdb format but cygwin can't make that

	// https://docs.microsoft.com/en-us/windows/win32/api/dbghelp/nf-dbghelp-syminitialize
	// > All DbgHelp functions, such as this one, are single threaded. Therefore, calls from more than one thread to this function will likely result in unexpected behavior or memory corruption.
	static pthread_mutex_t dbghelp_lock = PTHREAD_MUTEX_INITIALIZER;
	pthread_mutex_lock(&dbghelp_lock);

	HANDLE *process = GetCurrentProcess();

	static _Bool did_sym_initialize = 0;
	if (!did_sym_initialize++) SymInitialize(process, NULL, TRUE);

	void *stack[100];
	int frames = CaptureStackBackTrace(0, 100, stack, NULL);

	SYMBOL_INFO *symbol = calloc(sizeof(SYMBOL_INFO) + 256, 1);
	symbol->MaxNameLen = 255;
	symbol->SizeOfStruct = sizeof(SYMBOL_INFO);

	for (int i = 0; i < frames; i++) {
		SymFromAddr(process, (DWORD64)(stack[i]), 0, symbol);
		fprintf(stderr, "%i: %s - 0x%0llX\n", frames - i - 1, symbol->Name, symbol->Address);
	}

	free(symbol);
	pthread_mutex_unlock(&dbghelp_lock);
#endif
}
