#ifndef ANALYZER_H
#define ANALYZER_H

#include "uci.h"

#define MAX_PLY 400
#define BOOK_PLIES 10
#define DEFAULT_DEPTH 18
#define MULTI_PV 2

typedef enum {
    CLASS_BOOK,
    CLASS_BEST,
    CLASS_EXCELLENT,
    CLASS_GOOD,
    CLASS_INACCURACY,
    CLASS_MISTAKE,
    CLASS_BLUNDER,
    CLASS_BRILLIANT
} Classification;

typedef struct {
    int    ply;
    char   color[6];
    char   san[16];
    char   fen_before[256];
    char   fen_after[256];
    int    eval_cp_played;
    int    eval_cp_best;
    int    is_mate_played;
    int    mate_in_played;
    int    is_mate_best;
    int    mate_in_best;
    char   best_move_san[16];
    double win_percent_loss;
    double move_accuracy;
    Classification classification;
    int    engine_depth;
    int    is_capture;
} MoveResult;

typedef struct {
    char   color[6];
    double accuracy_pct;
    double acpl;
    int    best_count;
    int    excellent_count;
    int    good_count;
    int    inaccuracy_count;
    int    mistake_count;
    int    blunder_count;
    int    brilliant_count;
    int    book_count;
    int    total_moves;
} PlayerSummary;

typedef struct {
    MoveResult    moves[MAX_PLY];
    int           num_moves;
    PlayerSummary white_summary;
    PlayerSummary black_summary;
} GameAnalysis;

double win_percent(double cp);
double move_accuracy(double wp_loss);
Classification classify(int cp_loss, int is_capture, int is_best_move,
                        int alt_move_exists, int alt_cp_diff, int ply);
const char *classification_str(Classification c);

void compute_player_summary(const MoveResult *moves, int num_moves,
                            const char *color, PlayerSummary *summary);

#endif
