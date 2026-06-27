#include "worker.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_FENS 410
#define MAX_PGN  65536

/* hiredis element access — element is void* in some versions */
#define REPLY_ELEM(reply, i) (((redisReply**)(reply)->element)[i])

/* ── Progress hash helpers ──────────────────────────────────────────────── */

static int progress_set(RedisConn *rc, const char *game_id,
                        const char *status, int moves_analyzed, int moves_total,
                        const char *error_msg)
{
    char key[128];
    snprintf(key, sizeof(key), "job:%s:progress", game_id);

    redisReply *r = redis_conn_cmd(rc, "HSET %s status %s movesAnalyzed %d movesTotal %d",
                                   key, status, moves_analyzed, moves_total);
    if (!r) return -1;
    free_reply(r);

    if (error_msg) {
        redisReply *r2 = redis_conn_cmd(rc, "HSET %s errorMessage %s", key, error_msg);
        if (r2) free_reply(r2);
    }

    int ttl = (strcmp(status, "running") == 0) ? 3600 : 300;
    redisReply *r3 = redis_conn_cmd(rc, "EXPIRE %s %d", key, ttl);
    if (r3) free_reply(r3);

    return 0;
}

/* ── Subprocess: PGN → FENs via chess.js Node script ───────────────────── */

static int pgn_to_fens(const char *pgn, char *fens_buf, size_t buf_size, int *out_count)
{
    int fdin[2], fdout[2];
    if (pipe(fdin) < 0 || pipe(fdout) < 0) return -1;

    pid_t pid = fork();
    if (pid < 0) {
        close(fdin[0]); close(fdin[1]); close(fdout[0]); close(fdout[1]);
        return -1;
    }

    if (pid == 0) {
        close(fdin[1]); close(fdout[0]);
        dup2(fdin[0], STDIN_FILENO);
        dup2(fdout[1], STDOUT_FILENO);
        close(fdin[0]); close(fdout[1]);
        execlp("node", "node",
               "src/analyze-service/tools/pgn_to_fens.js", "/dev/stdin",
               (char *)NULL);
        _exit(127);
    }

    close(fdin[0]); close(fdout[1]);

    size_t pgn_len = strlen(pgn);
    size_t written = 0;
    while (written < pgn_len) {
        ssize_t w = write(fdin[1], pgn + written, pgn_len - written);
        if (w < 0) {
            if (errno == EINTR) continue;
            close(fdin[1]); close(fdout[0]);
            kill(pid, SIGKILL); waitpid(pid, NULL, 0);
            return -1;
        }
        written += (size_t)w;
    }
    close(fdin[1]);

    char line[256];
    int count = 0;
    size_t pos = 0;
    while (count < MAX_FENS) {
        ssize_t n = read(fdout[0], line, sizeof(line) - 1);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (n == 0) break;
        line[n] = '\0';
        char *p = line;
        char *eol;
        while ((eol = strchr(p, '\n')) && count < MAX_FENS) {
            *eol = '\0';
            size_t len = strlen(p);
            if (len > 0 && pos + len + 1 < buf_size) {
                memcpy(fens_buf + pos, p, len);
                fens_buf[pos + len] = '\n';
                pos += len + 1;
                count++;
            }
            p = eol + 1;
        }
    }
    close(fdout[0]);

    int status;
    waitpid(pid, &status, 0);
    if (out_count) *out_count = count;
    if (pos < buf_size) {
        fens_buf[pos] = '\0';
    } else {
        fens_buf[buf_size - 1] = '\0';
    }
    return (count > 0) ? 0 : -1;
}

/* ── Subprocess: FEN list → analysis JSON via analyze-game ─────────────── */

