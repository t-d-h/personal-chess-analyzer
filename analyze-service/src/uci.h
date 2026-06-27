#ifndef UCI_H
#define UCI_H

#include <stdio.h>

typedef struct {
    int  pid;
    int  stdin_fd;
    int  stdout_fd;
    FILE *out_stream;  // fdopen on stdout_fd for fgets
} StockfishProc;

int  sf_spawn(StockfishProc *sf, const char *path);
void sf_send(StockfishProc *sf, const char *cmd);
// Read lines until a line starting with `prefix` is found; fill buf
int  sf_read_until(StockfishProc *sf, const char *prefix, char *buf, size_t len);
void sf_kill(StockfishProc *sf);

typedef struct {
    int   cp;          // centipawns (capped mate)
    int   is_mate;
    int   mate_in;
    char  best_move[8]; // UCI move string e.g. "e2e4"
    int   depth;
} EvalResult;

int sf_analyze_fen(StockfishProc *sf, const char *fen, int depth, int multipv, EvalResult out[]);

#endif
