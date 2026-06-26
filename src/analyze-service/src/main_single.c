#include "uci.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: analyze-single <fen> [depth]\n");
        return 1;
    }

    const char *fen = argv[1];
    int depth = 18;
    if (argc >= 3) depth = atoi(argv[2]);
    if (depth < 1 || depth > 30) depth = 18;

    StockfishProc sf;
    memset(&sf, 0, sizeof(sf));

    const char *sf_path = getenv("STOCKFISH_PATH");
    if (sf_spawn(&sf, sf_path) < 0) {
        fprintf(stderr, "Error: failed to spawn stockfish\n");
        return 1;
    }

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

    EvalResult results[2];
    if (sf_analyze_fen(&sf, fen, depth, 2, results) < 0) {
        fprintf(stderr, "Error: analysis failed\n");
        sf_kill(&sf);
        return 1;
    }

    sf_send(&sf, "quit");
    sf_kill(&sf);

    printf("cp=%d best=%s\n", results[0].cp, results[0].best_move);
    return 0;
}
