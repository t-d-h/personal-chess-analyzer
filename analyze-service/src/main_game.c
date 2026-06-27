#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
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

int parse_line(char *line, char *fields[], int max_fields) {
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

int find_san_for_uci(const char *mapping, const char *uci, char *out_san, size_t max_len) {
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

int find_uci_for_san(const char *mapping, const char *san, char *out_uci, size_t max_len) {
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

void get_position_eval(StockfishProc *sf, const char *fen, int depth, int is_mate, int is_draw, EvalResult out[2]) {
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

int main(int argc, char *argv[]) {
    int has_fens = 0;
    int book_plies = 10;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--fens") == 0 && i + 1 < argc && strcmp(argv[i+1], "-") == 0) {
            has_fens = 1;
            i++;
        } else if (strcmp(argv[i], "--book-plies") == 0 && i + 1 < argc) {
            book_plies = atoi(argv[i + 1]);
            i++;
        }
    }

    if (!has_fens) {
        fprintf(stderr, "Usage: analyze-game --fens - [--book-plies N]\n");
        return 1;
    }

    // Try reading environment variable BOOK_PLIES as override
    const char *bp_env = getenv("BOOK_PLIES");
    if (bp_env) {
        book_plies = atoi(bp_env);
    }

    static MoveInfo moves[1024];
    int move_count = 0;
    char line[8192];

    while (fgets(line, sizeof(line), stdin)) {
        if (line[0] == '\n' || line[0] == '\r') continue;
        
        char *fields[7];
        int n_fields = parse_line(line, fields, 7);
        if (n_fields < 7) {
            fprintf(stderr, "Invalid input line: %s\n", line);
            continue;
        }

        MoveInfo *m = &moves[move_count];
        m->ply = atoi(fields[0]);
        strncpy(m->color, fields[1], sizeof(m->color) - 1);
        m->color[sizeof(m->color) - 1] = '\0';
        strncpy(m->san, fields[2], sizeof(m->san) - 1);
        m->san[sizeof(m->san) - 1] = '\0';
        strncpy(m->fenBefore, fields[3], sizeof(m->fenBefore) - 1);
        m->fenBefore[sizeof(m->fenBefore) - 1] = '\0';
        strncpy(m->fenAfter, fields[4], sizeof(m->fenAfter) - 1);
        m->fenAfter[sizeof(m->fenAfter) - 1] = '\0';
        strncpy(m->mapping, fields[5], sizeof(m->mapping) - 1);
        m->mapping[sizeof(m->mapping) - 1] = '\0';
        strncpy(m->status, fields[6], sizeof(m->status) - 1);
        m->status[sizeof(m->status) - 1] = '\0';

        move_count++;
        if (move_count >= 1024) break;
    }

    if (move_count == 0) {
        fprintf(stderr, "No moves read from stdin\n");
        return 1;
    }

    int depth = 18;
    const char *sf_path = getenv("STOCKFISH_PATH");
    if (!sf_path) {
        sf_path = "stockfish";
    }

    StockfishProc sf;
    if (sf_spawn(&sf, sf_path) != 0) {
        fprintf(stderr, "Failed to spawn Stockfish at path %s\n", sf_path);
        return 1;
    }

    EvalResult (*evals)[2] = calloc(move_count + 1, sizeof(*evals));
    if (!evals) {
        fprintf(stderr, "Failed to allocate memory for evaluations\n");
        sf_kill(&sf);
        return 1;
    }

    // Evaluate initial board position (k = 0)
    get_position_eval(&sf, moves[0].fenBefore, depth, 0, 0, evals[0]);

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
        get_position_eval(&sf, moves[k - 1].fenAfter, depth, is_mate, is_draw, evals[k]);
    }

    sf_kill(&sf);

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
    } PlayerSummaryStats;

    PlayerSummaryStats white_stats = {0};
    PlayerSummaryStats black_stats = {0};

    printf("{\n  \"moves\": [\n");

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

        int is_book = (ply <= book_plies);
        Classification c = classify(cp_loss, is_sacrifice, is_best_move, is_book);

        PlayerSummaryStats *stats = (i % 2 == 0) ? &white_stats : &black_stats;
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

        printf("    {\n");
        printf("      \"ply\": %d,\n", ply);
        printf("      \"color\": \"%s\",\n", m->color);
        printf("      \"san\": \"%s\",\n", m->san);
        printf("      \"fenBefore\": \"%s\",\n", m->fenBefore);
        printf("      \"fenAfter\": \"%s\",\n", m->fenAfter);

        if (played_is_mate) {
            printf("      \"evalCpPlayed\": null,\n");
        } else {
            printf("      \"evalCpPlayed\": %d,\n", evalCpPlayed);
        }

        if (best_is_mate) {
            printf("      \"evalCpBest\": null,\n");
        } else {
            printf("      \"evalCpBest\": %d,\n", evalCpBest);
        }

        if (played_is_mate) {
            printf("      \"evalMate\": %d,\n", played_mate_in);
        } else {
            printf("      \"evalMate\": null,\n");
        }

        char best_move_san[16] = {0};
        if (find_san_for_uci(m->mapping, evals[i][0].best_move, best_move_san, sizeof(best_move_san))) {
            printf("      \"bestMoveSan\": \"%s\",\n", best_move_san);
        } else {
            printf("      \"bestMoveSan\": null,\n");
        }

        printf("      \"winPercentLoss\": %.4f,\n", wp_loss);
        printf("      \"moveAccuracy\": %.2f,\n", accuracy);
        printf("      \"classification\": \"%s\",\n", classification_to_str(c));
        printf("      \"engineDepth\": %d\n", depth);
        printf("    }%s\n", (i == move_count - 1) ? "" : ",");
    }

    printf("  ],\n");

    double white_acc = white_stats.non_book_count > 0 ? (white_stats.accuracy_sum / white_stats.non_book_count) : 100.0;
    int white_acpl = white_stats.non_book_count > 0 ? (int)round((double)white_stats.cp_loss_sum / white_stats.non_book_count) : 0;
    double black_acc = black_stats.non_book_count > 0 ? (black_stats.accuracy_sum / black_stats.non_book_count) : 100.0;
    int black_acpl = black_stats.non_book_count > 0 ? (int)round((double)black_stats.cp_loss_sum / black_stats.non_book_count) : 0;

    printf("  \"playerSummaries\": [\n");
    // White summary
    printf("    {\n");
    printf("      \"color\": \"white\",\n");
    printf("      \"accuracyPct\": %.1f,\n", white_acc);
    printf("      \"acpl\": %d,\n", white_acpl);
    printf("      \"bestCount\": %d,\n", white_stats.best_count);
    printf("      \"excellentCount\": %d,\n", white_stats.excellent_count);
    printf("      \"goodCount\": %d,\n", white_stats.good_count);
    printf("      \"inaccuracyCount\": %d,\n", white_stats.inaccuracy_count);
    printf("      \"mistakeCount\": %d,\n", white_stats.mistake_count);
    printf("      \"blunderCount\": %d,\n", white_stats.blunder_count);
    printf("      \"brilliantCount\": %d\n", white_stats.brilliant_count);
    printf("    },\n");
    // Black summary
    printf("    {\n");
    printf("      \"color\": \"black\",\n");
    printf("      \"accuracyPct\": %.1f,\n", black_acc);
    printf("      \"acpl\": %d,\n", black_acpl);
    printf("      \"bestCount\": %d,\n", black_stats.best_count);
    printf("      \"excellentCount\": %d,\n", black_stats.excellent_count);
    printf("      \"goodCount\": %d,\n", black_stats.good_count);
    printf("      \"inaccuracyCount\": %d,\n", black_stats.inaccuracy_count);
    printf("      \"mistakeCount\": %d,\n", black_stats.mistake_count);
    printf("      \"blunderCount\": %d,\n", black_stats.blunder_count);
    printf("      \"brilliantCount\": %d\n", black_stats.brilliant_count);
    printf("    }\n");
    printf("  ]\n");
    printf("}\n");

    free(evals);
    return 0;
}
