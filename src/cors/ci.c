/*------------------------------------------------------------------------------
 * ci.c : control interface (named pipes) for multi-process CORS
 *
 * author  : cors contributors
 * version : 1.0
 * history : 2026/06/13 1.0  new
 *-----------------------------------------------------------------------------*/
#include "mcors.h"

#include <string.h>
#include <errno.h>

#ifndef WIN32
#include <unistd.h>
#endif

static void on_ci_connection_close(uv_handle_t *handle)
{
    cors_ci_client_t *client = handle->data;
    if (client) {
        if (client->owner) HASH_DEL(client->owner->clients, client);
        free(client);
    }
}

static void on_ci_alloc(uv_handle_t *handle, size_t suggested, uv_buf_t *buf)
{
    static char slab[MCORS_CI_MAX_MSG + 4];
    buf->base = slab;
    buf->len = sizeof(slab);
}

static void on_ci_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf)
{
    cors_ci_client_t *client = stream->data;
    if (nread < 0) {
        if (nread != UV_EOF) {
            log_trace(2, "cors_ci read error: %s\n", uv_strerror((int)nread));
        }
        uv_close((uv_handle_t *)stream, on_ci_connection_close);
        return;
    }
    if (nread == 0) return;
    log_trace(3, "cors_ci worker %d message (%zd bytes)\n", client->worker_id, nread);
}

static void on_ci_new_connection(uv_stream_t *server, int status)
{
    cors_ci_t *ci = server->data;
    cors_ci_client_t *client;
    uv_pipe_t *pipe;

    if (status < 0) {
        log_trace(1, "cors_ci connection error: %s\n", uv_strerror(status));
        return;
    }
    client = calloc(1, sizeof(*client));
    pipe = calloc(1, sizeof(*pipe));
    if (!client || !pipe) {
        free(client);
        free(pipe);
        return;
    }
    client->pipe = pipe;
    client->owner = ci;
    pipe->data = client;
    uv_pipe_init(server->loop, pipe, 0);
    if (uv_accept(server, (uv_stream_t *)pipe) == 0) {
        uv_read_start((uv_stream_t *)pipe, on_ci_alloc, on_ci_read);
        HASH_ADD_INT(ci->clients, worker_id, client);
    } else {
        uv_close((uv_handle_t *)pipe, on_ci_connection_close);
    }
}

extern int cors_ci_start(cors_ci_t *ci, uv_loop_t *loop, const char *pipe_path)
{
    if (!ci || !loop || !pipe_path) return 0;

    memset(ci, 0, sizeof(*ci));
    snprintf(ci->pipe_path, sizeof(ci->pipe_path), "%s", pipe_path);

    unlink(ci->pipe_path);

    ci->svr = calloc(1, sizeof(uv_pipe_t));
    if (!ci->svr) return 0;

    uv_pipe_init(loop, ci->svr, 0);
    ci->svr->data = ci;

    if (uv_pipe_bind(ci->svr, ci->pipe_path) != 0) {
        log_trace(1, "cors_ci_start: bind %s failed\n", ci->pipe_path);
        free(ci->svr);
        ci->svr = NULL;
        return 0;
    }
    if (uv_listen((uv_stream_t *)ci->svr, 8, on_ci_new_connection) != 0) {
        log_trace(1, "cors_ci_start: listen failed\n");
        uv_close((uv_handle_t *)ci->svr, NULL);
        free(ci->svr);
        ci->svr = NULL;
        unlink(ci->pipe_path);
        return 0;
    }
    ci->state = 1;
    log_trace(1, "cors_ci listening on %s\n", ci->pipe_path);
    return 1;
}

extern void cors_ci_close(cors_ci_t *ci)
{
    cors_ci_client_t *c, *tmp;

    if (!ci) return;

    HASH_ITER(hh, ci->clients, c, tmp) {
        HASH_DEL(ci->clients, c);
        if (c->pipe) uv_close((uv_handle_t *)c->pipe, on_ci_connection_close);
        else free(c);
    }
    if (ci->svr) {
        uv_close((uv_handle_t *)ci->svr, NULL);
        free(ci->svr);
        ci->svr = NULL;
    }
    if (ci->pipe_path[0]) unlink(ci->pipe_path);
    ci->state = 0;
}

static void on_ci_connect(uv_connect_t *req, int status)
{
    if (status < 0) {
        log_trace(1, "cors_ci_connect failed: %s\n", uv_strerror(status));
    }
}

extern int cors_ci_connect(uv_loop_t *loop, const char *pipe_path, uv_pipe_t *pipe)
{
    uv_connect_t req;

    if (!loop || !pipe_path || !pipe) return 0;
    uv_pipe_init(loop, pipe, 0);
    uv_pipe_connect(&req, pipe, pipe_path, on_ci_connect);
    return 1;
}

extern int cors_ci_send(cors_ci_t *ci, int worker_id, const char *msg, uint32_t len)
{
    cors_ci_client_t *client;
    uv_buf_t buf;
    uint32_t hdr[2];
    uv_write_t req;

    if (!ci || !msg || len == 0 || len > MCORS_CI_MAX_MSG) return 0;

    HASH_FIND_INT(ci->clients, &worker_id, client);
    if (!client || !client->pipe) return 0;

    hdr[0] = len;
    hdr[1] = (uint32_t)worker_id;
    buf = uv_buf_init((char *)hdr, sizeof(hdr));
    if (uv_write(&req, (uv_stream_t *)client->pipe, &buf, 1, NULL) != 0) return 0;

    buf = uv_buf_init((char *)msg, len);
    if (uv_write(&req, (uv_stream_t *)client->pipe, &buf, 1, NULL) != 0) return 0;

    return 1;
}
