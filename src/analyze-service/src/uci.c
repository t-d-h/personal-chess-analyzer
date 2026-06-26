#include "uci.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static const char *DEFAULT_STOCKFISH = "stockfish";

int sf_spawn(StockfishProc *sf, const char *path)
{
    if (!path) path = DEFAULT_STOCKFISH;

    int stdin_pipe[2];
    int stdout_pipe[2];

    if (pipe(stdin_pipe) < 0 || pipe(stdout_pipe) < 0) {
        perror("pipe");
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }

    if (pid == 0) {
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        close(stdin_pipe[0]);
        close(stdout_pipe[1]);
        execlp(path, path, (char *)NULL);
        _exit(127);
    }

    close(stdin_pipe[0]);
    close(stdout_pipe[1]);

    sf->pid = pid;
    sf->stdin_fd = stdin_pipe[1];
    sf->stdout_fd = stdout_pipe[0];
    sf->out_stream = fdopen(stdout_pipe[0], "r");
    if (!sf->out_stream) {
        perror("fdopen");
        close(stdout_pipe[0]);
        close(stdin_pipe[1]);
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        return -1;
    }

    return 0;
}

void sf_send(StockfishProc *sf, const char *cmd)
{
    size_t len = strlen(cmd);
    ssize_t r;
    r = write(sf->stdin_fd, cmd, len);
    r = write(sf->stdin_fd, "\n", 1);
    (void)r;
}

static char *fgets_retry(char *str, int n, FILE *stream)
{
    char *res;
    while (1) {
        res = fgets(str, n, stream);
        if (res == NULL) {
            if (ferror(stream) && errno == EINTR) {
                clearerr(stream);
                continue;
            }
        }
        break;
    }
    return res;
}

int sf_read_until(StockfishProc *sf, const char *prefix, char *buf, size_t len)
{
    size_t prefix_len = strlen(prefix);
    while (fgets_retry(buf, (int)len, sf->out_stream)) {
        if (strncmp(buf, prefix, prefix_len) == 0) {
            size_t blen = strlen(buf);
            if (blen > 0 && buf[blen - 1] == '\n') buf[blen - 1] = '\0';
            return 0;
        }
    }
    return -1;
}

void sf_kill(StockfishProc *sf)
{
    if (sf->pid > 0) {
        kill(sf->pid, SIGTERM);
        waitpid(sf->pid, NULL, 0);
        sf->pid = 0;
    }
    if (sf->out_stream) {
        fclose(sf->out_stream);
        sf->out_stream = NULL;
    }
    if (sf->stdin_fd > 0) {
        close(sf->stdin_fd);
        sf->stdin_fd = 0;
    }
    sf->stdout_fd = 0;
}

static int parse_score(const char *info_line, EvalResult *result)
{
    const char *p = strstr(info_line, " score ");
    if (!p) return -1;
    p += 7;

    if (strncmp(p, "cp ", 3) == 0) {
        p += 3;
        result->cp = atoi(p);
        result->is_mate = 0;
        result->mate_in = 0;
    } else if (strncmp(p, "mate ", 5) == 0) {
        p += 5;
        result->mate_in = atoi(p);
        result->is_mate = 1;
        result->cp = result->mate_in > 0 ? 10000 : -10000;
    } else {
        return -1;
    }

    return 0;
}

static int parse_depth(const char *info_line)
{
    const char *p = strstr(info_line, " depth ");
    if (!p) return 0;
    p += 7;
    return atoi(p);
}

static int parse_multipv(const char *info_line)
{
    const char *p = strstr(info_line, " multipv ");
    if (!p) return 1;
    p += 9;
    return atoi(p);
}

static int parse_bestmove(const char *bestmove_line, EvalResult *result)
{
    const char *p = bestmove_line + 9;
    while (*p == ' ') p++;
    size_t i = 0;
    while (*p && *p != ' ' && *p != '\n' && i < SF_MOVE_LEN - 1) {
        result->best_move[i++] = *p++;
    }
    result->best_move[i] = '\0';
    return 0;
}

static int parse_pv_move(const char *info_line, EvalResult *result)
{
    const char *pv = strstr(info_line, " pv ");
    if (!pv) return -1;
    pv += 4;
    while (*pv == ' ') pv++;
    size_t i = 0;
    while (*pv && *pv != ' ' && *pv != '\n' && i < SF_MOVE_LEN - 1) {
        result->best_move[i++] = *pv++;
    }
    result->best_move[i] = '\0';
    return 0;
}

int sf_analyze_fen(StockfishProc *sf, const char *fen, int depth, int multipv, EvalResult out[])
{
    char cmd[4096];
    char line[SF_LINE_LEN];

    for (int i = 0; i < multipv; i++) {
        memset(&out[i], 0, sizeof(EvalResult));
        out[i].cp = 0;
        out[i].is_mate = 0;
        out[i].mate_in = 0;
        out[i].best_move[0] = '\0';
        out[i].depth = 0;
    }

    sf_send(sf, "ucinewgame");
    snprintf(cmd, sizeof(cmd), "position fen %s", fen);
    sf_send(sf, cmd);
    snprintf(cmd, sizeof(cmd), "go depth %d multipv %d", depth, multipv);
    sf_send(sf, cmd);

    int done = 0;
    while (fgets_retry(line, sizeof(line), sf->out_stream)) {
        if (strncmp(line, "bestmove", 8) == 0) {
            if (out[0].best_move[0] == '\0') {
                parse_bestmove(line, &out[0]);
            }
            done = 1;
            break;
        }

        if (strncmp(line, "info ", 5) != 0) continue;

        int pv_index = parse_multipv(line) - 1;
        if (pv_index < 0 || pv_index >= multipv) continue;

        int d = parse_depth(line);
        if (d == 0) continue;

        parse_score(line, &out[pv_index]);
        out[pv_index].depth = d;
        parse_pv_move(line, &out[pv_index]);
    }

    if (!done) return -1;
    return 0;
}
