#ifndef MONGO_CONN_H
#define MONGO_CONN_H

#include <mongoc/mongoc.h>
#include <stdbool.h>

bool mongo_update_game_success(mongoc_client_t *client, const char *db_name, const char *game_id, const char *analysis_json, int moves_count, char *err_buf, size_t err_len);
bool mongo_update_game_failed(mongoc_client_t *client, const char *db_name, const char *game_id, const char *error_msg, char *err_buf, size_t err_len);

#endif
