#define _GNU_SOURCE // unlocked_stdio
#include "cli_input.h"
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <pthread.h>

#include "main.h"
#include "lua.h"
#include "click.h"
#include "vcr.h"
#include "buffers.h"
#include "macros.h"
#include "cli_output.h"

#pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wstrict-prototypes"
  #include <readline.h>
#pragma GCC diagnostic pop
#include <readline/history.h>

_Atomic(bool) cli_reading_line;

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
static lua_State *g_L;

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

static void winch_handler(int signal) {
	(void)signal;
	msg_write(msg_winch);
}

// -----------------------------------------------------------------------------

#define POLLNOTGOOD (POLLERR|POLLHUP|POLLNVAL)

static void *cli_main(void *ud) {
	lua_State *L = ud;
	g_L = L;
	set_thread_name("cli");

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

void cli_input_init(void *L) {
	if (thread != 0) return;
	if (!isatty(STDIN_FILENO)) return;
	if (pipe(msgpipe) == -1) {
		eprintln("cli: pipe: %s", strerror(errno));
		goto err;
	}
	int err = pthread_create(&thread, NULL, cli_main, L);
	if (err != 0) {
		thread = 0;
		eprintln("cli: pthread_create: %s", strerror(err));
		goto err;
	}
	return;
err:
	if (msgpipe[0] != -1) close(exchange(int, msgpipe[0], -1));
	if (msgpipe[1] != -1) close(exchange(int, msgpipe[1], -1));
}

void cli_input_deinit(void) {
	if (thread == 0) return;

	msg_write(msg_exit);

	pthread_join(exchange(pthread_t, thread, 0), NULL);

	close(exchange(int, msgpipe[0], -1));
	close(exchange(int, msgpipe[1], -1));
}
