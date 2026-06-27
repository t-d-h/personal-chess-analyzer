#ifndef UCI_H
#define UCI_H

#include <stdio.h>
#include <stddef.h>

#define SF_MOVE_LEN 8
#define SF_LINE_LEN 4096

#define DEFAULT_MOVE_TIMEOUT_MS 5000
#define SF_READ_BUF_LEN 8192

typedef struct {
    int  pid;
    int  stdin_fd;
    int  stdout_fd;
    FILE *out_stream;
    int  move_timeout_ms;
    char read_buf[SF_READ_BUF_LEN];
    int  read_buf_pos;
    int  read_buf_len;
} StockfishProc;

int  sf_spawn(StockfishProc *sf, const char *path);
void sf_send(StockfishProc *sf, const char *cmd);
int  sf_read_until(StockfishProc *sf, const char *prefix, char *buf, size_t len);
int  sf_read_line_timeout(StockfishProc *sf, char *buf, size_t len, int timeout_ms);
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
