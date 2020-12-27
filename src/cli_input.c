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

#if defined(__cplusplus)
 #include <lua.hpp>
#else
 #include <lua.h>
#endif

#include "buffers.h"
#include "cli_output.h"
#include "click.h"
#include "lua.h"
#include "macros.h"
#include "main.h"

_Atomic(bool) cli_reading_line;

#define DEFAULT_PROMPT "] "

//static char *history_file;
static char *prompt; // protected by cli_(un)lock_output()

// -----------------------------------------------------------------------------

static int msgpipe[2] = {-1, -1};

enum msg_action {
	msg_winch = 1,
	msg_exit = 2,
};

#define msg_write(c) ({ char c_ = (c); write(msgpipe[1], &c_, 1); })
#define msg_read()   ({ char c = 0; read(msgpipe[0], &c, 1); c; })

// -----------------------------------------------------------------------------

static _Atomic(bool) cli_exiting;

__attribute__((cold))
static void linehandler(char *line) {
	cli_reading_line = false;

	if (line != NULL) {
		if (*line != '\0') {
			lua_State *L = lua_get_state();
			 lua_getglobal(L, "_cli_input");
			  lua_pushstring(L, line);
			lua_call(L, 1, 0);
			lua_release_state(L);
			add_history(line);
			// ...
			/*
			HIST_ENTRY *ent = current_history();
			if (!ent || 0 == strcmp(line, ent->line)) {
				add_history(line);
				if (history_file) {
					append_history(1, history_file);
				}
			}
			*/
		}
	} else {
		cli_exiting = true;
		msg_write(msg_exit);
	}

	if (unlikely(cli_exiting)) {
		// has to be called from here so it doesn't print another prompt
		rl_callback_handler_remove();
	}

	free(line);
}

// -----------------------------------------------------------------------------

__attribute__((cold))
static void winch_handler(int signal) {
	(void)signal;
	msg_write(msg_winch);
}

// -----------------------------------------------------------------------------

#define POLLNOTGOOD (POLLERR|POLLHUP|POLLNVAL)

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
	 using_history();
	cli_unlock_output_norestore();

	struct pollfd fds[2] = {
		{.fd = STDIN_FILENO, .events = POLLIN},
		{.fd = msgpipe[0],   .events = POLLIN},
	};
	for (;;) {
		int rv = poll(fds, 2, -1);
		if (unlikely(rv == -1)) {
			if (likely(errno == EINTR)) continue;
			perror("cli: poll");
			break;
		}
		if (unlikely((fds[0].revents|fds[1].revents) & POLLNOTGOOD)) break;
		if (unlikely(fds[1].revents & POLLIN)) {
			switch (msg_read()) {
			case msg_winch:
				cli_lock_output_nosave();
				 rl_resize_terminal();
				cli_unlock_output_norestore();
				break;
			case msg_exit:
				goto out;
			default:
D				assert(0);
				__builtin_unreachable();
			}
		}
		if (likely(fds[0].revents & POLLIN)) {
			cli_lock_output_nosave(); cli_unlock_output_norestore();
			rl_callback_read_char();
			cli_reading_line = !cli_exiting;
			cli_lock_output_nosave(); cli_unlock_output_norestore();
			// threadsanitizer complains if these locks/unlocks are removed
			// something to do with the save/restore code in cli_output.c
			// i don't see how this could make it any safer though
		}
	}
out:
	signal(SIGWINCH, SIG_DFL);

	cli_lock_output_nosave();
	 free(exchange(prompt, NULL));
	 // put the next thing on its own line again
	 // for some reason, stdout here doesn't print it if you did ^C
	 fputc('\n', stderr);
	 // clean up readline and reset terminal state (important)
	 rl_callback_handler_remove();
	cli_unlock_output_norestore();

	// we're executing this line because
	// 1. user typed end-of-input character -> want main to quit in this case
	// 2. main is already quitting and telling us to quit -> no harm in calling this
	main_quit();

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

	msg_write(msg_exit);

	pthread_join(thread, NULL);

	close(exchange(msgpipe[0], -1));
	close(exchange(msgpipe[1], -1));

	thread = 0;
}

// -----------------------------------------------------------------------------

static int l_set_prompt(lua_State *L) {
	const char *newone = lua_tostring(L, 1) ?: DEFAULT_PROMPT;
	cli_lock_output();
	 free(prompt);
	 prompt = strdup(newone);
	 rl_set_prompt(prompt);
	cli_unlock_output();
	return 0;
}

void cli_input_init_lua(void *L) {
	 lua_pushcfunction(L, l_set_prompt);
	lua_setglobal(L, "_set_prompt");
}
