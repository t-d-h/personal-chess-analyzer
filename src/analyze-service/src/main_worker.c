#include "worker.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    const char *redis_url   = getenv("REDIS_URL");
    const char *mongo_url   = getenv("MONGO_URL");
    const char *mongo_db    = getenv("MONGO_DB");
    const char *sf_path     = getenv("STOCKFISH_PATH");
    const char *conc_env    = getenv("WORKER_CONCURRENCY");
    const char *depth_env   = getenv("ENGINE_DEPTH");
    const char *job_timeout_env = getenv("JOB_TIMEOUT");

    if (!redis_url)  redis_url = "redis://localhost:6379";
    if (!mongo_url)  mongo_url = "mongodb://localhost:27017";
    if (!mongo_db)   mongo_db  = "chess_analyzer";

    int concurrency = conc_env ? atoi(conc_env) : 2;
    if (concurrency < 1 || concurrency > MAX_WORKERS) concurrency = 2;

    int engine_depth = depth_env ? atoi(depth_env) : 18;
    if (engine_depth < 1) engine_depth = 18;

    int job_timeout = job_timeout_env ? atoi(job_timeout_env) : JOB_TIMEOUT_SECS;
    if (job_timeout < 1) job_timeout = JOB_TIMEOUT_SECS;

    fprintf(stderr, "[main] Starting chess-analyze-worker\n");
    fprintf(stderr, "[main] REDIS_URL=%s  MONGO_URL=%s  MONGO_DB=%s\n",
            redis_url, mongo_url, mongo_db);
    fprintf(stderr, "[main] Concurrency=%d  Engine depth=%d  Stockfish=%s  Job timeout=%ds\n",
            concurrency, engine_depth,
            sf_path ? sf_path : "(default)", job_timeout);

    /* Connect to Redis */
    RedisConn redis;
    if (redis_conn_init(&redis, redis_url) < 0) {
        fprintf(stderr, "[main] FATAL: Redis connection failed\n");
        return 1;
    }

    /* Connect to MongoDB */
    MongoConn mongo;
    if (mongo_conn_init(&mongo, mongo_url, mongo_db) < 0) {
        fprintf(stderr, "[main] FATAL: MongoDB connection failed\n");
        redis_conn_close(&redis);
        return 1;
    }

    /* Create worker threads */
    pthread_t threads[MAX_WORKERS];
    WorkerCtx contexts[MAX_WORKERS];

    for (int i = 0; i < concurrency; i++) {
        contexts[i].redis       = &redis;
        contexts[i].mongo       = &mongo;
        contexts[i].worker_id   = i;
        contexts[i].tools_dir   = "";
        contexts[i].stockfish_path = sf_path ? sf_path : "stockfish";
        contexts[i].engine_depth  = engine_depth;
        contexts[i].job_timeout_secs = job_timeout;

        if (pthread_create(&threads[i], NULL, worker_thread, &contexts[i]) != 0) {
            fprintf(stderr, "[main] FATAL: pthread_create failed for worker %d\n", i);
            mongo_conn_close(&mongo);
            redis_conn_close(&redis);
            return 1;
        }
        fprintf(stderr, "[main] Started worker thread %d (tid=%lu)\n",
                i, (unsigned long)threads[i]);
    }

    fprintf(stderr, "[main] %d worker threads running, blocking...\n", concurrency);

    /* Wait for all threads */
    for (int i = 0; i < concurrency; i++) {
        pthread_join(threads[i], NULL);
    }

    mongo_conn_close(&mongo);
    redis_conn_close(&redis);
    return 0;
}