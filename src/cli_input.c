#include "cli_input.h"

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <unistd.h>

#pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wstrict-prototypes"
  #include <readline.h>
#pragma GCC diagnostic pop
#include <readline/history.h>

#include <lua.h>

#include "buffers.h"
#include "cli_output.h"
#include "click.h"
#include "lua.h"
#include "macros.h"
#include "main.h"
#include "pipe_io.h"

_Atomic(bool) cli_reading_line;

#define DEFAULT_PROMPT "] "

#define HISTORY_FILE ".cli_history"
#define HISTORY_MAX_ITEMS 1000

static char *prompt; // protected by cli_(un)lock_output()

// -----------------------------------------------------------------------------

static int msgpipe[2] = {-1, -1};

enum msg {
	msg_exit = 1,
	msg_winch = 2,
};

// -----------------------------------------------------------------------------

// true if we're exiting because the user typed ^D at the terminal
static _Atomic(bool) cli_got_eof;

__attribute__((cold))
static void linehandler(char *line) {
	cli_reading_line = false;

	if (line != NULL) {
		if (*line != '\0') {
			lua_State *L = lua_get_state();
			if (L == NULL) goto lua_done;
			 lua_getglobal(L, "_cli_input");
			  lua_pushstring(L, line);
			lua_call(L, 1, 0);
			lua_release_state(L);
lua_done:;
			bool same_as_last_history_item =
			    (history_length != 0 &&
			     0 == strcmp(line, history_get(history_length)->line));

			if (!same_as_last_history_item) {
				add_history(line);
				append_history(1, HISTORY_FILE);
			}

		}
	} else {
		cli_got_eof = true;
		writech(msgpipe[1], msg_exit);
	}

	if (unlikely(cli_got_eof)) {
		// has to be called from here so it doesn't print the prompt again
		rl_callback_handler_remove();
	}

	free(line);
}

// -----------------------------------------------------------------------------

__attribute__((cold))
static void winch_handler(int signal) {
	(void)signal;
	writech(msgpipe[1], msg_winch);
}

// -----------------------------------------------------------------------------

__attribute__((cold))
static void *cli_main(void *ud) {
	(void)ud;
	set_thread_name("cli");

	signal(SIGWINCH, winch_handler);

	cli_lock_output_nosave();
	 if (!prompt) prompt = strdup(DEFAULT_PROMPT);
	 cli_reading_line = true;
	 rl_callback_handler_install(prompt, linehandler);
	 rl_bind_key('\t', rl_insert); // disable filename completion
	cli_unlock_output_norestore();

	// create the history file if it doesn't exist
	// otherwise append_history() can't append to it
	FILE *tmp = fopen(HISTORY_FILE, "a");
	if (tmp != NULL) fclose(exchange(tmp, NULL));

	using_history();
	stifle_history(HISTORY_MAX_ITEMS);
	read_history(HISTORY_FILE);
	history_set_pos(history_length);

	one_true_entry();
	for (;;) {
		switch (rdselect(msgpipe[0], STDIN_FILENO)) {
		case 2:
			cli_lock_output_nosave(); cli_unlock_output_norestore();
			rl_callback_read_char();
			cli_reading_line = !cli_got_eof;
			cli_lock_output_nosave(); cli_unlock_output_norestore();
			// threadsanitizer complains if these locks/unlocks are removed
			// something to do with the save/restore code in cli_output.c
			// i don't see how this could make it any safer though
			break;
		case 1:
			switch (readch(msgpipe[0])) {
			case msg_winch:
				cli_lock_output_nosave();
				 rl_resize_terminal();
				cli_unlock_output_norestore();
				break;
			case msg_exit:
				goto out;
			}
			break;
		case 0:
			perror("cli: rdselect");
			goto out;
		default:
			goto out;
		}
	}
out:
	one_true_exit();
	signal(SIGWINCH, SIG_DFL);

	cli_lock_output_nosave();
	 free(exchange(prompt, NULL));
	 // put the next thing on its own line again
	 // for some reason, stdout here doesn't print it if you did ^C
	 fputc('\n', stderr);
	 // clean up readline and reset terminal state (important)
	 rl_callback_handler_remove();
	cli_unlock_output_norestore();

	// this saves any modified history items
	write_history(HISTORY_FILE);

	// tell main to quit if it isn't already
	if (cli_got_eof) main_quit();

	return NULL;
}

// -----------------------------------------------------------------------------

static pthread_t thread;

void cli_input_init(void) {
	if (thread != 0) return;
	if (!isatty(STDIN_FILENO)) return;

	check_minus1(
	    pipe(msgpipe),
	    "cli: pipe",
	    goto err);

	check_errcode(
	    pthread_create(&thread, NULL, cli_main, NULL),
	    "cli: pthread_create",
	    goto err);

	return;
err:
	if (msgpipe[0] != -1) close(exchange(msgpipe[0], -1));
	if (msgpipe[1] != -1) close(exchange(msgpipe[1], -1));
	thread = 0;
}

void cli_input_deinit(void) {
	if (thread == 0) return;

	writech(msgpipe[1], msg_exit);

	struct timespec ts = {0};
	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec += 1;
	int err = pthread_timedjoin_np(thread, NULL, &ts);
	if (err != 0) {
		return;
	}

	close(exchange(msgpipe[0], -1));
	close(exchange(msgpipe[1], -1));

	thread = 0;
}

// -----------------------------------------------------------------------------

static int l_set_prompt(lua_State *L) {
	char *newone = strdup(lua_tostring(L, 1) ?: DEFAULT_PROMPT);
	cli_lock_output();
	 free(prompt);
	 prompt = newone;
	 rl_set_prompt(prompt);
	cli_unlock_output();
	return 0;
}

void cli_input_init_lua(void *L) {
	 lua_pushcfunction(L, l_set_prompt);
	lua_setglobal(L, "_set_prompt");
}
