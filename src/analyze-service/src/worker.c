#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <math.h>
#include "worker.h"
#include "redis_conn.h"
#include "mongo_conn.h"
#include "uci.h"
#include "analyzer.h"

typedef struct {
    int ply;
    char color[8];
    char san[16];
    char fenBefore[256];
    char fenAfter[256];
    char mapping[4096];
    char status[16];
} MoveInfo;

typedef struct {
    double accuracy_sum;
    int cp_loss_sum;
    int non_book_count;
    int best_count;
    int excellent_count;
    int good_count;
    int inaccuracy_count;
    int mistake_count;
    int blunder_count;
    int brilliant_count;
    double accuracy_sum_opening;
    int count_opening;
    double accuracy_sum_midgame;
    int count_midgame;
    double accuracy_sum_endgame;
    int count_endgame;
} PlayerSummaryStats;

static int parse_line(char *line, char *fields[], int max_fields) {
    int count = 0;
    char *p = line;
    while (p && count < max_fields) {
        fields[count++] = p;
        char *next = strchr(p, '|');
        if (next) {
            *next = '\0';
            p = next + 1;
        } else {
            // Remove trailing newline/carriage return
            size_t len = strlen(p);
            while (len > 0 && (p[len - 1] == '\n' || p[len - 1] == '\r')) {
                p[len - 1] = '\0';
                len--;
            }
            break;
        }
    }
    return count;
}

static int count_major_minor_pieces(const char *fen) {
    int count = 0;
    for (int i = 0; fen[i] != '\0' && fen[i] != ' '; i++) {
        char c = fen[i];
        if (c == 'Q' || c == 'R' || c == 'B' || c == 'N' ||
            c == 'q' || c == 'r' || c == 'b' || c == 'n') {
            count++;
        }
    }
    return count;
}

static int find_san_for_uci(const char *mapping, const char *uci, char *out_san, size_t max_len) {
    if (!mapping || !uci || uci[0] == '\0') {
        return 0;
    }
    const char *p = mapping;
    size_t uci_len = strlen(uci);
    while (p) {
        if (strncmp(p, uci, uci_len) == 0 && p[uci_len] == ':') {
            const char *san_start = p + uci_len + 1;
            const char *comma = strchr(san_start, ',');
            size_t san_len;
            if (comma) {
                san_len = comma - san_start;
            } else {
                san_len = strlen(san_start);
            }
            if (san_len >= max_len) {
                san_len = max_len - 1;
            }
            strncpy(out_san, san_start, san_len);
            out_san[san_len] = '\0';
            return 1;
        }
        p = strchr(p, ',');
        if (p) {
            p++;
        }
    }
    return 0;
}

static int find_uci_for_san(const char *mapping, const char *san, char *out_uci, size_t max_len) {
    if (!mapping || !san || san[0] == '\0') {
        return 0;
    }
    const char *p = mapping;
    while (p) {
        const char *colon = strchr(p, ':');
        if (!colon) break;
        const char *comma = strchr(colon, ',');
        size_t san_len;
        if (comma) {
            san_len = comma - (colon + 1);
        } else {
            san_len = strlen(colon + 1);
        }
        if (san_len == strlen(san) && strncmp(colon + 1, san, san_len) == 0) {
            size_t uci_len = colon - p;
            if (uci_len >= max_len) {
                uci_len = max_len - 1;
            }
            strncpy(out_uci, p, uci_len);
            out_uci[uci_len] = '\0';
            return 1;
        }
        p = comma;
        if (p) {
            p++;
        }
    }
    return 0;
}

static void get_position_eval(StockfishProc *sf, const char *fen, int depth, int is_mate, int is_draw, EvalResult out[2]) {
    if (is_mate) {
        out[0].cp = -10000;
        out[0].is_mate = 1;
        out[0].mate_in = 0;
        out[0].best_move[0] = '\0';
        out[0].depth = depth;
        
        out[1].cp = -10000;
        out[1].is_mate = 1;
        out[1].mate_in = 0;
        out[1].best_move[0] = '\0';
        out[1].depth = depth;
    } else if (is_draw) {
        out[0].cp = 0;
        out[0].is_mate = 0;
        out[0].mate_in = 0;
        out[0].best_move[0] = '\0';
        out[0].depth = depth;
        
        out[1].cp = 0;
        out[1].is_mate = 0;
        out[1].mate_in = 0;
        out[1].best_move[0] = '\0';
        out[1].depth = depth;
    } else {
        sf_analyze_fen(sf, fen, depth, 2, out);
    }
}

