#ifndef MONGO_CONN_H
#define MONGO_CONN_H

#include <mongoc/mongoc.h>

typedef struct {
    mongoc_client_t *client;
    mongoc_collection_t *games;
    char uri_str[512];
} MongoConn;

int mongo_conn_init(MongoConn *mc, const char *url, const char *db_name);
void mongo_conn_close(MongoConn *mc);
int mongo_update_analysis(MongoConn *mc, const char *game_id_hex,
                          const char *analysis_json, int num_moves,
                          const char *status, const char *error_msg);
int mongo_mark_running(MongoConn *mc, const char *game_id_hex);

#endif
