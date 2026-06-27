#ifndef WORKER_H
#define WORKER_H

#include "redis_conn.h"
#include "mongo_conn.h"
#include "analyzer.h"

#define STREAM_KEY "chess:analysis-jobs"
#define CONSUMER_GROUP "workers"
#define MAX_WORKERS 16
#define BLOCK_MS 5000
#define JOB_TIMEOUT_SECS 300

typedef struct {
    RedisConn *redis;
    MongoConn *mongo;
    int worker_id;
    const char *tools_dir;
    const char *stockfish_path;
    int engine_depth;
    int job_timeout_secs;
} WorkerCtx;

void *worker_thread(void *arg);

#endif