static int spawn_pgn_to_fens(const char *pgn, FILE **out_stream, pid_t *child_pid) {
    int stdin_pipe[2];
    int stdout_pipe[2];
    if (pipe(stdin_pipe) < 0 || pipe(stdout_pipe) < 0) {
        return -1;
    }
    pid_t pid = fork();
    if (pid < 0) {
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        return -1;
    }
    if (pid == 0) {
        // Child
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stdout_pipe[1], STDERR_FILENO);
        
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        
        if (access("src/analyze-service/tools/pgn_to_fens.js", F_OK) == 0) {
            execlp("node", "node", "src/analyze-service/tools/pgn_to_fens.js", "-", NULL);
        } else if (access("analyze-service/tools/pgn_to_fens.js", F_OK) == 0) {
            execlp("node", "node", "analyze-service/tools/pgn_to_fens.js", "-", NULL);
        } else {
            execlp("node", "node", "tools/pgn_to_fens.js", "-", NULL);
        }
        perror("execlp node");
        exit(1);
    } else {
        // Parent
        close(stdin_pipe[0]);
        close(stdout_pipe[1]);
        
        // Write PGN to child stdin
        size_t pgn_len = strlen(pgn);
        ssize_t written = 0;
        while (written < (ssize_t)pgn_len) {
            ssize_t n = write(stdin_pipe[1], pgn + written, pgn_len - written);
            if (n < 0) {
                break;
            }
            written += n;
        }
        close(stdin_pipe[1]); // EOF
        
        *out_stream = fdopen(stdout_pipe[0], "r");
        *child_pid = pid;
        if (!*out_stream) {
            close(stdout_pipe[0]);
            return -1;
        }
        return 0;
    }
}

