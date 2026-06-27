#include "uci.h"
#include "analyzer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <limits.h>

#define MAX_FENS (MAX_PLY + 10)
#define MAX_LINE 512
#define MAX_SANS (MAX_PLY + 10)
#define MAX_CAPTURES (MAX_PLY + 10)

static char *fgets_retry(char *str, int n, FILE *stream)
{
    char *res;
    while (1) {
        res = fgets(str, n, stream);
        if (res == NULL) {
            if (ferror(stream) && errno == EINTR) {
                clearerr(stream);
                continue;
            }
        }
        break;
    }
    return res;
}

static int sf_ensure_ready(StockfishProc *sf)
{
    char buf[SF_LINE_LEN];
    sf_send(sf, "uci");
    if (sf_read_until(sf, "uciok", buf, sizeof(buf)) < 0) return -1;
    sf_send(sf, "isready");
    if (sf_read_until(sf, "readyok", buf, sizeof(buf)) < 0) return -1;
    return 0;
}

static int sf_restart(StockfishProc *sf, const char *path)
{
    int saved_timeout = sf->move_timeout_ms;
    sf_kill(sf);
    if (sf_spawn(sf, path) < 0) return -1;
    sf->move_timeout_ms = saved_timeout;
    if (sf_ensure_ready(sf) < 0) {
        sf_kill(sf);
        return -1;
    }
    return 0;
}

static int read_fens(FILE *in, char fens[][256], int max)
{
    int count = 0;
    char line[256];
    while (fgets_retry(line, sizeof(line), in) && count < max) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[--len] = '\0';
        if (len == 0) continue;
        size_t flen = strlen(line);
        if (flen >= 255) flen = 254;
        memcpy(fens[count], line, flen);
        fens[count][flen] = '\0';
        count++;
    }
    return count;
}

static int parse_sidecar(const char *path, char sans[][16], int captures[], int max)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char buf[65536];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';

    int count = 0;
    char *p = strstr(buf, "\"sans\"");
    if (!p) return -1;
    p = strchr(p, '[');
    if (!p) return -1;
    p++;

    while (*p && *p != ']' && count < max) {
        while (*p == ' ' || *p == ',' || *p == '\n' || *p == '\r' || *p == '\t') p++;
        if (*p == '"') {
            p++;
            int i = 0;
            while (*p && *p != '"' && i < 15) {
                sans[count][i++] = *p++;
            }
            sans[count][i] = '\0';
            if (*p == '"') p++;
            count++;
        } else {
            break;
        }
    }

    char *q = strstr(buf, "\"captures\"");
    if (q) {
        q = strchr(q, '[');
        if (q) {
            q++;
            int ci = 0;
            while (*q && *q != ']' && ci < max) {
                while (*q == ' ' || *q == ',' || *q == '\n' || *q == '\r' || *q == '\t') q++;
                if (*q == '0' || *q == '1') {
                    captures[ci++] = (*q - '0');
                    q++;
                } else {
                    break;
                }
            }
        }
    }

    return count;
}

static const char *color_for_ply(int ply)
{
    return (ply % 2 == 1) ? "white" : "black";
}

static double wp_for_eval(int cp, int is_mate, int mate_in)
{
    if (is_mate) {
        return (mate_in > 0) ? 100.0 : 0.0;
    }
    return win_percent((double)cp);
}

