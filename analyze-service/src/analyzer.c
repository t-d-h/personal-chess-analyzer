#include "analyzer.h"
#include <math.h>
#include <string.h>

const char* classification_to_str(Classification c) {
    switch (c) {
        case BOOK: return "book";
        case BEST: return "best";
        case EXCELLENT: return "excellent";
        case GOOD: return "good";
        case INACCURACY: return "inaccuracy";
        case MISTAKE: return "mistake";
        case BLUNDER: return "blunder";
        case BRILLIANT: return "brilliant";
        default: return "unknown";
    }
}

double win_percent(double cp) {
    if (cp > 10000.0) cp = 10000.0;
    if (cp < -10000.0) cp = -10000.0;
    return 50.0 + 50.0 * (2.0 / (1.0 + exp(-0.00368208 * cp)) - 1.0);
}

double move_accuracy(double wp_loss) {
    double raw = 103.1668 * exp(-0.04354 * wp_loss) - 3.1669;
    if (raw < 0.0) raw = 0.0;
    if (raw > 100.0) raw = 100.0;
    return raw;
}

Classification classify(int cp_loss, int is_sacrifice, int is_best_move, int is_book) {
    if (is_book) return BOOK;
    if (is_sacrifice && is_best_move) return BRILLIANT;
    if (cp_loss <= 0) return BEST;
    if (cp_loss < 50) return EXCELLENT;
    if (cp_loss < 100) return GOOD;
    if (cp_loss < 300) return INACCURACY;
    if (cp_loss < 500) return MISTAKE;
    return BLUNDER;
}