static bool run_game_analysis(const char *game_id, const char *pgn, redisContext *redis, mongoc_client_t *mongo_client, const WorkerConfig *config, int worker_id, char *err_msg, size_t err_msg_len) {
    FILE *node_out = NULL;
    pid_t node_pid = 0;
    
    if (spawn_pgn_to_fens(pgn, &node_out, &node_pid) < 0) {
        snprintf(err_msg, err_msg_len, "Failed to spawn pgn_to_fens.js process");
        return false;
    }
    
    MoveInfo *moves = calloc(2048, sizeof(MoveInfo));
    if (!moves) {
        snprintf(err_msg, err_msg_len, "Memory allocation failed for moves");
        fclose(node_out);
        kill(node_pid, SIGKILL);
        waitpid(node_pid, NULL, 0);
        return false;
    }
    
    int move_count = 0;
    char line[8192];
    char potential_err[1024] = {0};
    
    while (fgets(line, sizeof(line), node_out)) {
        char *fields[7];
        char line_copy[8192];
        strcpy(line_copy, line);
        
        int n_fields = parse_line(line_copy, fields, 7);
        if (n_fields < 7) {
            // Save potential error from node process
            size_t len = strlen(line);
            while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
                line[len - 1] = '\0';
                len--;
            }
            snprintf(potential_err, sizeof(potential_err), "%s", line);
            continue;
        }
        
        if (move_count >= 2048) {
            break;
        }
        
        MoveInfo *m = &moves[move_count];
        m->ply = atoi(fields[0]);
        strncpy(m->color, fields[1], sizeof(m->color) - 1);
        strncpy(m->san, fields[2], sizeof(m->san) - 1);
        strncpy(m->fenBefore, fields[3], sizeof(m->fenBefore) - 1);
        strncpy(m->fenAfter, fields[4], sizeof(m->fenAfter) - 1);
        strncpy(m->mapping, fields[5], sizeof(m->mapping) - 1);
        strncpy(m->status, fields[6], sizeof(m->status) - 1);
        
        move_count++;
    }
    
    fclose(node_out);
    
    int status = 0;
    waitpid(node_pid, &status, 0);
    if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        snprintf(err_msg, err_msg_len, "%s", potential_err[0] != '\0' ? potential_err : "pgn_to_fens.js exited with non-zero code");
        free(moves);
        return false;
    }
    
    if (move_count == 0) {
        snprintf(err_msg, err_msg_len, "No moves read from PGN");
        free(moves);
        return false;
    }
    
    const char *sf_path = getenv("STOCKFISH_PATH") ?: "stockfish";
    StockfishProc sf;
    if (sf_spawn(&sf, sf_path) != 0) {
        snprintf(err_msg, err_msg_len, "Failed to spawn Stockfish at %s", sf_path);
        free(moves);
        return false;
    }
    
    EvalResult (*evals)[2] = calloc(move_count + 1, sizeof(*evals));
    if (!evals) {
        snprintf(err_msg, err_msg_len, "Memory allocation failed for evaluations");
        sf_kill(&sf);
        free(moves);
        return false;
    }
    
    // Evaluate initial position (k = 0)
    get_position_eval(&sf, moves[0].fenBefore, config->depth, 0, 0, evals[0]);
    
    // Evaluate each position after each move
    for (int k = 1; k <= move_count; k++) {
        int is_mate = 0;
        int is_draw = 0;
        if (k == move_count) {
            if (strcmp(moves[k - 1].status, "checkmate") == 0) {
                is_mate = 1;
            } else if (strcmp(moves[k - 1].status, "draw") == 0) {
                is_draw = 1;
            }
        }
        
        get_position_eval(&sf, moves[k - 1].fenAfter, config->depth, is_mate, is_draw, evals[k]);
        
        // Update Redis progress
        redisReply *hset = redisCommand(redis, "HSET job:%s:progress status running movesAnalyzed %d movesTotal %d", game_id, k, move_count);
        if (hset) freeReplyObject(hset);
        redisReply *expire = redisCommand(redis, "EXPIRE job:%s:progress 3600", game_id);
        if (expire) freeReplyObject(expire);
    }
    
    sf_kill(&sf);
    
    // Generate JSON
    char *analysis_json = NULL;
    size_t analysis_size = 0;
    FILE *mem = open_memstream(&analysis_json, &analysis_size);
    if (!mem) {
        snprintf(err_msg, err_msg_len, "Failed to open memstream for JSON");
        free(evals);
        free(moves);
        return false;
    }
    
    PlayerSummaryStats white_stats = {0};
    PlayerSummaryStats black_stats = {0};
    
    fprintf(mem, "{\n  \"moves\": [\n");
    for (int i = 0; i < move_count; i++) {
        int ply = i + 1;
        MoveInfo *m = &moves[i];
        
        int best_is_mate = evals[i][0].is_mate;
        int best_mate_in = evals[i][0].mate_in;
        int evalCpBest = evals[i][0].cp;

        int played_is_mate = evals[i + 1][0].is_mate;
        int played_mate_in = -evals[i + 1][0].mate_in;
        int evalCpPlayed = -evals[i + 1][0].cp;

        double wp_best = win_percent(best_is_mate ? (best_mate_in >= 0 ? 10000 : -10000) : evalCpBest);
        double wp_played = win_percent(played_is_mate ? (played_mate_in >= 0 ? 10000 : -10000) : evalCpPlayed);
        double wp_loss = wp_best - wp_played;
        if (wp_loss < 0.0) wp_loss = 0.0;

        double accuracy = move_accuracy(wp_loss);

        char played_move_uci[8] = {0};
        find_uci_for_san(m->mapping, m->san, played_move_uci, sizeof(played_move_uci));

        int cp_loss = 0;
        int acpl_best = best_is_mate ? (best_mate_in >= 0 ? 1000 : -1000) : (evalCpBest > 1000 ? 1000 : (evalCpBest < -1000 ? -1000 : evalCpBest));
        int acpl_played = played_is_mate ? (played_mate_in >= 0 ? 1000 : -1000) : (evalCpPlayed > 1000 ? 1000 : (evalCpPlayed < -1000 ? -1000 : evalCpPlayed));
        cp_loss = acpl_best - acpl_played;
        if (cp_loss < 0) cp_loss = 0;

        int is_best_move = (strcmp(played_move_uci, evals[i][0].best_move) == 0 || cp_loss <= 0);

        int is_sacrifice = 0;
        int is_played_capture = (strchr(m->san, 'x') != NULL);
        char second_best_uci[8] = {0};
        strncpy(second_best_uci, evals[i][1].best_move, sizeof(second_best_uci) - 1);
        if (is_played_capture && second_best_uci[0] != '\0') {
            char second_best_san[16] = {0};
            if (find_san_for_uci(m->mapping, second_best_uci, second_best_san, sizeof(second_best_san))) {
                if (strchr(second_best_san, 'x') == NULL) {
                    is_sacrifice = 1;
                }
            }
        }

        int is_book = (ply <= config->book_plies);
        Classification c = classify(cp_loss, is_sacrifice, is_best_move, is_book);

        int phase = 1; // 0=Opening, 1=Midgame, 2=Endgame
        if (ply <= 20 || is_book) {
            phase = 0;
        } else if (count_major_minor_pieces(m->fenBefore) <= 6) {
            phase = 2;
        } else {
            phase = 1;
        }

        PlayerSummaryStats *stats = (i % 2 == 0) ? &white_stats : &black_stats;
        
        double phase_acc = is_book ? 100.0 : accuracy;
        if (phase == 0) {
            stats->accuracy_sum_opening += phase_acc;
            stats->count_opening++;
        } else if (phase == 1) {
            stats->accuracy_sum_midgame += phase_acc;
            stats->count_midgame++;
        } else {
            stats->accuracy_sum_endgame += phase_acc;
            stats->count_endgame++;
        }

        if (!is_book) {
            stats->non_book_count++;
            stats->accuracy_sum += accuracy;
            stats->cp_loss_sum += cp_loss;

            switch (c) {
                case BEST: stats->best_count++; break;
                case EXCELLENT: stats->excellent_count++; break;
                case GOOD: stats->good_count++; break;
                case INACCURACY: stats->inaccuracy_count++; break;
                case MISTAKE: stats->mistake_count++; break;
                case BLUNDER: stats->blunder_count++; break;
                case BRILLIANT: stats->brilliant_count++; break;
                default: break;
            }
        }

        fprintf(mem, "    {\n");
        fprintf(mem, "      \"ply\": %d,\n", ply);
        fprintf(mem, "      \"color\": \"%s\",\n", m->color);
        fprintf(mem, "      \"san\": \"%s\",\n", m->san);
        fprintf(mem, "      \"fenBefore\": \"%s\",\n", m->fenBefore);
        fprintf(mem, "      \"fenAfter\": \"%s\",\n", m->fenAfter);

        if (played_is_mate) {
            fprintf(mem, "      \"evalCpPlayed\": null,\n");
        } else {
            fprintf(mem, "      \"evalCpPlayed\": %d,\n", evalCpPlayed);
        }

        if (best_is_mate) {
            fprintf(mem, "      \"evalCpBest\": null,\n");
        } else {
            fprintf(mem, "      \"evalCpBest\": %d,\n", evalCpBest);
        }

        if (played_is_mate) {
            fprintf(mem, "      \"evalMate\": %d,\n", played_mate_in);
        } else {
            fprintf(mem, "      \"evalMate\": null,\n");
        }

        char best_move_san[16] = {0};
        if (find_san_for_uci(m->mapping, evals[i][0].best_move, best_move_san, sizeof(best_move_san))) {
            fprintf(mem, "      \"bestMoveSan\": \"%s\",\n", best_move_san);
        } else {
            fprintf(mem, "      \"bestMoveSan\": null,\n");
        }

        fprintf(mem, "      \"winPercentLoss\": %.4f,\n", wp_loss);
        fprintf(mem, "      \"moveAccuracy\": %.2f,\n", accuracy);
        fprintf(mem, "      \"classification\": \"%s\",\n", classification_to_str(c));
        fprintf(mem, "      \"engineDepth\": %d\n", config->depth);
        fprintf(mem, "    }%s\n", (i == move_count - 1) ? "" : ",");
    }
    fprintf(mem, "  ],\n");

    double white_acc = white_stats.non_book_count > 0 ? (white_stats.accuracy_sum / white_stats.non_book_count) : 100.0;
    int white_acpl = white_stats.non_book_count > 0 ? (int)round((double)white_stats.cp_loss_sum / white_stats.non_book_count) : 0;
    double black_acc = black_stats.non_book_count > 0 ? (black_stats.accuracy_sum / black_stats.non_book_count) : 100.0;
    int black_acpl = black_stats.non_book_count > 0 ? (int)round((double)black_stats.cp_loss_sum / black_stats.non_book_count) : 0;

    int white_rating = (int)fmax(100.0, round(white_acc * 35.0 - 500.0));
    int black_rating = (int)fmax(100.0, round(black_acc * 35.0 - 500.0));

    double w_op_acc = white_stats.count_opening > 0 ? (white_stats.accuracy_sum_opening / white_stats.count_opening) : 100.0;
    double w_mid_acc = white_stats.count_midgame > 0 ? (white_stats.accuracy_sum_midgame / white_stats.count_midgame) : 100.0;
    double w_end_acc = white_stats.count_endgame > 0 ? (white_stats.accuracy_sum_endgame / white_stats.count_endgame) : 100.0;

    double b_op_acc = black_stats.count_opening > 0 ? (black_stats.accuracy_sum_opening / black_stats.count_opening) : 100.0;
    double b_mid_acc = black_stats.count_midgame > 0 ? (black_stats.accuracy_sum_midgame / black_stats.count_midgame) : 100.0;
    double b_end_acc = black_stats.count_endgame > 0 ? (black_stats.accuracy_sum_endgame / black_stats.count_endgame) : 100.0;

    fprintf(mem, "  \"playerSummaries\": [\n");
    // White summary
    fprintf(mem, "    {\n");
    fprintf(mem, "      \"color\": \"white\",\n");
    fprintf(mem, "      \"accuracyPct\": %.1f,\n", white_acc);
    fprintf(mem, "      \"acpl\": %d,\n", white_acpl);
    fprintf(mem, "      \"bestCount\": %d,\n", white_stats.best_count);
    fprintf(mem, "      \"excellentCount\": %d,\n", white_stats.excellent_count);
    fprintf(mem, "      \"goodCount\": %d,\n", white_stats.good_count);
    fprintf(mem, "      \"inaccuracyCount\": %d,\n", white_stats.inaccuracy_count);
    fprintf(mem, "      \"mistakeCount\": %d,\n", white_stats.mistake_count);
    fprintf(mem, "      \"blunderCount\": %d,\n", white_stats.blunder_count);
    fprintf(mem, "      \"brilliantCount\": %d,\n", white_stats.brilliant_count);
    fprintf(mem, "      \"estimatedRating\": %d,\n", white_rating);
    fprintf(mem, "      \"openingAccuracy\": %.1f,\n", w_op_acc);
    fprintf(mem, "      \"midgameAccuracy\": %.1f,\n", w_mid_acc);
    fprintf(mem, "      \"endgameAccuracy\": %.1f\n", w_end_acc);
    fprintf(mem, "    },\n");
    // Black summary
    fprintf(mem, "    {\n");
    fprintf(mem, "      \"color\": \"black\",\n");
    fprintf(mem, "      \"accuracyPct\": %.1f,\n", black_acc);
    fprintf(mem, "      \"acpl\": %d,\n", black_acpl);
    fprintf(mem, "      \"bestCount\": %d,\n", black_stats.best_count);
    fprintf(mem, "      \"excellentCount\": %d,\n", black_stats.excellent_count);
    fprintf(mem, "      \"goodCount\": %d,\n", black_stats.good_count);
    fprintf(mem, "      \"inaccuracyCount\": %d,\n", black_stats.inaccuracy_count);
    fprintf(mem, "      \"mistakeCount\": %d,\n", black_stats.mistake_count);
    fprintf(mem, "      \"blunderCount\": %d,\n", black_stats.blunder_count);
    fprintf(mem, "      \"brilliantCount\": %d,\n", black_stats.brilliant_count);
    fprintf(mem, "      \"estimatedRating\": %d,\n", black_rating);
    fprintf(mem, "      \"openingAccuracy\": %.1f,\n", b_op_acc);
    fprintf(mem, "      \"midgameAccuracy\": %.1f,\n", b_mid_acc);
    fprintf(mem, "      \"endgameAccuracy\": %.1f\n", b_end_acc);
    fprintf(mem, "    }\n");
    fprintf(mem, "  ]\n");
    fprintf(mem, "}\n");

    fclose(mem);
    
    char mongo_err[512] = {0};
    bool mongo_ok = mongo_update_game_success(mongo_client, config->db_name, game_id, analysis_json, move_count, mongo_err, sizeof(mongo_err));
    
    if (!mongo_ok) {
        snprintf(err_msg, err_msg_len, "MongoDB update failed: %s", mongo_err);
        free(analysis_json);
        free(evals);
        free(moves);
        return false;
    }
    
    // Save analyzed game to Redis with 1-day (86400s) TTL
    redisReply *cache_set = redisCommand(redis, "SET game:%s:analysis %s EX 86400", game_id, analysis_json);
    if (cache_set) {
        freeReplyObject(cache_set);
    }
    
    free(analysis_json);
    free(evals);
    free(moves);
    
    // Update Redis progress
    redisReply *hset = redisCommand(redis, "HSET job:%s:progress status completed movesAnalyzed %d movesTotal %d", game_id, move_count, move_count);
    if (hset) freeReplyObject(hset);
    redisReply *expire = redisCommand(redis, "EXPIRE job:%s:progress 300", game_id);
    if (expire) freeReplyObject(expire);
    
    fprintf(stdout, "[worker-%d] completed game %s (%d moves)\n", worker_id, game_id, move_count);
    fflush(stdout);
    return true;
}

