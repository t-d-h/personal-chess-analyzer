#include "analyzer.h"

#include <math.h>
#include <string.h>

double win_percent(double cp)
{
    if (cp > 10000.0) cp = 10000.0;
    if (cp < -10000.0) cp = -10000.0;
    return 50.0 + 50.0 * (2.0 / (1.0 + exp(-0.00368208 * cp)) - 1.0);
}

double move_accuracy(double wp_loss)
{
    double raw = 103.1668 * exp(-0.04354 * (wp_loss * 100.0)) - 3.1669;
    if (raw < 0.0) raw = 0.0;
    if (raw > 100.0) raw = 100.0;
    return raw;
}

Classification classify(int cp_loss, int is_capture, int is_best_move,
                        int alt_move_exists, int alt_cp_diff, int ply)
{
    if (ply <= BOOK_PLIES) return CLASS_BOOK;
    if (is_capture && is_best_move && alt_move_exists && alt_cp_diff < 50)
        return CLASS_BRILLIANT;
    if (cp_loss == 0) return CLASS_BEST;
    if (cp_loss < 50) return CLASS_EXCELLENT;
    if (cp_loss < 100) return CLASS_GOOD;
    if (cp_loss < 300) return CLASS_INACCURACY;
    if (cp_loss < 500) return CLASS_MISTAKE;
    return CLASS_BLUNDER;
}

const char *classification_str(Classification c)
{
    switch (c) {
    case CLASS_BOOK:        return "book";
    case CLASS_BEST:        return "best";
    case CLASS_EXCELLENT:   return "excellent";
    case CLASS_GOOD:        return "good";
    case CLASS_INACCURACY:  return "inaccuracy";
    case CLASS_MISTAKE:     return "mistake";
    case CLASS_BLUNDER:     return "blunder";
    case CLASS_BRILLIANT:   return "brilliant";
    }
    return "unknown";
}

void compute_player_summary(const MoveResult *moves, int num_moves,
                            const char *color, PlayerSummary *summary)
{
    memset(summary, 0, sizeof(PlayerSummary));
    snprintf(summary->color, sizeof(summary->color), "%s", color);

    double total_wp_loss = 0.0;
    double total_accuracy = 0.0;
    int count = 0;

    for (int i = 0; i < num_moves; i++) {
        if (strcmp(moves[i].color, color) != 0) continue;

        count++;
        total_wp_loss += moves[i].win_percent_loss;
        total_accuracy += moves[i].move_accuracy;

        switch (moves[i].classification) {
        case CLASS_BEST:       summary->best_count++; break;
        case CLASS_EXCELLENT:  summary->excellent_count++; break;
        case CLASS_GOOD:       summary->good_count++; break;
        case CLASS_INACCURACY: summary->inaccuracy_count++; break;
        case CLASS_MISTAKE:    summary->mistake_count++; break;
        case CLASS_BLUNDER:    summary->blunder_count++; break;
        case CLASS_BRILLIANT:  summary->brilliant_count++; break;
        case CLASS_BOOK:       summary->book_count++; break;
        }
    }

    summary->total_moves = count;
    if (count > 0) {
        summary->accuracy_pct = total_accuracy / count;
        summary->acpl = (total_wp_loss / count) * 100.0;
    }
}