static int run_analyze_game(const char *fens_data, char *json_buf, size_t buf_size,
                            int timeout_secs)
{
    (void)fens_data;  /* stdin provides FENs */

    int fdin[2], fdout[2];
    if (pipe(fdin) < 0 || pipe(fdout) < 0) return -1;

    pid_t pid = fork();
    if (pid < 0) {
        close(fdin[0]); close(fdin[1]); close(fdout[0]); close(fdout[1]);
        return -1;
    }

    if (pid == 0) {
        close(fdin[1]); close(fdout[0]);
        dup2(fdin[0], STDIN_FILENO);
        dup2(fdout[1], STDOUT_FILENO);
        close(fdin[0]); close(fdout[1]);

        char exe_path[1024];
        ssize_t rlen = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
        if (rlen != -1) {
            exe_path[rlen] = '\0';
            char *last_slash = strrchr(exe_path, '/');
            if (last_slash) {
                *last_slash = '\0';
                strncat(exe_path, "/analyze-game", sizeof(exe_path) - strlen(exe_path) - 1);
                execl(exe_path, "analyze-game", (char *)NULL);
            }
        }
        // Fallback
        execlp("src/analyze-service/bin/analyze-game", "analyze-game", (char *)NULL);
        fprintf(stderr, "[worker] failed to execute analyze-game: %s\n", strerror(errno));
        _exit(127);
    }

    close(fdin[0]); close(fdout[1]);

    size_t len = strlen(fens_data);
    size_t written = 0;
    while (written < len) {
        ssize_t w = write(fdin[1], fens_data + written, len - written);
        if (w < 0) {
            if (errno == EINTR) continue;
            close(fdin[1]); close(fdout[0]);
            kill(pid, SIGKILL); waitpid(pid, NULL, 0);
            return -1;
        }
        written += (size_t)w;
    }
    close(fdin[1]);

    time_t start = time(NULL);
    size_t pos = 0;
    while (pos < buf_size - 1) {
        int remaining_ms = -1;
        if (timeout_secs > 0) {
            time_t elapsed = time(NULL) - start;
            if (elapsed >= (time_t)timeout_secs) {
                kill(pid, SIGKILL);
                waitpid(pid, NULL, 0);
                close(fdout[0]);
                return -2;
            }
            remaining_ms = (int)((time_t)timeout_secs - elapsed) * 1000;
            if (remaining_ms < 100) remaining_ms = 100;
        }

        struct pollfd pfd = { .fd = fdout[0], .events = POLLIN };
        int ready = poll(&pfd, 1, remaining_ms);
        if (ready < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ready == 0) continue;

        ssize_t nr = read(fdout[0], json_buf + pos, buf_size - pos - 1);
        if (nr < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (nr == 0) break;
        pos += (size_t)nr;
    }
    json_buf[pos] = '\0';
    close(fdout[0]);

    int status;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        if (code != 0) {
            fprintf(stderr, "[worker] analyze-game exited with code %d\n", code);
            return -1;
        }
    } else if (WIFSIGNALED(status)) {
        fprintf(stderr, "[worker] analyze-game killed by signal %d\n", WTERMSIG(status));
        return -1;
    } else {
        fprintf(stderr, "[worker] analyze-game failed in unknown way\n");
        return -1;
    }
    return 0;
}

/* ── Parse job fields from redis message ─────────────────────────────────── */

static void parse_job_message(redisReply *msg,
                              char *game_id, size_t gid_size,
                              char *pgn, size_t pgn_size)
{
    if (msg->type != REDIS_REPLY_ARRAY || msg->elements < 2) return;
    redisReply *fields = REPLY_ELEM(msg, 1);
    if (fields->type != REDIS_REPLY_ARRAY) return;

    for (size_t fi = 0; fi + 1 < fields->elements; fi += 2) {
        redisReply *fld = REPLY_ELEM(fields, (int)fi);
        redisReply *val = REPLY_ELEM(fields, (int)fi + 1);
        if (fld->type != REDIS_REPLY_STRING || val->type != REDIS_REPLY_STRING)
            continue;
        if (strcmp(fld->str, "gameId") == 0)
            snprintf(game_id, gid_size, "%s", val->str);
        else if (strcmp(fld->str, "pgn") == 0)
            snprintf(pgn, pgn_size, "%s", val->str);
    }
}

