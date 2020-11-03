#define _GNU_SOURCE // unlocked_stdio
#include "cli_input.h"

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
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
#include "vcr.h"

_Atomic(bool) cli_reading_line;

// -----------------------------------------------------------------------------

static int msgpipe[2] = {-1, -1};

enum msg_action {
	msg_winch = 1,
	msg_exit = 2,
	msg_manual_eof = 3,
};

#define msg_write(c) ({ char c_ = (c); write(msgpipe[1], &c_, 1); })
#define msg_read()   ({ char c = 0; read(msgpipe[0], &c, 1); c; })

// -----------------------------------------------------------------------------

static _Atomic(bool) cli_exiting;
static lua_State *g_L;

__attribute__((cold))
static void linehandler(char *line) {
	cli_reading_line = false;
	double tm = vcr_get_timestamp();

	if (unlikely(line == NULL)) {
		vcr_event {
			vcr_add_string("what", "cli_eof");
			vcr_add_double("timestamp", tm);
		}
		cli_exiting = true;
		msg_write(msg_exit);
		goto end;
	}
	if (unlikely(*line == '\0')) goto end;

	vcr_event {
		vcr_add_string("what", "cli_input");
		vcr_add_string("text", line);
		vcr_add_double("timestamp", tm);
	}

	LUA_LOCK();
	lua_State *L = g_L;
	 lua_getglobal(L, "_cli_input");
	  lua_pushstring(L, line);
	lua_call(L, 1, 0);
	opportunistic_click();
	LUA_UNLOCK();

	add_history(line);
end:
	if (unlikely(cli_exiting)) rl_callback_handler_remove();
	// ^ has to be called from here so it doesn't print another prompt
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
	set_thread_name("cli");
	lua_State *L = ud;
	g_L = L;

	signal(SIGWINCH, winch_handler);

	cli_lock_output_nosave();
	 cli_reading_line = true;
	 rl_callback_handler_install("] ", linehandler);
	 rl_bind_key('\t', rl_insert); // disable filename completion
	cli_unlock_output_norestore();

	struct pollfd fds[2] = {
		{.fd = STDIN_FILENO, .events = POLLIN},
		{.fd = msgpipe[0],   .events = POLLIN},
	};
	for (;;) {
		int rv = poll(fds, 2, -1);
		if (unlikely(rv == -1)) {
			if (likely(errno == EINTR)) continue;
			eprintln("cli: poll: %s", strerror(errno));
			break;
		}
		if (unlikely(fds[0].revents & POLLNOTGOOD)) break;
		if (unlikely(fds[1].revents & POLLNOTGOOD)) break;
		if (unlikely(fds[1].revents & POLLIN)) {
			switch (msg_read()) {
			case msg_winch:
				cli_lock_output_nosave();
				 rl_resize_terminal();
				cli_unlock_output_norestore();
				break;
			case msg_exit:
				goto out;
			case msg_manual_eof:
				{
				double tm = vcr_get_timestamp();
				vcr_event {
					vcr_add_string("what", "cli_eof");
					vcr_add_double("timestamp", tm);
				}
				}
				goto out;
			default:
				assert(false);
			}
		}
		if (likely(fds[0].revents & POLLIN)) {
			rl_callback_read_char();
			cli_reading_line = !cli_exiting;
		}
	}
out:
	signal(SIGWINCH, SIG_DFL);

	cli_lock_output_nosave();
	 // put the next thing on its own line again
	 // for some reason, stdout here doesn't print it if you did ^C
	 fputc_unlocked('\n', stderr);
	 // clean up readline and reset terminal state (important)
	 rl_callback_handler_remove();
	cli_unlock_output_norestore();

	// we're executing this line because
	// 1. user typed end-of-input character -> want main to quit in this case
	// 2. main is already quitting and telling us to quit -> no harm in calling this
	main_stop();

	return NULL;
}

// -----------------------------------------------------------------------------

static pthread_t thread;

__attribute__((cold))
void cli_input_init(void *L) {
	if (thread != 0) return;
	if (!isatty(STDIN_FILENO)) return;

	check_minus1(
	    pipe(msgpipe),
	    "cli: pipe",
	    goto err);

	check_errcode(
	    pthread_create(&thread, NULL, cli_main, L),
	    "cli: pthread_create",
	    goto err);

	return;
err:
	thread = 0;
	if (msgpipe[0] != -1) close(exchange(msgpipe[0], -1));
	if (msgpipe[1] != -1) close(exchange(msgpipe[1], -1));
}

__attribute__((cold))
void cli_input_deinit(void) {
	if (thread == 0) return;

	msg_write(msg_exit);

	pthread_join(exchange(thread, 0), NULL);

	close(exchange(msgpipe[0], -1));
	close(exchange(msgpipe[1], -1));
}

// -----------------------------------------------------------------------------

void cli_input_manual_eof(void) {
	msg_write(msg_manual_eof);
}
