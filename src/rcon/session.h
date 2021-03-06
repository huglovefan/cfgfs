#pragma once

#include <stdint.h>

struct rcon_session;

struct rcon_session *rcon_connect(const char *host, int port, const char *password);
int rcon_run_cfg(struct rcon_session *session, const char *cfg, int32_t *id_out);
void rcon_disconnect(struct rcon_session *session);

#define max_cfg_size_rcon ((size_t)1014)
