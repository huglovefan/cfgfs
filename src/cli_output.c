#define _GNU_SOURCE // unlocked_stdio. there's no vfprintf_unlocked though
#include "cli_output.h"

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

// -----------------------------------------------------------------------------

// https://stackoverflow.com/a/5070889

static struct {
	char *line_buffer;
	int end; // line length. set to -1 if we shouldn't restore it
	int point; // cursor position
} saved;

void cli_lock_output(void) {
	pthread_mutex_lock(&output_lock);
	if (likely(cli_reading_line)) {
		saved.end = rl_end;
		saved.point = rl_point;
		assert(saved.end >= 0);

		saved.line_buffer = realloc(saved.line_buffer, (size_t)saved.end+1);
		memcpy(saved.line_buffer, rl_line_buffer, (size_t)saved.end);
		saved.line_buffer[saved.end] = '\0';

		rl_save_prompt();
		rl_replace_line("", 0);
		rl_redisplay();
	} else {
		saved.end = -1;
	}
}
void cli_unlock_output(void) {
	if (likely(saved.end >= 0)) {
		rl_restore_prompt();
		rl_replace_line(saved.line_buffer, 0);
		rl_point = saved.point;
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
