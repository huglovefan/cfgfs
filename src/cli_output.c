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
#include "cli_scrollback.h"
#include "lua.h"
#include "macros.h"

static pthread_mutex_t output_lock = PTHREAD_MUTEX_INITIALIZER;

// -----------------------------------------------------------------------------

void println(const char *fmt, ...) {
	va_list args;
	char *line;

	va_start(args, fmt);
	 int len = vasprintf(&line, fmt, args);
	va_end(args);

	if (unlikely(len == -1)) return;

	cli_lock_output();
	 fwrite_unlocked(line, 1, (size_t)len, stdout);
	 fputc_unlocked('\n', stdout);
	 cli_scrollback_add_output(line);
	cli_unlock_output();
}

void eprintln(const char *fmt, ...) {
	va_list args;
	char *line;

	va_start(args, fmt);
	 int len = vasprintf(&line, fmt, args);
	va_end(args);

	if (unlikely(len == -1)) return;

	cli_lock_output();
	 fwrite_unlocked(line, 1, (size_t)len, stderr);
	 fputc_unlocked('\n', stderr);
	 cli_scrollback_add_output(line);
	cli_unlock_output();
}

__attribute__((minsize))
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
		saved.line_buffer = (char *)realloc(saved.line_buffer, saved.buffer_alloc);
	} while (unlikely(saved.buffer_alloc < sz));
}

void cli_lock_output(void) {
	cli_lock_output_nosave();
	cli_save_prompt_locked();
}
void cli_unlock_output(void) {
	cli_restore_prompt_locked();
	cli_unlock_output_norestore();
}

void cli_save_prompt_locked(void) {
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
void cli_restore_prompt_locked(void) {
	if (likely(saved.end != restore_nothing)) {
		if (unlikely(saved.end != restore_empty_line)) {
			rl_replace_line(saved.line_buffer, 0);
			rl_point = saved.point;
		}
		rl_restore_prompt();
		rl_redisplay();
	}
}

// -----------------------------------------------------------------------------

void cli_lock_output_nosave(void) {
	pthread_mutex_lock(&output_lock);
}
_Bool cli_trylock_output_nosave(void) {
	return (0 == pthread_mutex_trylock(&output_lock));
}
void cli_unlock_output_norestore(void) {
	pthread_mutex_unlock(&output_lock);
}