static void process_entry(redisReply *entry, redisContext *redis, mongoc_client_t *mongo_client, const WorkerConfig *config, int worker_id) {
    if (entry->type != REDIS_REPLY_ARRAY || entry->elements < 2) {
        return;
    }
    const char *entry_id = entry->element[0]->str;
    redisReply *fields = entry->element[1];
    
    char game_id[128] = {0};
    char *pgn = NULL;
    
    for (size_t i = 0; i < fields->elements; i += 2) {
        const char *key = fields->element[i]->str;
        const char *val = fields->element[i+1]->str;
        if (strcmp(key, "gameId") == 0) {
            strncpy(game_id, val, sizeof(game_id) - 1);
        } else if (strcmp(key, "pgn") == 0) {
            pgn = strdup(val);
        }
    }
    
    if (game_id[0] == '\0' || !pgn) {
        fprintf(stderr, "[worker-%d] Invalid entry format\n", worker_id);
        if (pgn) free(pgn);
        redisReply *ack = redisCommand(redis, "XACK chess:analysis-jobs workers %s", entry_id);
        if (ack) freeReplyObject(ack);
        return;
    }
    
    fprintf(stdout, "[worker-%d] processing game %s (entry_id=%s)\n", worker_id, game_id, entry_id);
    fflush(stdout);
    
    char err_msg[512] = {0};
    bool success = run_game_analysis(game_id, pgn, redis, mongo_client, config, worker_id, err_msg, sizeof(err_msg));
    
    if (!success) {
        fprintf(stderr, "[worker-%d] Failed processing game %s: %s\n", worker_id, game_id, err_msg);
        fflush(stderr);
        
        // Update Redis progress to failed
        redisReply *hset = redisCommand(redis, "HSET job:%s:progress status failed errorMessage %s", game_id, err_msg);
        if (hset) freeReplyObject(hset);
        redisReply *expire = redisCommand(redis, "EXPIRE job:%s:progress 300", game_id);
        if (expire) freeReplyObject(expire);
        
        // Update Mongo to failed
        char mongo_err[512];
        mongo_update_game_failed(mongo_client, config->db_name, game_id, err_msg, mongo_err, sizeof(mongo_err));
    }
    
    // Always acknowledge after MongoDB update
    redisReply *ack = redisCommand(redis, "XACK chess:analysis-jobs workers %s", entry_id);
    if (ack) freeReplyObject(ack);
    
    free(pgn);
}