int main(int argc, char *argv[])
{
    int depth = DEFAULT_DEPTH;
    const char *sidecar_path = NULL;
    int arg_idx = 1;

    while (arg_idx < argc) {
        if (strcmp(argv[arg_idx], "--fens") == 0 && arg_idx + 1 < argc) {
            arg_idx += 2;
        } else if (strcmp(argv[arg_idx], "--depth") == 0 && arg_idx + 1 < argc) {
            depth = atoi(argv[arg_idx + 1]);
            arg_idx += 2;
        } else if (strcmp(argv[arg_idx], "--sidecar") == 0 && arg_idx + 1 < argc) {
            sidecar_path = argv[arg_idx + 1];
            arg_idx += 2;
        } else {
            break;
        }
    }

    if (depth < 1 || depth > 30) depth = DEFAULT_DEPTH;

    StockfishProc sf;
    memset(&sf, 0, sizeof(sf));

    const char *sf_path = getenv("STOCKFISH_PATH");

    if (sf_spawn(&sf, sf_path) < 0) {
        fprintf(stderr, "Error: failed to spawn stockfish\n");
        return 1;
    }

    const char *move_timeout_env = getenv("MOVE_TIMEOUT_MS");
    if (move_timeout_env) sf.move_timeout_ms = atoi(move_timeout_env);

    char buf[SF_LINE_LEN];
    sf_send(&sf, "uci");
    if (sf_read_until(&sf, "uciok", buf, sizeof(buf)) < 0) {
        fprintf(stderr, "Error: no uciok\n");
        sf_kill(&sf);
        return 1;
    }
    sf_send(&sf, "isready");
    if (sf_read_until(&sf, "readyok", buf, sizeof(buf)) < 0) {
        fprintf(stderr, "Error: no readyok\n");
        sf_kill(&sf);
        return 1;
    }

    static char fens[MAX_FENS][256];
    int num_fens = read_fens(stdin, fens, MAX_FENS);
    if (num_fens < 2) {
        fprintf(stderr, "Error: need at least 2 FENs (start + 1 move)\n");
        sf_kill(&sf);
        return 1;
    }

    int num_moves = num_fens - 1;

    static char sans[MAX_SANS][16];
    static int captures[MAX_CAPTURES];
    memset(sans, 0, sizeof(sans));
    memset(captures, 0, sizeof(captures));

    if (sidecar_path) {
        parse_sidecar(sidecar_path, sans, captures, num_moves);
    }

    GameAnalysis analysis;
    memset(&analysis, 0, sizeof(analysis));
    analysis.num_moves = num_moves;

    printf("{\n  \"moves\": [\n");

    for (int i = 0; i < num_moves; i++) {
        int ply = i + 1;
        const char *color = color_for_ply(ply);
        const char *fen_before = fens[i];
        const char *fen_after = fens[i + 1];

        EvalResult results[MULTI_PV];
        int analyze_rc = sf_analyze_fen(&sf, fen_before, depth, MULTI_PV, results);

        if (analyze_rc < 0) {
            fprintf(stderr, "[analyze-game] move %d: engine timeout, restarting Stockfish\n", ply);

            const char *restart_path = getenv("STOCKFISH_PATH");
            if (sf_restart(&sf, restart_path) < 0) {
                fprintf(stderr, "[analyze-game] FATAL: could not restart Stockfish after timeout\n");
                sf_kill(&sf);
                return 1;
            }

            if (i > 0) printf(",\n");
            printf("    {\n");
            printf("      \"ply\": %d,\n", ply);
            printf("      \"color\": \"%s\",\n", color);
            if (sans[i][0])
                printf("      \"san\": \"%s\",\n", sans[i]);
            else
                printf("      \"san\": \"\",\n");
            printf("      \"fenBefore\": \"%s\",\n", fen_before);
            printf("      \"fenAfter\": \"%s\",\n", fen_after);
            printf("      \"evalCpPlayed\": null,\n");
            printf("      \"evalCpBest\": null,\n");
            printf("      \"evalMate\": null,\n");
            printf("      \"bestMoveUci\": \"\",\n");
            printf("      \"winPercentLoss\": 0.0,\n");
            printf("      \"moveAccuracy\": 0.0,\n");
            printf("      \"classification\": \"inaccuracy\",\n");
            printf("      \"engineDepth\": 0\n");
            printf("    }");

            MoveResult *m = &analysis.moves[i];
            m->ply = ply;
            snprintf(m->color, sizeof(m->color), "%s", color);
            if (sans[i][0]) {
                size_t slen = strlen(sans[i]);
                if (slen >= sizeof(m->san)) slen = sizeof(m->san) - 1;
                memcpy(m->san, sans[i], slen);
                m->san[slen] = '\0';
            }
            {
                size_t flen = strlen(fen_before);
                if (flen >= sizeof(m->fen_before)) flen = sizeof(m->fen_before) - 1;
                memcpy(m->fen_before, fen_before, flen);
                m->fen_before[flen] = '\0';
            }
            {
                size_t flen = strlen(fen_after);
                if (flen >= sizeof(m->fen_after)) flen = sizeof(m->fen_after) - 1;
                memcpy(m->fen_after, fen_after, flen);
                m->fen_after[flen] = '\0';
            }
            m->eval_cp_played = INT_MIN;
            m->eval_cp_best = INT_MIN;
            m->is_mate_played = 0;
            m->mate_in_played = 0;
            m->is_mate_best = 0;
            m->mate_in_best = 0;
            m->best_move_san[0] = '\0';
            m->win_percent_loss = 0.0;
            m->move_accuracy = 0.0;
            m->classification = CLASS_INACCURACY;
            m->engine_depth = 0;
            m->is_capture = (i < num_moves) ? captures[i] : 0;
            continue;
        }

        int is_white = (ply % 2 == 1);

        int cp_played = results[0].cp;
        int cp_best = results[0].cp;
        int is_mate_played = results[0].is_mate;
        int mate_in_played = results[0].mate_in;
        int is_mate_best = results[0].is_mate;
        int mate_in_best = results[0].mate_in;
        char best_move_uc[SF_MOVE_LEN];
        strncpy(best_move_uc, results[0].best_move, SF_MOVE_LEN - 1);
        best_move_uc[SF_MOVE_LEN - 1] = '\0';

        if (!is_white) {
            cp_played = -cp_played;
            cp_best = -cp_best;
        }

        if (results[1].best_move[0] != '\0') {
            int cp_alt = results[1].cp;
            if (!is_white) cp_alt = -cp_alt;

            if (is_mate_played && !results[1].is_mate) {
                cp_best = cp_played;
            } else if (!is_mate_played && results[1].is_mate) {
                cp_best = cp_alt;
                is_mate_best = 1;
                mate_in_best = results[1].mate_in;
                if (!is_white) mate_in_best = -mate_in_best;
            } else if (is_mate_played && results[1].is_mate) {
                int my_mate = is_white ? mate_in_played : -mate_in_played;
                int alt_mate = is_white ? results[1].mate_in : -results[1].mate_in;
                cp_best = (alt_mate > my_mate) ? cp_alt : cp_played;
                if (alt_mate > my_mate) {
                    is_mate_best = 1;
                    mate_in_best = results[1].mate_in;
                }
            } else {
                if (cp_alt > cp_played) {
                    cp_best = cp_alt;
                }
            }
        }

        int cp_loss = cp_best - cp_played;
        if (cp_loss < 0) cp_loss = 0;

        double wp_played = wp_for_eval(cp_played, is_mate_played, mate_in_played);
        double wp_best = wp_for_eval(cp_best, is_mate_best, mate_in_best);
        double wp_loss = wp_best - wp_played;
        if (wp_loss < 0.0) wp_loss = 0.0;

        double m_acc = move_accuracy(wp_loss);

        int is_best = (strcmp(results[0].best_move, best_move_uc) == 0) || (cp_loss == 0);
        int is_capture = (i < num_moves) ? captures[i] : 0;
        int alt_move_exists = (results[1].best_move[0] != '\0' &&
                               strcmp(results[1].best_move, results[0].best_move) != 0);
        int alt_cp_diff = alt_move_exists ? abs(cp_best - (!is_white ? -results[1].cp : results[1].cp)) : 9999;
        Classification cls = classify(cp_loss, is_capture, is_best, alt_move_exists, alt_cp_diff, ply);

        MoveResult *m = &analysis.moves[i];
        m->ply = ply;
        snprintf(m->color, sizeof(m->color), "%s", color);
        if (sans[i][0]) {
            size_t slen = strlen(sans[i]);
            if (slen >= sizeof(m->san)) slen = sizeof(m->san) - 1;
            memcpy(m->san, sans[i], slen);
            m->san[slen] = '\0';
        }
        {
            size_t flen = strlen(fen_before);
            if (flen >= sizeof(m->fen_before)) flen = sizeof(m->fen_before) - 1;
            memcpy(m->fen_before, fen_before, flen);
            m->fen_before[flen] = '\0';
        }
        {
            size_t flen = strlen(fen_after);
            if (flen >= sizeof(m->fen_after)) flen = sizeof(m->fen_after) - 1;
            memcpy(m->fen_after, fen_after, flen);
            m->fen_after[flen] = '\0';
        }
        m->eval_cp_played = cp_played;
        m->eval_cp_best = cp_best;
        m->is_mate_played = is_mate_played;
        m->mate_in_played = mate_in_played;
        m->is_mate_best = is_mate_best;
        m->mate_in_best = mate_in_best;
        snprintf(m->best_move_san, sizeof(m->best_move_san), "%s", best_move_uc);
        m->win_percent_loss = wp_loss;
        m->move_accuracy = m_acc;
        m->classification = cls;
        m->engine_depth = results[0].depth;
        m->is_capture = is_capture;

        if (i > 0) printf(",\n");

        printf("    {\n");
        printf("      \"ply\": %d,\n", ply);
        printf("      \"color\": \"%s\",\n", color);
        if (sans[i][0])
            printf("      \"san\": \"%s\",\n", sans[i]);
        else
            printf("      \"san\": \"\",\n");
        printf("      \"fenBefore\": \"%s\",\n", fen_before);
        printf("      \"fenAfter\": \"%s\",\n", fen_after);

        if (is_mate_played)
            printf("      \"evalCpPlayed\": null,\n");
        else
            printf("      \"evalCpPlayed\": %d,\n", cp_played);

        if (is_mate_best)
            printf("      \"evalCpBest\": null,\n");
        else
            printf("      \"evalCpBest\": %d,\n", cp_best);

        if (is_mate_played)
            printf("      \"evalMate\": %d,\n", mate_in_played);
        else
            printf("      \"evalMate\": null,\n");

        printf("      \"bestMoveUci\": \"%s\",\n", best_move_uc);
        printf("      \"winPercentLoss\": %.4f,\n", wp_loss);
        printf("      \"moveAccuracy\": %.1f,\n", m_acc);
        printf("      \"classification\": \"%s\",\n", classification_str(cls));
        printf("      \"engineDepth\": %d\n", results[0].depth);
        printf("    }");
    }

    printf("\n  ],\n");

    compute_player_summary(analysis.moves, analysis.num_moves, "white", &analysis.white_summary);
    compute_player_summary(analysis.moves, analysis.num_moves, "black", &analysis.black_summary);

    PlayerSummary *ws = &analysis.white_summary;
    PlayerSummary *bs = &analysis.black_summary;

    printf("  \"playerSummaries\": [\n");
    printf("    {\n");
    printf("      \"color\": \"white\",\n");
    printf("      \"accuracyPct\": %.1f,\n", ws->accuracy_pct);
    printf("      \"acpl\": %.1f,\n", ws->acpl);
    printf("      \"bestCount\": %d,\n", ws->best_count);
    printf("      \"excellentCount\": %d,\n", ws->excellent_count);
    printf("      \"goodCount\": %d,\n", ws->good_count);
    printf("      \"inaccuracyCount\": %d,\n", ws->inaccuracy_count);
    printf("      \"mistakeCount\": %d,\n", ws->mistake_count);
    printf("      \"blunderCount\": %d,\n", ws->blunder_count);
    printf("      \"brilliantCount\": %d,\n", ws->brilliant_count);
    printf("      \"bookCount\": %d,\n", ws->book_count);
    printf("      \"totalMoves\": %d\n", ws->total_moves);
    printf("    },\n");
    printf("    {\n");
    printf("      \"color\": \"black\",\n");
    printf("      \"accuracyPct\": %.1f,\n", bs->accuracy_pct);
    printf("      \"acpl\": %.1f,\n", bs->acpl);
    printf("      \"bestCount\": %d,\n", bs->best_count);
    printf("      \"excellentCount\": %d,\n", bs->excellent_count);
    printf("      \"goodCount\": %d,\n", bs->good_count);
    printf("      \"inaccuracyCount\": %d,\n", bs->inaccuracy_count);
    printf("      \"mistakeCount\": %d,\n", bs->mistake_count);
    printf("      \"blunderCount\": %d,\n", bs->blunder_count);
    printf("      \"brilliantCount\": %d,\n", bs->brilliant_count);
    printf("      \"bookCount\": %d,\n", bs->book_count);
    printf("      \"totalMoves\": %d\n", bs->total_moves);
    printf("    }\n");
    printf("  ]\n");
    printf("}\n");

    sf_send(&sf, "quit");
    sf_kill(&sf);

    return 0;
}
