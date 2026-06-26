#include "redis_conn.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

static int parse_redis_url(const char *url, char *host, int host_size, int *port)
{
    *port = 6379;
    host[0] = '\0';

    const char *p = url;
    if (strncmp(p, "redis://", 8) == 0) p += 8;
    else if (strncmp(p, "rediss://", 9) == 0) p += 9;

    const char *colon = strchr(p, ':');
    const char *slash = strchr(p, '/');

    if (colon && (!slash || colon < slash)) {
        size_t hlen = (size_t)(colon - p);
        if (hlen >= (size_t)host_size) hlen = (size_t)(host_size - 1);
        memcpy(host, p, hlen);
        host[hlen] = '\0';
        *port = atoi(colon + 1);
    } else {
        size_t hlen = slash ? (size_t)(slash - p) : strlen(p);
        if (hlen >= (size_t)host_size) hlen = (size_t)(host_size - 1);
        memcpy(host, p, hlen);
        host[hlen] = '\0';
    }

    return 0;
}

int redis_conn_init(RedisConn *rc, const char *url)
{
    memset(rc, 0, sizeof(RedisConn));
    parse_redis_url(url, rc->host, sizeof(rc->host), &rc->port);

    rc->ctx = redisConnect(rc->host, rc->port);
    if (rc->ctx == NULL || rc->ctx->err) {
        fprintf(stderr, "[redis] connect error: %s\n",
                rc->ctx ? rc->ctx->errstr : "alloc failed");
        if (rc->ctx) redisFree(rc->ctx);
        rc->ctx = NULL;
        return -1;
    }

    struct timeval tv = {5, 0};
    redisSetTimeout(rc->ctx, tv);

    fprintf(stderr, "[redis] connected to %s:%d\n", rc->host, rc->port);
    return 0;
}

void redis_conn_close(RedisConn *rc)
{
    if (rc->ctx) {
        redisFree(rc->ctx);
        rc->ctx = NULL;
    }
}

redisReply *redis_conn_cmd(RedisConn *rc, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    redisReply *reply = redisvCommand(rc->ctx, fmt, ap);
    va_end(ap);

    if (!reply) {
        fprintf(stderr, "[redis] command failed (connection error)\n");
        redisReconnect(rc->ctx);
        return NULL;
    }
    return reply;
}

void free_reply(redisReply *reply)
{
    if (reply) freeReplyObject(reply);
}
