#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include "worker.h"
#include "redis_conn.h"

#define MAX_THREADS 32

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    
    fprintf(stdout, "[main] Starting chess-analyze-worker\n");
    fflush(stdout);
    
    WorkerConfig config;
    strncpy(config.redis_url, getenv("REDIS_URL") ?: "redis://localhost:6379", sizeof(config.redis_url) - 1);
    strncpy(config.mongo_url, getenv("MONGO_URL") ?: "mongodb://localhost:27017", sizeof(config.mongo_url) - 1);
    
    config.depth = 18;
    const char *depth_env = getenv("ENGINE_DEPTH");
    if (depth_env) {
        config.depth = atoi(depth_env);
    }
    
    config.book_plies = 10;
    const char *bp_env = getenv("BOOK_PLIES");
    if (bp_env) {
        config.book_plies = atoi(bp_env);
    }
    
    int concurrency = 2;
    const char *concurrency_env = getenv("WORKER_CONCURRENCY");
    if (concurrency_env) {
        concurrency = atoi(concurrency_env);
    }
    if (concurrency > MAX_THREADS) {
        concurrency = MAX_THREADS;
    }
    if (concurrency < 1) {
        concurrency = 1;
    }
    
    mongoc_init();
    
    bson_error_t mongo_err;
    mongoc_uri_t *uri = mongoc_uri_new_with_error(config.mongo_url, &mongo_err);
    if (!uri) {
        fprintf(stderr, "[main] Failed to parse MONGO_URL: %s\n", mongo_err.message);
        mongoc_cleanup();
        return 1;
    }
    
    const char *db_name = mongoc_uri_get_database(uri);
    if (db_name && strlen(db_name) > 0) {
        strncpy(config.db_name, db_name, sizeof(config.db_name) - 1);
    } else {
        strcpy(config.db_name, "chess_analyzer");
    }
    
    fprintf(stdout, "[main] REDIS_URL=%s  MONGO_URL=%s  MONGO_DB=%s\n", config.redis_url, config.mongo_url, config.db_name);
    fprintf(stdout, "[main] Concurrency=%d  Engine depth=%d  Book plies=%d\n", concurrency, config.depth, config.book_plies);
    fflush(stdout);
    
    mongoc_client_pool_t *mongo_pool = mongoc_client_pool_new(uri);
    if (!mongo_pool) {
        fprintf(stderr, "[main] Failed to create MongoDB client pool\n");
        mongoc_uri_destroy(uri);
        mongoc_cleanup();
        return 1;
    }
    mongoc_client_pool_set_error_api(mongo_pool, MONGOC_ERROR_API_VERSION_2);
    
    // Ensure consumer group exists
    char redis_err[256];
    redisContext *redis = redis_connect(config.redis_url, redis_err, sizeof(redis_err));
    if (redis) {
        redisReply *reply = redisCommand(redis, "XGROUP CREATE chess:analysis-jobs workers $ MKSTREAM");
        if (reply) {
            if (reply->type == REDIS_REPLY_ERROR) {
                // If it is BUSYGROUP, we can ignore it
                if (strstr(reply->str, "BUSYGROUP") == NULL) {
                    fprintf(stderr, "[main] Redis XGROUP CREATE error: %s\n", reply->str);
                }
            } else {
                fprintf(stdout, "[main] Created Redis consumer group 'workers'\n");
                fflush(stdout);
            }
            freeReplyObject(reply);
        }
        redisFree(redis);
    } else {
        fprintf(stderr, "[main] Warning: Could not pre-create Redis consumer group: %s\n", redis_err);
    }
    
    pthread_t threads[MAX_THREADS];
    ThreadCtx thread_ctxs[MAX_THREADS];
    
    for (int i = 0; i < concurrency; i++) {
        thread_ctxs[i].config = &config;
        thread_ctxs[i].mongo_pool = mongo_pool;
        thread_ctxs[i].worker_id = i;
        if (pthread_create(&threads[i], NULL, worker_thread, &thread_ctxs[i]) != 0) {
            fprintf(stderr, "[main] Failed to create thread %d\n", i);
        }
    }
    
    for (int i = 0; i < concurrency; i++) {
        pthread_join(threads[i], NULL);
    }
    
    mongoc_client_pool_destroy(mongo_pool);
    mongoc_uri_destroy(uri);
    mongoc_cleanup();
    
    return 0;
}