/* ── Process one job ──────────────────────────────────────────────────────── */

static int process_job(WorkerCtx *ctx, const char *entry_id,
                        const char *game_id, const char *pgn)
{
    (void)entry_id;
    fprintf(stderr, "[worker-%d] processing game %s\n",
            ctx->worker_id, game_id);

    mongo_mark_running(ctx->mongo, game_id);

    char fens_buf[128 * 256];
    int fen_count = 0;
    if (pgn_to_fens(pgn, fens_buf, sizeof(fens_buf), &fen_count) < 0) {
        fprintf(stderr, "[worker-%d] pgn_to_fens failed for %s\n",
                ctx->worker_id, game_id);
        progress_set(ctx->redis, game_id, "failed", 0, 0, "PGN parse failed");
        mongo_update_analysis(ctx->mongo, game_id, "[]", 0, "failed", "PGN parse failed");
        return 0;
    }

    progress_set(ctx->redis, game_id, "running", 0, fen_count - 1, NULL);

    char json_buf[512 * 1024];
    int rc = run_analyze_game(fens_buf, json_buf, sizeof(json_buf),
                              ctx->job_timeout_secs);
    if (rc == -2) {
        fprintf(stderr, "[worker-%d] job timed out for %s\n",
                ctx->worker_id, game_id);
        progress_set(ctx->redis, game_id, "failed", 0, fen_count - 1,
                     "job timed out after 5 minutes");
        mongo_update_analysis(ctx->mongo, game_id, "[]", fen_count - 1,
                              "failed", "job timed out after 5 minutes");
        return 0;
    }
    if (rc < 0) {
        fprintf(stderr, "[worker-%d] analyze-game failed for %s\n",
                ctx->worker_id, game_id);
        progress_set(ctx->redis, game_id, "failed", 0, fen_count - 1, "engine error");
        mongo_update_analysis(ctx->mongo, game_id, "[]", fen_count - 1, "failed", "engine error");
        return 0;
    }

    int num_moves = fen_count - 1;
    progress_set(ctx->redis, game_id, "completed", num_moves, num_moves, NULL);

    if (mongo_update_analysis(ctx->mongo, game_id, json_buf, num_moves,
                               "completed", NULL) < 0) {
        fprintf(stderr, "[worker-%d] mongo_update_analysis failed for %s\n",
                ctx->worker_id, game_id);
    }

    fprintf(stderr, "[worker-%d] completed game %s (%d moves)\n",
            ctx->worker_id, game_id, num_moves);
    return 0;
}

/* ── XAUTOCLAIM: reclaim stale entries on startup ───────────────────────── */

static void reclaim_stale(WorkerCtx *ctx)
{
    char cname[64];
    snprintf(cname, sizeof(cname), "worker-%d-%d", (int)getpid(), ctx->worker_id);

    redisReply *r = redis_conn_cmd(ctx->redis,
        "XAUTOCLAIM %s %s %s 600000 0-0 COUNT 10",
        STREAM_KEY, CONSUMER_GROUP, cname);
    if (!r || r->type != REDIS_REPLY_ARRAY || r->elements < 2) {
        if (r) free_reply(r);
        return;
    }

    redisReply *messages = REPLY_ELEM(r, 1);
    if (!messages || messages->type != REDIS_REPLY_ARRAY) {
        free_reply(r);
        return;
    }

    for (size_t mi = 0; mi < messages->elements; mi++) {
        redisReply *msg = REPLY_ELEM(messages, (int)mi);
        if (!msg || msg->type != REDIS_REPLY_ARRAY || msg->elements < 2)
            continue;

        char game_id[64] = {0};
        char pgn[MAX_PGN] = {0};
        parse_job_message(msg, game_id, sizeof(game_id), pgn, sizeof(pgn));

        if (game_id[0] && pgn[0]) {
            const char *eid = REPLY_ELEM(msg, 0)->str;
            process_job(ctx, eid, game_id, pgn);
        }
    }
    free_reply(r);
}

