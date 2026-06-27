#ifndef REDIS_CONN_H
#define REDIS_CONN_H

#include <hiredis/hiredis.h>

redisContext *redis_connect(const char *url, char *err_buf, size_t err_len);

#endif
