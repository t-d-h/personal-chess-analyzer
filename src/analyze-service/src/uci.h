#ifndef UCI_H
#define UCI_H

#include <stdio.h>
#include <stddef.h>

#define SF_MOVE_LEN 8
#define SF_LINE_LEN 4096

typedef struct {
    int  pid;
    int  stdin_fd;
    int  stdout_fd;
    FILE *out_stream;
} StockfishProc;

int  sf_spawn(StockfishProc *sf, const char *path);
void sf_send(StockfishProc *sf, const char *cmd);
int  sf_read_until(StockfishProc *sf, const char *prefix, char *buf, size_t len);
void sf_kill(StockfishProc *sf);

typedef struct {
    int  cp;
    int  is_mate;
    int  mate_in;
    char best_move[SF_MOVE_LEN];
    int  depth;
} EvalResult;

int sf_analyze_fen(StockfishProc *sf, const char *fen, int depth, int multipv, EvalResult out[]);

#endif
