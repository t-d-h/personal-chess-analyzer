#ifndef WORKER_H
#define WORKER_H

#include <mongoc/mongoc.h>
#include <hiredis/hiredis.h>
#include <stdbool.h>

typedef struct {
    char redis_url[512];
    char mongo_url[512];
    char db_name[128];
    int depth;
    int book_plies;
} WorkerConfig;

typedef struct {
    const WorkerConfig *config;
    mongoc_client_pool_t *mongo_pool;
    int worker_id;
} ThreadCtx;

void *worker_thread(void *arg);

#endif
