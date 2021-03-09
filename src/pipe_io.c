#include "pipe_io.h"

#include <errno.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "cli_output.h"
#include "macros.h"

int rdselect_real(int count, ...) {
	assert(count >= 1 && count <= RDSELECT_MAX_FD);
	struct pollfd fds[RDSELECT_MAX_FD];

	va_list args;
	va_start(args, count);
	for (int i = 0; i < count; i++) {
		fds[i].events = POLLIN;
		fds[i].fd = va_arg(args, int);
	}
	va_end(args);

	int rv;
	for (;;) {
		rv = poll(fds, (nfds_t)count, -1);
		if (unlikely(rv == -1)) {
			if (likely(errno == EINTR)) continue;
			return 0;
		}
D		assert(rv != 0);
		for (int i = 0; i < count; i++) {
			if (unlikely(fds[i].revents&(POLLERR|POLLHUP|POLLNVAL))) {
				return -(i+1);
			}
		}
		for (int i = 0; i < count; i++) {
			if (likely(fds[i].revents&POLLIN)) {
				return i+1;
			}
		}
		assert_unreachable();
	}
}

// note: this saves and restores errno because it's called from signal handlers
// (the error handling isnt signal-safe but lets hope it doesn't fail)
void writech_real(int fd, char c) {
	int olderrno = errno;
	ssize_t rv;
again:
	rv = write(fd, &c, 1);
	switch (rv) {
	case -1:
		if (likely(errno == EINTR)) goto again;
		perror("writech");
		abort();
	case 0:
		eprintln("writech: write error");
		abort();
	case 1:
		errno = olderrno;
		return;
	default:
		assert_unreachable();
	}
}

char readch_real(int fd) {
	char c;
	ssize_t rv;
again:
	rv = read(fd, &c, 1);
	switch (rv) {
	case -1:
		if (likely(errno == EINTR)) goto again;
		perror("readch");
		abort();
	case 0:
		eprintln("readch: read error");
		abort();
	case 1:
		return c;
	default:
		assert_unreachable();
	}
}
