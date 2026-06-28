#include "uci.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: analyze-single <fen> [depth]\n");
        return 1;
    }
    
    const char *fen = argv[1];
    int depth = 18;
    if (argc >= 3) {
        depth = atoi(argv[2]);
    }

    const char *sf_path = getenv("STOCKFISH_PATH");
    if (!sf_path) {
        sf_path = "stockfish";
    }

    StockfishProc sf;
    if (sf_spawn(&sf, sf_path) != 0) {
        fprintf(stderr, "Failed to spawn stockfish\n");
        return 1;
    }

    EvalResult out[2] = {0};
    sf_analyze_fen(&sf, fen, depth, 2, out);
    
    printf("cp=%d best=%s\n", out[0].cp, out[0].best_move);
    
    sf_kill(&sf);
    return 0;
}
