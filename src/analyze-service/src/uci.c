#include "uci.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static const char *DEFAULT_STOCKFISH = "stockfish";

static int sf_read_line_raw(StockfishProc *sf, char *buf, size_t len, int timeout_ms)
{
    size_t pos = 0;
    int got_any = 0;

    while (pos < len - 1) {
        if (sf->read_buf_pos >= sf->read_buf_len) {
            struct pollfd pfd = { .fd = sf->stdout_fd, .events = POLLIN };
            int remaining = got_any ? 5000 : timeout_ms;
            int ready = poll(&pfd, 1, remaining);
            if (ready <= 0) {
                if (got_any) break;
                return -1;
            }
            ssize_t nr = read(sf->stdout_fd, sf->read_buf, SF_READ_BUF_LEN);
            if (nr <= 0) {
                if (nr < 0 && errno == EINTR) continue;
                break;
            }
            sf->read_buf_len = (int)nr;
            sf->read_buf_pos = 0;
        }

        char ch = sf->read_buf[sf->read_buf_pos++];
        got_any = 1;
        if (ch == '\n') {
            break;
        }
        buf[pos++] = ch;
    }
    buf[pos] = '\0';
    return got_any ? 0 : -1;
}

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
        setpgid(0, 0);
        execlp(path, path, (char *)NULL);
        _exit(127);
    }

    setpgid(pid, pid);

    close(stdin_pipe[0]);
    close(stdout_pipe[1]);

    sf->pid = pid;
    sf->stdin_fd = stdin_pipe[1];
    sf->stdout_fd = stdout_pipe[0];
    sf->move_timeout_ms = DEFAULT_MOVE_TIMEOUT_MS;
    sf->read_buf_pos = 0;
    sf->read_buf_len = 0;
    sf->out_stream = NULL;

    return 0;
}

void sf_send(StockfishProc *sf, const char *cmd)
{
    size_t len = strlen(cmd);
    while (len > 0) {
        ssize_t w = write(sf->stdin_fd, cmd, len);
        if (w < 0) {
            if (errno == EINTR) continue;
            break;
        }
        cmd += w;
        len -= (size_t)w;
    }
    const char *nl = "\n";
    size_t nl_len = 1;
    while (nl_len > 0) {
        ssize_t w = write(sf->stdin_fd, nl, nl_len);
        if (w < 0) {
            if (errno == EINTR) continue;
            break;
        }
        nl += w;
        nl_len -= (size_t)w;
    }
}

int sf_read_until(StockfishProc *sf, const char *prefix, char *buf, size_t len)
{
    size_t prefix_len = strlen(prefix);
    while (sf_read_line_raw(sf, buf, len, 5000) == 0) {
        if (strncmp(buf, prefix, prefix_len) == 0) {
            return 0;
        }
    }
    return -1;
}

int sf_read_line_timeout(StockfishProc *sf, char *buf, size_t len, int timeout_ms)
{
    return sf_read_line_raw(sf, buf, len, timeout_ms);
}

void sf_kill(StockfishProc *sf)
{
    if (sf->pid > 0) {
        kill(-sf->pid, SIGKILL);
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
    if (sf->stdout_fd > 0) {
        close(sf->stdout_fd);
        sf->stdout_fd = 0;
    }
    sf->read_buf_pos = 0;
    sf->read_buf_len = 0;
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
    int timeout_ms = sf->move_timeout_ms > 0 ? sf->move_timeout_ms : DEFAULT_MOVE_TIMEOUT_MS;

    while (1) {
        if (sf_read_line_timeout(sf, line, sizeof(line), timeout_ms) < 0) {
            return -1;
        }

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
