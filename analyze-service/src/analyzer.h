#ifndef ANALYZER_H
#define ANALYZER_H

#include "uci.h"

typedef enum {
    BOOK, BEST, EXCELLENT, GOOD, INACCURACY, MISTAKE, BLUNDER, BRILLIANT
} Classification;

const char* classification_to_str(Classification c);
double win_percent(double cp);
double move_accuracy(double wp_loss);
Classification classify(int cp_loss, int is_sacrifice, int is_best_move, int is_book);

#endif