/* ── Worker thread ──────────────────────────────────────────────────────── */

void *worker_thread(void *arg)
{
    WorkerCtx *ctx = (WorkerCtx *)arg;

    char cname[64];
    snprintf(cname, sizeof(cname), "worker-%d-%d", (int)getpid(), ctx->worker_id);

    /* Ensure consumer group exists (idempotent) */
    redisReply *r = redis_conn_cmd(ctx->redis,
        "XGROUP CREATE %s %s $ MKSTREAM", STREAM_KEY, CONSUMER_GROUP);
    if (r) {
        fprintf(stderr, "[worker-%d] XGROUP CREATE reply type=%d str=%s\n",
                ctx->worker_id, r->type, r->type == REDIS_REPLY_STATUS || r->type == REDIS_REPLY_ERROR ? r->str : "(non-string)");
        free_reply(r);
    } else {
        fprintf(stderr, "[worker-%d] XGROUP CREATE failed (null reply)\n", ctx->worker_id);
    }

    reclaim_stale(ctx);

    while (1) {
        redisReply *reply = redis_conn_cmd(ctx->redis,
            "XREADGROUP GROUP %s %s COUNT 1 BLOCK 5000 STREAMS %s >",
            CONSUMER_GROUP, cname, STREAM_KEY);

        if (!reply) {
            fprintf(stderr, "[worker-%d] XREADGROUP failed (null reply), retrying...\n",
                    ctx->worker_id);
            sleep(2);
            continue;
        }

        if (reply->type != REDIS_REPLY_ARRAY) {
            fprintf(stderr, "[worker-%d] XREADGROUP returned non-array type=%d str=%s\n",
                    ctx->worker_id, reply->type, reply->type == REDIS_REPLY_ERROR ? reply->str : "");
            free_reply(reply);
            sleep(2);
            continue;
        }

        if (reply->elements == 0) {
            free_reply(reply);
            continue;
        }

        fprintf(stderr, "[worker-%d] XREADGROUP active: elements=%zu\n", ctx->worker_id, (size_t)reply->elements);

        /* reply = [[stream_key, [messages...]], ...] */
        for (size_t si = 0; si < reply->elements; si++) {
            redisReply *stream = REPLY_ELEM(reply, (int)si);
            if (stream->type != REDIS_REPLY_ARRAY || stream->elements < 2)
                continue;

            redisReply *messages = REPLY_ELEM(stream, 1);
            if (messages->type != REDIS_REPLY_ARRAY)
                continue;

            for (size_t mi = 0; mi < messages->elements; mi++) {
                redisReply *msg = REPLY_ELEM(messages, (int)mi);
                if (!msg || msg->type != REDIS_REPLY_ARRAY || msg->elements < 2)
                    continue;

                const char *entry_id = REPLY_ELEM(msg, 0)->str;

                char game_id[64] = {0};
                char pgn[MAX_PGN] = {0};
                parse_job_message(msg, game_id, sizeof(game_id), pgn, sizeof(pgn));

                fprintf(stderr, "[worker-%d] XREADGROUP message: entry_id=%s game_id=%s pgn_len=%zu\n",
                        ctx->worker_id, entry_id, game_id, strlen(pgn));

                if (game_id[0] && pgn[0]) {
                    process_job(ctx, entry_id, game_id, pgn);

                    redisReply *ack = redis_conn_cmd(ctx->redis,
                        "XACK %s %s %s", STREAM_KEY, CONSUMER_GROUP, entry_id);
                    if (ack) free_reply(ack);
                }
            }
        }
        free_reply(reply);
    }

    return NULL;
}