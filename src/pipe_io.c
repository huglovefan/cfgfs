#include "pipe_io.h"

#include <poll.h>
#include <stdarg.h>
#include <errno.h>

#include "macros.h"

int rdselect_real(int count, ...) {
	assume(count >= 1);
	assume(count <= 126); // C standard says calls have a limit of 127 arguments
	struct pollfd fds[count];

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
		UNREACHABLE();
	}
}
