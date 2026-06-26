#ifndef REDIS_CONN_H
#define REDIS_CONN_H

#include <hiredis/hiredis.h>

typedef struct {
    redisContext *ctx;
    char host[256];
    int port;
} RedisConn;

int redis_conn_init(RedisConn *rc, const char *url);
void redis_conn_close(RedisConn *rc);
redisReply *redis_conn_cmd(RedisConn *rc, const char *fmt, ...);
void free_reply(redisReply *reply);

#endif