void *worker_thread(void *arg) {
    ThreadCtx *ctx = (ThreadCtx *)arg;
    const WorkerConfig *config = ctx->config;
    
    char redis_err[256];
    redisContext *redis = redis_connect(config->redis_url, redis_err, sizeof(redis_err));
    if (!redis) {
        fprintf(stderr, "[worker-%d] Failed to connect to Redis: %s\n", ctx->worker_id, redis_err);
        return NULL;
    }
    
    fprintf(stdout, "[worker-%d] connected to Redis\n", ctx->worker_id);
    fflush(stdout);
    
    mongoc_client_t *mongo_client = mongoc_client_pool_pop(ctx->mongo_pool);
    if (!mongo_client) {
        fprintf(stderr, "[worker-%d] Failed to pop Mongo client from pool\n", ctx->worker_id);
        redisFree(redis);
        return NULL;
    }
    
    char consumer_name[128];
    snprintf(consumer_name, sizeof(consumer_name), "worker-%d-%lu", getpid(), (unsigned long)pthread_self());
    
    fprintf(stdout, "[worker-%d] Started worker thread (tid=%lu)\n", ctx->worker_id, (unsigned long)pthread_self());
    fflush(stdout);
    
    // 1. Reclaim old jobs (XAUTOCLAIM)
    char last_id[64] = "0-0";
    while (1) {
        redisReply *reply = redisCommand(redis, "XAUTOCLAIM chess:analysis-jobs workers %s 600000 %s COUNT 10", consumer_name, last_id);
        if (!reply) {
            break;
        }
        if (reply->type == REDIS_REPLY_ERROR) {
            fprintf(stderr, "[worker-%d] XAUTOCLAIM error: %s\n", ctx->worker_id, reply->str);
            fflush(stderr);
            freeReplyObject(reply);
            break;
        }
        if (reply->type != REDIS_REPLY_ARRAY || reply->elements < 2) {
            freeReplyObject(reply);
            break;
        }
        
        strncpy(last_id, reply->element[0]->str, sizeof(last_id) - 1);
        last_id[sizeof(last_id) - 1] = '\0';
        
        redisReply *entries = reply->element[1];
        if (entries->type == REDIS_REPLY_ARRAY && entries->elements > 0) {
            for (size_t i = 0; i < entries->elements; i++) {
                redisReply *entry = entries->element[i];
                process_entry(entry, redis, mongo_client, config, ctx->worker_id);
            }
        } else {
            freeReplyObject(reply);
            break;
        }
        freeReplyObject(reply);
        if (strcmp(last_id, "0-0") == 0) {
            break;
        }
    }
    
    // 2. Main block/read loop
    while (1) {
        redisReply *reply = redisCommand(redis, "XREADGROUP GROUP workers %s COUNT 1 BLOCK 5000 STREAMS chess:analysis-jobs >", consumer_name);
        if (!reply) {
            sleep(1);
            continue;
        }
        if (reply->type == REDIS_REPLY_ERROR) {
            fprintf(stderr, "[worker-%d] XREADGROUP error: %s\n", ctx->worker_id, reply->str);
            fflush(stderr);
            freeReplyObject(reply);
            sleep(1);
            continue;
        }
        
        if (reply->type == REDIS_REPLY_ARRAY && reply->elements > 0) {
            redisReply *stream_reply = reply->element[0];
            if (stream_reply->type == REDIS_REPLY_ARRAY && stream_reply->elements >= 2) {
                redisReply *entries = stream_reply->element[1];
                if (entries->type == REDIS_REPLY_ARRAY && entries->elements > 0) {
                    for (size_t i = 0; i < entries->elements; i++) {
                        redisReply *entry = entries->element[i];
                        process_entry(entry, redis, mongo_client, config, ctx->worker_id);
                    }
                }
            }
        }
        freeReplyObject(reply);
    }
    
    mongoc_client_pool_push(ctx->mongo_pool, mongo_client);
    redisFree(redis);
    return NULL;
}
