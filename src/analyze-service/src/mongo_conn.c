#include "mongo_conn.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bson/bson.h>
#include <unistd.h>

int mongo_conn_init(MongoConn *mc, const char *url, const char *db_name)
{
    memset(mc, 0, sizeof(MongoConn));
    snprintf(mc->uri_str, sizeof(mc->uri_str), "%s", url);

    mongoc_init();

    bson_error_t error;
    mongoc_uri_t *uri = mongoc_uri_new_with_error(mc->uri_str, &error);
    if (!uri) {
        fprintf(stderr, "[mongo] invalid URI: %s\n", error.message);
        return -1;
    }

    mc->client = mongoc_client_new_from_uri(uri);
    mongoc_uri_destroy(uri);

    if (!mc->client) {
        fprintf(stderr, "[mongo] failed to create client\n");
        return -1;
    }

    mongoc_client_set_appname(mc->client, "chess-analyze-worker");

    mongoc_database_t *db = mongoc_client_get_database(mc->client, db_name);
    mc->games = mongoc_database_get_collection(db, "games");
    mongoc_database_destroy(db);

    int max_retries = 5;
    bson_t *cmd = BCON_NEW("ping", BCON_INT32(1));
    bool connected = false;
    for (int attempt = 1; attempt <= max_retries; attempt++) {
        bson_error_t ping_error;
        if (mongoc_client_command_simple(mc->client, db_name, cmd, NULL, NULL, &ping_error)) {
            connected = true;
            break;
        }
        fprintf(stderr, "[mongo] ping attempt %d/%d failed: %s. Retrying in 1s...\n",
                attempt, max_retries, ping_error.message);
        sleep(1);
    }
    bson_destroy(cmd);

    if (!connected) {
        return -1;
    }

    fprintf(stderr, "[mongo] connected to %s (db=%s)\n", url, db_name);
    return 0;
}

void mongo_conn_close(MongoConn *mc)
{
    if (mc->games) {
        mongoc_collection_destroy(mc->games);
        mc->games = NULL;
    }
    if (mc->client) {
        mongoc_client_destroy(mc->client);
        mc->client = NULL;
    }
    mongoc_cleanup();
}

static bson_oid_t oid_from_hex(const char *hex)
{
    bson_oid_t oid;
    bson_oid_init_from_string(&oid, hex);
    return oid;
}

int mongo_mark_running(MongoConn *mc, const char *game_id_hex)
{
    bson_oid_t oid = oid_from_hex(game_id_hex);
    bson_t *query = BCON_NEW("_id", BCON_OID(&oid));

    bson_t *update = BCON_NEW(
        "$set", "{",
            "analysis.status", BCON_UTF8("running"),
            "analysis.updatedAt", BCON_DATE_TIME(bson_get_monotonic_time() / 1000),
        "}"
    );

    bson_error_t error;
    bool ok = mongoc_collection_update_one(mc->games, query, update, NULL, NULL, &error);

    bson_destroy(query);
    bson_destroy(update);

    if (!ok) {
        fprintf(stderr, "[mongo] mark_running failed: %s\n", error.message);
        return -1;
    }
    return 0;
}

int mongo_update_analysis(MongoConn *mc, const char *game_id_hex,
                          const char *analysis_json, int num_moves,
                          const char *status, const char *error_msg)
{
    bson_oid_t oid = oid_from_hex(game_id_hex);
    bson_t *query = BCON_NEW("_id", BCON_OID(&oid));

    bson_error_t bson_error;
    bson_t *analysis_doc = bson_new_from_json(
        (const uint8_t *)analysis_json, strlen(analysis_json), &bson_error
    );
    if (!analysis_doc) {
        fprintf(stderr, "[mongo] failed to parse analysis JSON: %s\n", bson_error.message);
        bson_destroy(query);
        return -1;
    }

    bson_t update;
    bson_init(&update);
    bson_t child;
    bson_append_document_begin(&update, "$set", 4, &child);
    bson_append_utf8(&child, "analysis.status", 15, status, (int)strlen(status));
    if (strcmp(status, "completed") == 0) {
        bson_append_date_time(&child, "analysis.completedAt", 20,
                              bson_get_monotonic_time() / 1000);
    }
    if (error_msg) {
        bson_append_utf8(&child, "analysis.errorMessage", 21, error_msg, (int)strlen(error_msg));
    } else {
        bson_append_null(&child, "analysis.errorMessage", 21);
    }
    bson_append_date_time(&child, "analysis.updatedAt", 19,
                          bson_get_monotonic_time() / 1000);
    bson_append_int32(&child, "analysis.movesAnalyzed", 22, num_moves);

    bson_iter_t iter;
    bson_iter_t moves_iter;
    bson_iter_t summaries_iter;

    if (bson_iter_init(&iter, analysis_doc) &&
        bson_iter_find_descendant(&iter, "moves", &moves_iter) &&
        BSON_ITER_HOLDS_ARRAY(&moves_iter)) {
        uint32_t array_len;
        const uint8_t *array_data;
        bson_iter_array(&moves_iter, &array_len, &array_data);
        
        bson_t moves_array;
        bson_init_static(&moves_array, array_data, array_len);
        bson_append_array(&child, "analysis.moves", 14, &moves_array);
    } else {
        bson_t empty_array;
        bson_init(&empty_array);
        bson_append_array(&child, "analysis.moves", 14, &empty_array);
        bson_destroy(&empty_array);
    }

    if (bson_iter_init(&iter, analysis_doc) &&
        bson_iter_find_descendant(&iter, "playerSummaries", &summaries_iter) &&
        BSON_ITER_HOLDS_ARRAY(&summaries_iter)) {
        uint32_t array_len;
        const uint8_t *array_data;
        bson_iter_array(&summaries_iter, &array_len, &array_data);
        
        bson_t summaries_array;
        bson_init_static(&summaries_array, array_data, array_len);
        bson_append_array(&child, "analysis.playerSummaries", 24, &summaries_array);
    } else {
        bson_t empty_array;
        bson_init(&empty_array);
        bson_append_array(&child, "analysis.playerSummaries", 24, &empty_array);
        bson_destroy(&empty_array);
    }

    bson_append_document_end(&update, &child);

    bson_error_t error;
    bool ok = mongoc_collection_update_one(mc->games, query, &update, NULL, NULL, &error);

    bson_destroy(query);
    bson_destroy(analysis_doc);
    bson_destroy(&update);

    if (!ok) {
        fprintf(stderr, "[mongo] update_analysis failed: %s\n", error.message);
        return -1;
    }
    return 0;
}
