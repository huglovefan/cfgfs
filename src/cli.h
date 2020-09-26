#pragma once

#include <alloca.h>
#include <stdbool.h>
#include <stdio.h>
#include <pthread.h>

#include "macros.h"

#pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wstrict-prototypes"
  #include <readline.h>
#pragma GCC diagnostic pop

extern pthread_mutex_t cli_mutex;
extern bool _Atomic    cli_reading_line;

// -----------------------------------------------------------------------------

void cli_thread_start(void *L);
void cli_thread_stop(void);

// -----------------------------------------------------------------------------

// https://stackoverflow.com/a/5070889

#define cli_lock_stdout() \
	do {  \
		pthread_mutex_lock(&cli_mutex); \
		char *tmp_line_buffer = NULL; \
		int tmp_point = -1; /* silence "-Wconditional-uninitialized" false positive */ \
		if (likely(cli_reading_line)) { \
			int tmp_len = rl_end; \
			tmp_line_buffer = alloca(tmp_len+1); \
			memcpy(tmp_line_buffer, rl_line_buffer, (unsigned int)tmp_len); \
			tmp_line_buffer[tmp_len] = '\0'; \
			tmp_point = rl_point; \
			rl_save_prompt(); \
			rl_replace_line("", 0); \
			rl_redisplay(); \
		}

#define cli_unlock_stdout() \
		if (likely(tmp_line_buffer != NULL)) { \
			rl_restore_prompt(); \
			rl_replace_line(tmp_line_buffer, 0); \
			rl_point = tmp_point; \
			rl_redisplay(); \
		} \
		pthread_mutex_unlock(&cli_mutex); \
	} while (0)

#define cli_println(fmt, ...) \
	do { \
		cli_lock_stdout(); \
		fprintf(stderr, fmt "\n", ##__VA_ARGS__); \
		cli_unlock_stdout(); \
	} while (0)

// -----------------------------------------------------------------------------
