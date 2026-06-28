#include "uci.h"
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <sys/types.h>

int sf_spawn(StockfishProc *sf, const char *path) {
    int stdin_pipe[2];
    int stdout_pipe[2];

    if (pipe(stdin_pipe) < 0 || pipe(stdout_pipe) < 0) {
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        return -1;
    }

    if (pid == 0) {
        // Child
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        
        execlp(path, path, NULL);
        perror("execlp");
        exit(1);
    } else {
        // Parent
        close(stdin_pipe[0]);
        close(stdout_pipe[1]);
        
        sf->pid = pid;
        sf->stdin_fd = stdin_pipe[1];
        sf->stdout_fd = stdout_pipe[0];
        sf->out_stream = fdopen(sf->stdout_fd, "r");
        if (!sf->out_stream) return -1;
        
        // Wait for Stockfish to be ready
        char buf[256];
        sf_send(sf, "uci\n");
        if (sf_read_until(sf, "uciok", buf, sizeof(buf)) == 0) {
            sf_kill(sf);
            return -1;
        }
        sf_send(sf, "isready\n");
        if (sf_read_until(sf, "readyok", buf, sizeof(buf)) == 0) {
            sf_kill(sf);
            return -1;
        }
        
        return 0;
    }
}

void sf_send(StockfishProc *sf, const char *cmd) {
    if (write(sf->stdin_fd, cmd, strlen(cmd)) < 0) {
        perror("write");
    }
}

int sf_read_until(StockfishProc *sf, const char *prefix, char *buf, size_t len) {
    size_t prefix_len = strlen(prefix);
    while (fgets(buf, len, sf->out_stream)) {
        if (strncmp(buf, prefix, prefix_len) == 0) {
            return 1;
        }
    }
    return 0;
}

void sf_kill(StockfishProc *sf) {
    sf_send(sf, "quit\n");
    fclose(sf->out_stream); // also closes stdout_fd
    close(sf->stdin_fd);
    kill(sf->pid, SIGKILL);
}

int sf_analyze_fen(StockfishProc *sf, const char *fen, int depth, int multipv, EvalResult out[]) {
    char cmd[512];
    
    // reset out array depth
    for (int i=0; i<multipv; i++) {
        out[i].depth = 0;
        out[i].cp = 0;
        out[i].is_mate = 0;
        out[i].mate_in = 0;
        out[i].best_move[0] = '\0';
    }

    sf_send(sf, "ucinewgame\n");
    snprintf(cmd, sizeof(cmd), "position fen %s\n", fen);
    sf_send(sf, cmd);
    
    snprintf(cmd, sizeof(cmd), "go depth %d multipv %d\n", depth, multipv);
    sf_send(sf, cmd);

    // Parse info lines until bestmove
    char line[1024];
    while (fgets(line, sizeof(line), sf->out_stream)) {
        if (strncmp(line, "bestmove", 8) == 0) {
            // parse bestmove e2e4
            char bm[8];
            if (sscanf(line, "bestmove %7s", bm) == 1) {
                if (multipv > 0) {
                    strncpy(out[0].best_move, bm, sizeof(out[0].best_move) - 1);
                    out[0].best_move[sizeof(out[0].best_move) - 1] = '\0';
                }
            }
            break;
        }
        
        if (strncmp(line, "info", 4) == 0) {
            // Need to parse score cp/mate, multipv, depth
            int p_depth = 0;
            int p_multipv = 1;
            int p_cp = 0;
            int p_mate = 0;
            int is_mate = 0;
            
            char *p = strstr(line, " depth ");
            if (p) sscanf(p + 7, "%d", &p_depth);
            
            p = strstr(line, " multipv ");
            if (p) sscanf(p + 9, "%d", &p_multipv);
            
            p = strstr(line, " score cp ");
            if (p) {
                sscanf(p + 10, "%d", &p_cp);
            } else {
                p = strstr(line, " score mate ");
                if (p) {
                    is_mate = 1;
                    sscanf(p + 12, "%d", &p_mate);
                    if (p_mate > 0) p_cp = 10000;
                    else p_cp = -10000;
                }
            }

            if (p_multipv > 0 && p_multipv <= multipv && p_depth >= out[p_multipv-1].depth) {
                out[p_multipv-1].depth = p_depth;
                out[p_multipv-1].cp = p_cp;
                out[p_multipv-1].is_mate = is_mate;
                out[p_multipv-1].mate_in = p_mate;
                
                // also extract pv move for multipv entries
                char *pv = strstr(line, " pv ");
                if (pv) {
                    char bm[8];
                    if (sscanf(pv + 4, "%7s", bm) == 1) {
                        strncpy(out[p_multipv-1].best_move, bm, sizeof(out[p_multipv-1].best_move) - 1);
                        out[p_multipv-1].best_move[sizeof(out[p_multipv-1].best_move) - 1] = '\0';
                    }
                }
            }
        }
    }
    return 0;
}
