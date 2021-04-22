/*
 * This file contains code from the rcon project. See README.md for details.
 */

#include "session.h"

#include <netdb.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>
#include <unistd.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "../cli_output.h"
#include "../lua.h"
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

static int send_command_nowait(struct rcon_session *sess, char const *cmd, int32_t *id_out);

static void *rcon_reader_main(void *ud);
struct rcon_threaddata {
	struct rcon_session *sess;
	char *password;
};

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

		if (-1 == setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &(int){1}, sizeof(int))) {
			perror("rcon: setsockopt(TCP_NODELAY)");
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

	if (info) freeaddrinfo(info);

	struct rcon_threaddata *td = malloc(sizeof(struct rcon_threaddata));
	td->sess = sess;
	td->password = (password) ? strdup(password) : NULL;

	pthread_t thread;
	check_errcode(
	    pthread_create(&thread, NULL, rcon_reader_main, (void *)td),
	    "rcon: pthread_create",
	    goto err);
	pthread_detach(thread);

	return sess;
err:
	if (info) freeaddrinfo(info);

	if (sock != -1) close(sock);
	if (sess != NULL) {
		src_rcon_free(sess->r);
		free(sess->response.data);
		free(sess);
	}
	return NULL;
}

int rcon_run_cfg(struct rcon_session *sess, const char *cfg, int32_t *id_out) {
	return send_command_nowait(sess, cfg, id_out);
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

static int send_command_nowait(struct rcon_session *sess, char const *cmd, int32_t *id_out) {
	int ec = -1;

	src_rcon_message_t *command = src_rcon_command(sess->r, cmd);
	if (command == NULL) {
		goto cleanup;
	}
	if (send_message(sess, command)) {
		goto cleanup;
	}

	ec = 0;
	if (id_out) *id_out = command->id;
cleanup:
	src_rcon_message_free(command);

	return ec;
}

// -----------------------------------------------------------------------------

static void *rcon_reader_main(void *ud) {
	set_thread_name("rcon_reader");
	struct rcon_threaddata *td = ud;

	src_rcon_message_t *auth = NULL;
	bool auth_ok = false;
	if (td->password != NULL) {
		auth = src_rcon_auth(td->sess->r, td->password);
		free(td->password); td->password = NULL;
		if (send_message(td->sess, auth)) {
			assert(false, "Out of memory");
		}
		if (wait_auth(td->sess, auth)) {
			goto auth_done;
		}
		auth_ok = true;
	} else {
		auth_ok = true;
	}
auth_done:
	{
		lua_State *L = lua_get_state("rcon_reader");
		 lua_getglobal(L, "_rcon_status");
		  lua_pushstring(L, auth_ok ? "auth_ok" : "auth_fail");
		lua_call(L, 1, 0);
		lua_release_state(L);
	}
	if (auth) src_rcon_message_free(auth);
	if (!auth_ok) goto cleanup;

	if (!td->sess->response.data) goto read;

	for (;;) {
		src_rcon_message_t *command = NULL;
		src_rcon_message_t **commandanswers = NULL;
		size_t off = 0;
		rcon_error_t status = src_rcon_command_wait(td->sess->r, command, &commandanswers, &off, td->sess->response.data, td->sess->response.len);
		if (status != rcon_error_moredata) {
			byte_array_remove_range(&td->sess->response, 0, off);
		}

		if (status == rcon_error_success && commandanswers != NULL) {
			lua_State *L = NULL;
			bool nonewline = false;
			for (src_rcon_message_t **p = commandanswers; *p != NULL; p++) {
				size_t bodylen = strlen((char *)(*p)->body);

				if (!L) L = lua_get_state("rcon_reader");
				if (!L) goto nolua;

				 lua_getglobal(L, "_rcon_data");
				  lua_pushlstring(L, (char *)(*p)->body, bodylen);
				   lua_pushinteger(L, (*p)->id);
				lua_call(L, 2, 0);
				nonewline = (bodylen != 0 && (*p)->body[bodylen-1] != '\n');
			}
			if (L) {
				if (nonewline) {
					 lua_getglobal(L, "_rcon_data");
					  lua_pushnil(L);
					lua_call(L, 1, 0);
				}
				lua_release_state_no_click(L);
			}
nolua:;
		}

		src_rcon_message_freev(commandanswers);
		commandanswers = NULL;
read:;
		{
			char tmp[512];
			ssize_t ret = read(td->sess->conn, tmp, sizeof(tmp));
			if (ret == -1) {
				perror("Failed to receive data");
				goto cleanup;
			}
			if (ret == 0) {
				lua_State *L = lua_get_state("rcon_reader");
				if (L) {
					 lua_getglobal(L, "_rcon_status");
					  lua_pushstring(L, "disconnect");
					lua_call(L, 1, 0);
					lua_release_state(L);
				}
				break;
			}
			byte_array_append(&td->sess->response, tmp, (size_t)ret);
		}
	}
cleanup:
	free(td);
	return NULL;
}
