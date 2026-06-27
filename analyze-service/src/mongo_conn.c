#include "mongo_conn.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

bool mongo_update_game_success(mongoc_client_t *client, const char *db_name, const char *game_id, const char *analysis_json, int moves_count, char *err_buf, size_t err_len) {
    mongoc_collection_t *collection = mongoc_client_get_collection(client, db_name, "games");
    
    bson_oid_t oid;
    if (!bson_oid_is_valid(game_id, strlen(game_id))) {
        snprintf(err_buf, err_len, "Invalid Game OID: %s", game_id);
        mongoc_collection_destroy(collection);
        return false;
    }
    bson_oid_init_from_string(&oid, game_id);
    
    bson_t selector;
    bson_init(&selector);
    bson_append_oid(&selector, "_id", 3, &oid);
    
    bson_error_t error;
    bson_t *parsed_analysis = bson_new_from_json((const uint8_t *)analysis_json, -1, &error);
    if (!parsed_analysis) {
        snprintf(err_buf, err_len, "Failed to parse analysis JSON: %s", error.message);
        bson_destroy(&selector);
        mongoc_collection_destroy(collection);
        return false;
    }
    
    uint32_t moves_len = 0;
    const uint8_t *moves_data = NULL;
    uint32_t summaries_len = 0;
    const uint8_t *summaries_data = NULL;
    
    bson_iter_t iter;
    if (bson_iter_init_find(&iter, parsed_analysis, "moves") && BSON_ITER_HOLDS_ARRAY(&iter)) {
        bson_iter_array(&iter, &moves_len, &moves_data);
    }
    if (bson_iter_init_find(&iter, parsed_analysis, "playerSummaries") && BSON_ITER_HOLDS_ARRAY(&iter)) {
        bson_iter_array(&iter, &summaries_len, &summaries_data);
    }
    
    bson_t moves_bson;
    if (moves_data) {
        bson_init_static(&moves_bson, moves_data, moves_len);
    }
    bson_t summaries_bson;
    if (summaries_data) {
        bson_init_static(&summaries_bson, summaries_data, summaries_len);
    }
    
    bson_t update;
    bson_t set_doc;
    bson_init(&update);
    if (bson_append_document_begin(&update, "$set", 4, &set_doc)) {
        bson_append_utf8(&set_doc, "analysis.status", 15, "completed", -1);
        bson_append_int32(&set_doc, "analysis.movesAnalyzed", 22, moves_count);
        
        int64_t now_ms = (int64_t)time(NULL) * 1000;
        bson_append_date_time(&set_doc, "analysis.completedAt", 20, now_ms);
        bson_append_date_time(&set_doc, "analysis.updatedAt", 18, now_ms);
        
        if (moves_data) {
            bson_append_array(&set_doc, "analysis.moves", 14, &moves_bson);
        }
        if (summaries_data) {
            bson_append_array(&set_doc, "analysis.playerSummaries", 24, &summaries_bson);
        }
        bson_append_document_end(&update, &set_doc);
    }
    
    bool success = mongoc_collection_update_one(collection, &selector, &update, NULL, NULL, &error);
    if (!success) {
        snprintf(err_buf, err_len, "%s", error.message);
    }
    
    bson_destroy(parsed_analysis);
    bson_destroy(&selector);
    bson_destroy(&update);
    mongoc_collection_destroy(collection);
    return success;
}

bool mongo_update_game_failed(mongoc_client_t *client, const char *db_name, const char *game_id, const char *error_msg, char *err_buf, size_t err_len) {
    mongoc_collection_t *collection = mongoc_client_get_collection(client, db_name, "games");
    
    bson_oid_t oid;
    if (!bson_oid_is_valid(game_id, strlen(game_id))) {
        snprintf(err_buf, err_len, "Invalid Game OID: %s", game_id);
        mongoc_collection_destroy(collection);
        return false;
    }
    bson_oid_init_from_string(&oid, game_id);
    
    bson_t selector;
    bson_init(&selector);
    bson_append_oid(&selector, "_id", 3, &oid);
    
    bson_t update;
    bson_t set_doc;
    bson_init(&update);
    if (bson_append_document_begin(&update, "$set", 4, &set_doc)) {
        bson_append_utf8(&set_doc, "analysis.status", 15, "failed", -1);
        bson_append_utf8(&set_doc, "analysis.errorMessage", 21, error_msg, -1);
        int64_t now_ms = (int64_t)time(NULL) * 1000;
        bson_append_date_time(&set_doc, "analysis.updatedAt", 18, now_ms);
        bson_append_document_end(&update, &set_doc);
    }
    
    bson_error_t error;
    bool success = mongoc_collection_update_one(collection, &selector, &update, NULL, NULL, &error);
    if (!success) {
        snprintf(err_buf, err_len, "%s", error.message);
    }
    
    bson_destroy(&selector);
    bson_destroy(&update);
    mongoc_collection_destroy(collection);
    return success;
}
