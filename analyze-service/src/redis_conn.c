#include "redis_conn.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

redisContext *redis_connect(const char *url, char *err_buf, size_t err_len) {
    char host[256] = "localhost";
    int port = 6379;
    
    if (url && strlen(url) > 0) {
        const char *p = url;
        if (strncmp(p, "redis://", 8) == 0) {
            p += 8;
        }
        const char *colon = strchr(p, ':');
        if (colon) {
            int host_len = colon - p;
            if (host_len > 0 && host_len < (int)sizeof(host)) {
                strncpy(host, p, host_len);
                host[host_len] = '\0';
            }
            port = atoi(colon + 1);
        } else {
            if (strlen(p) < sizeof(host)) {
                strcpy(host, p);
            }
        }
    }
    
    redisContext *c = redisConnect(host, port);
    if (!c || c->err) {
        if (c) {
            snprintf(err_buf, err_len, "%s", c->errstr);
            redisFree(c);
        } else {
            snprintf(err_buf, err_len, "Cannot allocate redis context");
        }
        return NULL;
    }
    return c;
}
