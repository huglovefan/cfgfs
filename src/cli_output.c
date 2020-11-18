#define _GNU_SOURCE // unlocked_stdio. there's no vfprintf_unlocked though
#include "cli_output.h"

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wstrict-prototypes"
  #include <readline.h>
#pragma GCC diagnostic pop

#include "cli_input.h"
#include "macros.h"

static pthread_mutex_t output_lock = PTHREAD_MUTEX_INITIALIZER;

// -----------------------------------------------------------------------------

void println(const char *fmt, ...) {
	cli_lock_output();
	va_list args;
	va_start(args, fmt);
	 vfprintf(stdout, fmt, args);
	va_end(args);
	fputc_unlocked('\n', stdout);
	cli_unlock_output();
}

void eprintln(const char *fmt, ...) {
	cli_lock_output();
	va_list args;
	va_start(args, fmt);
	 vfprintf(stderr, fmt, args);
	va_end(args);
	fputc_unlocked('\n', stderr);
	cli_unlock_output();
}

__attribute__((cold))
void perror(const char *s) {
	if (likely(s != NULL && *s != '\0')) {
		eprintln("%s: %s", s, strerror(errno));
	} else {
		eprintln("%s", strerror(errno));
	}
}

// -----------------------------------------------------------------------------

// https://stackoverflow.com/a/5070889

static struct {
	char *line_buffer;
	int end; // line length (or the enum below)
	int point; // cursor position
	size_t buffer_alloc;
} saved;
enum {
	restore_nothing = -1,
	restore_empty_line = 0,
};

__attribute__((cold))
static void ensure_buffer(size_t sz) {
	do {
		saved.buffer_alloc = (saved.buffer_alloc) ? saved.buffer_alloc*2 : 128;
		saved.line_buffer = realloc(saved.line_buffer, saved.buffer_alloc);
	} while (unlikely(saved.buffer_alloc < sz));
}

void cli_lock_output(void) {
	pthread_mutex_lock(&output_lock);
	if (likely(cli_reading_line)) {
		saved.end = rl_end;
D		assert(saved.end >= 0);
		if (unlikely(saved.end != restore_empty_line)) {
			size_t len = (size_t)saved.end;
			if (unlikely(saved.buffer_alloc < len+1)) {
				ensure_buffer(len+1);
			}
			memcpy(saved.line_buffer, rl_line_buffer, len+1);
			saved.point = rl_point;
			rl_replace_line("", 0);
		}
		rl_save_prompt();
		rl_redisplay();
	} else {
		saved.end = restore_nothing;
	}
}
void cli_unlock_output(void) {
	if (likely(saved.end != restore_nothing)) {
		if (unlikely(saved.end != restore_empty_line)) {
			rl_replace_line(saved.line_buffer, 0);
			rl_point = saved.point;
		}
		rl_restore_prompt();
		rl_redisplay();
	}
	pthread_mutex_unlock(&output_lock);
}

// -----------------------------------------------------------------------------

void cli_lock_output_nosave(void) {
	pthread_mutex_lock(&output_lock);
}
void cli_unlock_output_norestore(void) {
	pthread_mutex_unlock(&output_lock);
}
