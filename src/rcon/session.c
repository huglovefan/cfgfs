/*
 * This file contains code from the rcon project. See README.md for details.
 */

#include "session.h"

#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "../cli_output.h"
#include "../macros.h"

#include "srcrcon.h"

// -----------------------------------------------------------------------------

struct byte_array {
	size_t len;
	size_t cap;
	char *data;
};

static void byte_array_append(struct byte_array *self, const char *data, size_t sz) {
	if (self->cap < sz+self->len) goto resize;
resize_done:
	memcpy(self->data+self->len, data, sz);
	self->len += sz;
	return;
resize:
	do {
		self->cap = (self->cap != 0) ? self->cap*2 : 64;
	} while (self->cap < sz+self->len);
	self->data = realloc(self->data, self->cap);
	goto resize_done;
}
static void byte_array_remove_range(struct byte_array *self, size_t start, size_t len) {
	if (start >= self->len) return;
	if (len > self->len-start) len = self->len-start;
	size_t end = start+len;
	memmove(self->data+start, self->data+end, self->len-end);
	self->len -= len;
}

// -----------------------------------------------------------------------------

static int send_message(struct rcon_session *sess, src_rcon_message_t *msg);
static int wait_auth(struct rcon_session *sess, src_rcon_message_t *auth);

static int send_command_nowait(struct rcon_session *sess, char const *cmd);
static int send_command_dowait(struct rcon_session *sess, char const *cmd);

struct rcon_session {
	src_rcon_t *r;
	int conn;
	struct byte_array response;
};

struct rcon_session *rcon_connect(const char *host, int port, const char *password) {
	struct rcon_session *sess = calloc(1, sizeof(struct rcon_session));
	if (!sess) {
		return NULL;
	}

	int sock = -1;
	src_rcon_message_t *auth = NULL;

	char portstr[8];
	sprintf(portstr, "%hu", (unsigned short)port);

	struct addrinfo hint = {0};
	hint.ai_socktype = SOCK_STREAM;
	hint.ai_family = AF_UNSPEC;
	hint.ai_flags = AI_PASSIVE;

	struct addrinfo *info = NULL;

	int ret;

	if ((ret = getaddrinfo(host, portstr, &hint, &info))) {
		eprintln("Failed to resolve host: %s: %s", host, gai_strerror(ret));
		goto err;
	}

	for (struct addrinfo *ai = info; ai != NULL; ai = ai->ai_next) {
		sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if (-1 == sock) {
			continue;
		}

		if (-1 == connect(sock, ai->ai_addr, ai->ai_addrlen)) {
			close(sock);
			sock = -1;
			continue;
		}

		break;
	}

	if (sock < 0) {
		eprintln("Failed to connect to the given host/service");
		goto err;
	}

	sess->conn = sock;
	sess->r = src_rcon_new();

	if (password != NULL) {
		auth = src_rcon_auth(sess->r, password);
		if (send_message(sess, auth)) {
			goto err;
		}
		if (wait_auth(sess, auth)) {
			eprintln("Invalid auth reply, valid password?");
			goto err;
		}
	}

	if (auth) src_rcon_message_free(auth);
	if (info) freeaddrinfo(info);

	return sess;
err:
	if (auth) src_rcon_message_free(auth);
	if (info) freeaddrinfo(info);

	if (sock != -1) close(sock);
	if (sess != NULL) {
		src_rcon_free(sess->r);
		free(sess->response.data);
		free(sess);
	}
	return NULL;
}

int rcon_run_cfg(struct rcon_session *sess, const char *cfg, int nowait) {
	if (nowait) {
		return send_command_nowait(sess, cfg);
	} else {
		return send_command_dowait(sess, cfg);
	}
}

void rcon_disconnect(struct rcon_session *sess) {
	if (sess == NULL) return;
	close(sess->conn);
	free(sess->response.data);
	src_rcon_free(sess->r);
	free(sess);
}

// -----------------------------------------------------------------------------

static int send_message(struct rcon_session *sess, src_rcon_message_t *msg) {
	uint8_t *data = NULL;
	size_t size = 0;
	if (src_rcon_serialize(sess->r, msg, &data, &size)) {
		return -1;
	}

	uint8_t *p = data;
	do {
		ssize_t ret = write(sess->conn, p, size);
		if (ret == 0 || ret == -1) {
			free(data);
			perror("Failed to communicate");
			return -2;
		}

		p += ret;
		size -= (size_t)ret;
	} while (size > 0);

	free(data);

	return 0;
}

static int wait_auth(struct rcon_session *sess, src_rcon_message_t *auth) {
	for (;;) {
		char tmp[512];
		ssize_t ret = read(sess->conn, tmp, sizeof(tmp));
		if (ret == -1) {
			perror("Failed to receive data");
			return -1;
		}

		byte_array_append(&sess->response, tmp, (size_t)ret);

		size_t off = 0;
		rcon_error_t status = src_rcon_auth_wait(sess->r, auth, &off, sess->response.data, sess->response.len);
		if (status != rcon_error_moredata) {
			byte_array_remove_range(&sess->response, 0, off);
			return (int)status;
		}
	}
}

static int send_command_nowait(struct rcon_session *sess, char const *cmd) {
	int ec = -1;

	src_rcon_message_t *command = src_rcon_command(sess->r, cmd);
	if (command == NULL) {
		goto cleanup;
	}
	if (send_message(sess, command)) {
		goto cleanup;
	}

	ec = 0;
cleanup:
	src_rcon_message_free(command);

	return ec;
}

static int send_command_dowait(struct rcon_session *sess, char const *cmd) {
	src_rcon_message_t *command = NULL, *end = NULL;
	src_rcon_message_t **commandanswers = NULL;
	int ec = -1;

	command = src_rcon_command(sess->r, cmd);
	if (command == NULL) {
		goto cleanup;
	}
	if (send_message(sess, command)) {
		goto cleanup;
	}

	end = src_rcon_command(sess->r, "");
	if (end == NULL) {
		goto cleanup;
	}
	if (send_message(sess, end)) {
		goto cleanup;
	}

	size_t off = 0;
	bool done = false;
	do {
		char tmp[512];
		ssize_t ret = read(sess->conn, tmp, sizeof(tmp));
		if (ret == -1) {
			perror("Failed to receive data");
			goto cleanup;
		}
		if (ret == 0) {
			eprintln("Peer: connection closed");
			done = true;
		}

		byte_array_append(&sess->response, tmp, (size_t)ret);
		rcon_error_t status = src_rcon_command_wait(sess->r, command, &commandanswers, &off, sess->response.data, sess->response.len);
		if (status != rcon_error_moredata) {
			byte_array_remove_range(&sess->response, 0, off);
		}

		if (status == rcon_error_success && commandanswers != NULL) {
			bool locked = false;
			for (src_rcon_message_t **p = commandanswers; *p != NULL; p++) {
				if ((*p)->id == end->id) {
					done = true;
					continue;
				}
				size_t bodylen = strlen((char *)(*p)->body);

				if (!locked++) cli_lock_output();
				fwrite((*p)->body, 1, bodylen, stderr);

				if (bodylen != 0 && (*p)->body[bodylen-1] != '\n') {
					fputc('\n', stderr);
				}
			}
			if (locked--) cli_unlock_output();
		}

		src_rcon_message_freev(commandanswers);
		commandanswers = NULL;
	} while (!done);

	ec = 0;
cleanup:
	src_rcon_message_free(command);
	src_rcon_message_free(end);
	src_rcon_message_freev(commandanswers);

	return ec;
}
