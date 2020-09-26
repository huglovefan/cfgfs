#include "cli.h"
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>

#include "main.h"
#include "lua.h"
#include "click.h"

#include <readline/history.h>

#define THE_PROMPT "] "

pthread_mutex_t cli_mutex = PTHREAD_MUTEX_INITIALIZER;
bool _Atomic    cli_reading_line = false;

// -----------------------------------------------------------------------------

enum quitpipe_action {
	qp_winch = 1,
	qp_exit = 2,
};

static int          quitpipe[2];
static lua_State    *g_L;
static bool _Atomic cli_exiting;

#define qp_write(c) ({ char c_ = (c); write(quitpipe[1], &c_, 1); })
#define qp_read() ({ char c = 0; read(quitpipe[0], &c, 1); c; })

static void handle_sigwinch(int signal) {
	(void)signal;
	qp_write(qp_winch);
}

static void linehandler(char *line) {
	cli_reading_line = false;

	if (unlikely(line == NULL)) {
		cli_exiting = true;
		qp_write(qp_exit);
		goto end;
	}
	if (unlikely(*line == '\0')) goto end;

	LUA_LOCK();
	lua_State *L = g_L;
	 lua_getglobal(L, "_cfg");
	  lua_pushstring(L, line);
	lua_call(L, 1, 0);
	LUA_UNLOCK();
	click(0);

	add_history(line);
end:
	if (unlikely(cli_exiting)) rl_callback_handler_remove();
}

// -----------------------------------------------------------------------------

static pthread_t thread;

static struct pollfd fds[2] = {
	{.fd = STDIN_FILENO, .events = POLLIN},
	{.fd = -1,           .events = POLLIN},
};

#define POLLNOTGOOD (POLLERR|POLLHUP|POLLNVAL)

static void *cli_thread_main(void *ud) {
	set_thread_name("cf_cli_thing");
	lua_State *L = (lua_State *)ud;
	g_L = L;

	pthread_mutex_lock(&cli_mutex);
	 signal(SIGWINCH, handle_sigwinch);
	 rl_bind_key('\t', rl_insert); // disable filename completion
	 rl_callback_handler_install(THE_PROMPT, linehandler);
	 cli_reading_line = true;
	pthread_mutex_unlock(&cli_mutex);

	while (true) {
		int rv = poll(fds, 2, -1);
		if (unlikely(rv == -1)) {
			// TODO: https://man7.org/linux/man-pages/man2/signalfd.2.html
			if (likely(errno == EINTR)) continue;
			break;
		}
		if (unlikely(fds[0].revents & POLLNOTGOOD)) break;
		if (unlikely(fds[1].revents & POLLNOTGOOD)) break;
		if (unlikely(fds[1].revents & POLLIN)) {
			switch (qp_read()) {
			case qp_winch:
				pthread_mutex_lock(&cli_mutex);
				 rl_resize_terminal();
				pthread_mutex_unlock(&cli_mutex);
				break;
			case qp_exit:
				goto outofhere;
			}
		}
		if (likely(fds[0].revents & POLLIN)) {
			pthread_mutex_lock(&cli_mutex);
			pthread_mutex_unlock(&cli_mutex);
			rl_callback_read_char();
			cli_reading_line = true;
			pthread_mutex_lock(&cli_mutex);
			pthread_mutex_unlock(&cli_mutex);
			// these locks and unlocks may seem useless but they
			//  make threadsanitizer's complaints disappear
			// it works as a memory barrier or something
		}
	}
outofhere:
	if (cli_exiting) fputc('\n', stderr);

	signal(SIGWINCH, SIG_DFL);

	pthread_mutex_lock(&cli_mutex);
	 cli_reading_line = false;
	 rl_callback_handler_remove();
	pthread_mutex_unlock(&cli_mutex);

	close(quitpipe[1]); // <-- is the other end closed?
	                    // ^ what did this comment mean?
	main_stop();

	pthread_exit(NULL);
	return NULL;
}

// -----------------------------------------------------------------------------

__attribute__((cold))
void cli_thread_start(void *L) {
	if (thread != 0) return;

	pipe(quitpipe);
	fds[1].fd = quitpipe[0];
	pthread_create(&thread, NULL, cli_thread_main, L);
}

__attribute__((cold))
void cli_thread_stop(void) {
	if (thread == 0) return;

	cli_exiting = true;
	qp_write(qp_exit);

	pthread_join(thread, NULL);
	thread = 0;
}

// -----------------------------------------------------------------------------
