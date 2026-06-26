#include "worker.h"

#include <errno.h>
#include <fcntl.h>
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

    redisReply *r = redis_conn_cmd(rc, "HSET", key,
                                   "status", status,
                                   "movesAnalyzed", "%d", moves_analyzed,
                                   "movesTotal", "%d", moves_total);
    if (!r) return -1;
    free_reply(r);

    if (error_msg) {
        redisReply *r2 = redis_conn_cmd(rc, "HSET", key, "errorMessage", error_msg);
        if (r2) free_reply(r2);
    }

    int ttl = (strcmp(status, "running") == 0) ? 3600 : 300;
    redisReply *r3 = redis_conn_cmd(rc, "EXPIRE", key, "%d", ttl);
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
    ssize_t n;
    while ((n = read(fdout[0], line, sizeof(line) - 1)) > 0 && count < MAX_FENS) {
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
    return (count > 0) ? 0 : -1;
}

/* ── Subprocess: FEN list → analysis JSON via analyze-game ─────────────── */

static int run_analyze_game(const char *fens_data, char *json_buf, size_t buf_size)
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
        execlp("src/analyze-service/bin/analyze-game", "analyze-game", (char *)NULL);
        _exit(127);
    }

    close(fdin[0]); close(fdout[1]);

    size_t len = strlen(fens_data);
    size_t written = 0;
    while (written < len) {
        ssize_t w = write(fdin[1], fens_data + written, len - written);
        if (w < 0) {
            close(fdin[1]); close(fdout[0]);
            kill(pid, SIGKILL); waitpid(pid, NULL, 0);
            return -1;
        }
        written += (size_t)w;
    }
    close(fdin[1]);

    size_t pos = 0;
    ssize_t nr;
    while ((nr = read(fdout[0], json_buf + pos, buf_size - pos - 1)) > 0) {
        pos += (size_t)nr;
        if ((size_t)pos >= buf_size - 1) break;
    }
    json_buf[pos] = '\0';
    close(fdout[0]);

    int status;
    waitpid(pid, &status, 0);
    return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : -1;
}

/* ── Parse job fields from redis message ─────────────────────────────────── */

static void parse_job_message(redisReply *msg,
                              char *game_id, size_t gid_size,
                              char *pgn, size_t pgn_size)
{
    /* msg is [entry_id, nfields, field1, value1, ...] */
    for (size_t fi = 1; fi + 1 < msg->elements; fi += 2) {
        redisReply *fld = REPLY_ELEM(msg, (int)fi);
        redisReply *val = REPLY_ELEM(msg, (int)fi + 1);
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
    if (run_analyze_game(fens_buf, json_buf, sizeof(json_buf)) < 0) {
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
        "XAUTOCLAIM", STREAM_KEY, CONSUMER_GROUP, cname,
        "600000", "0-0", "COUNT", "10");
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
        "XGROUP", "CREATE", STREAM_KEY, CONSUMER_GROUP, "$", "MKSTREAM");
    if (r) free_reply(r);

    reclaim_stale(ctx);

    while (1) {
        redisReply *reply = redis_conn_cmd(ctx->redis,
            "XREADGROUP", "GROUP", CONSUMER_GROUP, cname,
            "COUNT", "1", "BLOCK", "5000",
            "STREAMS", STREAM_KEY, ">");

        if (!reply) {
            fprintf(stderr, "[worker-%d] XREADGROUP failed, retrying...\n",
                    ctx->worker_id);
            sleep(2);
            continue;
        }

        if (reply->type != REDIS_REPLY_ARRAY || reply->elements == 0) {
            free_reply(reply);
            continue;
        }

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

                if (game_id[0] && pgn[0]) {
                    process_job(ctx, entry_id, game_id, pgn);

                    redisReply *ack = redis_conn_cmd(ctx->redis,
                        "XACK", STREAM_KEY, CONSUMER_GROUP, entry_id);
                    if (ack) free_reply(ack);
                }
            }
        }
        free_reply(reply);
    }

    return NULL;
}