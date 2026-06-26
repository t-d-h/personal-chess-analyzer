# Chess Game Analyzer — Architecture

## 1. Overview

A web service that takes a chess.com game (pasted PGN or a chess.com game URL),
runs it through Stockfish, and returns a move-by-move analysis: accuracy %,
move classifications (best / good / inaccuracy / mistake / blunder /
brilliant), an evaluation graph, and basic game metadata (players, ratings,
time control, result).

**Goals**
- Accept PGN text or a `chess.com/game/...` URL as input.
- Analyze every move with Stockfish and compute per-player accuracy.
- Classify each move using centipawn-loss buckets.
- Cache results so the same game is never re-analyzed from scratch.
- Keep the HTTP layer responsive — engine analysis runs asynchronously, in a
  performance-optimized native service.

**Non-goals (v1)**
- Live/ongoing game analysis (this is for completed games only).
- Opening book / theory database beyond a simple "skip first N plies" rule.
- User accounts, auth, or multi-tenant rate limiting beyond basic IP throttling.

## 2. High-level flow

```
Browser              API gateway              Chess.com API (external)
   |  POST /api/games        |
   | (pgn or url) ----------->|
   |                          |--- if url: fetch PGN ----------------->|
   |                          |<------------------- PGN ----------------|
   |                          |
   |                          |--- XADD job -----------------> Redis stream
   |<-- { gameId, jobId } ----|
   |                                                |
   |  poll GET /api/jobs/:id                        v
   |<---------- progress (read from Redis) ---- Analyze service (C)
   |                                                |  spawns Stockfish per move,
   |                                                |  computes accuracy/classification
   |                                                v
   |  GET /api/games/:id/analysis              MongoDB (final result)
   |<------------------------------------------------|
```

The API gateway never blocks on engine analysis. It parses/validates the
game, enqueues a job on Redis, and returns immediately. The CPU-heavy work —
talking to Stockfish and crunching the accuracy math — happens in a separate,
horizontally-scalable native service written in C. The frontend polls (or
subscribes via WebSocket) for progress until the job completes.

## 3. Components

### 3.1 Frontend

- Chessboard + move list (`react-chessboard` or `chessboard.js` + `chess.js`
  for move/SAN handling on the client).
- Eval graph across the game (line chart, x = ply, y = win% or centipawns).
- Input box that accepts either pasted PGN or a chess.com game URL — detect
  which one it is client-side (regex on `chess.com/game/` or `/live/` /
  `/daily/`) before sending to the backend, but the backend re-validates
  regardless.
- Polls `GET /api/jobs/:jobId` every 1–2s, or opens a WebSocket if available,
  to show progress ("analyzing move 14 of 38").

### 3.2 API gateway

Thin, I/O-bound layer responsible for:
- Validating input (valid PGN, or a resolvable chess.com URL).
- Parsing PGN headers (players, ratings, time control, result, ECO code).
- Deduplication: hash the PGN (or use the chess.com game ID) and check
  MongoDB for an existing completed analysis before creating a new job.
- Publishing analysis jobs onto the Redis stream and exposing job status.
- Serving the finished analysis out of MongoDB once the analyze service has
  written it.

This service does no engine work itself — it's a good fit for a higher-level
language (Node.js/TypeScript, Go, etc.) since its job is HTTP, parsing, and
talking to chess.com, not raw compute.

### 3.3 Chess.com integration

If the input is a URL rather than raw PGN, fetch the game from chess.com's
public API rather than asking the user to copy-paste:
- Archived (completed) games: `GET https://api.chess.com/pub/player/{username}/games/{YYYY}/{MM}`
  — find the matching game by URL/UUID, or
- Resolve the game UUID directly if the URL format exposes one.

Always re-parse the returned PGN yourself — don't trust chess.com's own
accuracy numbers if present; this service computes its own.

### 3.4 Queue — Redis

- Redis Streams (`XADD` / `XREADGROUP`) rather than a plain list, so multiple
  analyze-service replicas can form a consumer group: each job is delivered
  to exactly one worker, and an unacknowledged job (worker crashed mid-job)
  can be reclaimed (`XCLAIM`) and retried.
- Job payload: `{ gameId, pgn }`.
- The analyze service writes live progress into a Redis hash
  (`job:{gameId}:progress` → `{ status, movesAnalyzed, movesTotal }`) as it
  works through the game. The API gateway reads this hash directly to answer
  `GET /api/jobs/:jobId` — cheap, and avoids hitting MongoDB on every poll.
- On completion, the analyze service writes the full result document to
  MongoDB once and sets `status = completed` in the Redis hash (with a short
  TTL so finished/failed job keys don't accumulate forever).
- Set a max job runtime (e.g. 5 minutes); the analyze service marks the job
  failed past that rather than holding a stream entry unacknowledged forever.

### 3.5 Analyze service (C)

This is the performance-critical piece, and is implemented in **C** rather
than a higher-level language, since it's the part doing real work at scale:
spawning/managing Stockfish subprocesses and running the accuracy math over
every position of every game.

- A pool of OS processes or threads (configurable, sized to available CPU
  cores minus headroom for the host). Each worker:
  1. Blocks on `XREADGROUP` against the Redis stream (via `hiredis`) waiting
     for the next job.
  2. `fork()`/`exec()`s a single Stockfish binary for the whole game (not one
     process per move — process startup cost adds up fast over 40+ moves),
     and talks to it over UCI through pipes (`stdin`/`stdout`).
  3. Sends `ucinewgame`, then for each ply: sets the position, runs `go depth N`
     (or `go movetime N`) with `multipv 2`, parses the `info`/`bestmove`
     output.
  4. Runs the win%/accuracy/classification math (§3.6) directly in C —
     `expf`/`powf` from `math.h`, no allocation in the hot loop.
  5. Updates the Redis progress hash after each move.
  6. On the last move, builds the full result document and writes it to
     MongoDB via `mongo-c-driver` (libbson/libmongoc), then acks the stream
     entry (`XACK`).
- Per-move timeout: if Stockfish doesn't respond in time, kill and restart
  the subprocess, record that move's eval as unavailable, and continue
  rather than failing the whole job.
- Stateless beyond the in-flight job — safe to run N replicas behind the same
  Redis consumer group for horizontal scaling.

### 3.6 Accuracy & move classification

**Step 1 — win% conversion.** Centipawn evaluations are misleading on their
own (losing 100cp in an equal position is a disaster; losing 100cp in a
totally winning position is irrelevant). Convert to win probability first,
using the same approach Lichess publishes:

```
winPercent(cp) = 50 + 50 * (2 / (1 + exp(-0.00368208 * cp)) - 1)
```

- `cp` is from the perspective of the player who is about to move.
- Cap mate scores before converting (e.g. mate-in-N → ±10000cp) so the curve
  math doesn't break on forced mates.

**Step 2 — per-move win% loss.**

```
winPercentLoss = max(0, winPercentBefore - winPercentAfter)
```

`winPercentBefore` = win% of the best available move.
`winPercentAfter` = win% of the move actually played.

**Step 3 — per-move accuracy score.**

```
moveAccuracy = clamp(103.1668 * exp(-0.04354 * winPercentLoss) - 3.1669, 0, 100)
```

This is the commonly-used decay curve that maps win%-loss onto a 0–100 score
that matches the shape of chess.com's own published accuracy ranges. Note:
chess.com has not published its exact formula — this is the standard
open-source approximation, accurate enough for this purpose.

**Step 4 — game accuracy per player.**

Average `moveAccuracy` across each player's own moves (don't mix White's and
Black's moves into one average).

**Move classification** (bucketed by centipawn loss, not win%-loss — this
matches how chess.com describes its own categories):

| Label        | Centipawn loss        |
|--------------|------------------------|
| Best         | 0                       |
| Excellent    | < 50                    |
| Good         | < 100                   |
| Inaccuracy   | 100–300                 |
| Mistake      | 300–500                 |
| Blunder      | > 500                   |
| Brilliant    | special case — see below |
| Book         | special case — see below |

**Brilliant move detection**: requires `multipv ≥ 2`. A move qualifies as
"brilliant" only if (a) it loses material relative to a safer alternative
(i.e. it's a sacrifice), and (b) it is still the engine's #1 choice. A
sacrifice that merely "doesn't lose" is not brilliant — it has to be best.

**Book move handling**: don't run classification on the first N plies (a
configurable constant, default 8–10) if the position matches a known opening
line — penalizing a player for not finding the engine's "improvement" over
established theory produces noise, not signal. A simple ECO/opening-name
lookup from the PGN header is sufficient for v1; a full opening book is a
later enhancement.

### 3.7 Caching

- Key cache entries on a hash of the normalized PGN (strip comments/clock
  annotations before hashing) or the chess.com game ID when available, stored
  as a unique-indexed field on the MongoDB document.
- If a cache hit exists (a completed document with that hash already in
  MongoDB), return it immediately — don't publish a new job.

## 4. Data model (MongoDB)

MongoDB document-per-game is a natural fit here: a game has a bounded number
of moves (well under MongoDB's 16MB document limit even for very long games),
so moves are embedded as an array inside the game document rather than split
into a separate collection — one read gets you the whole analysis.

**Collection: `games`**

```js
{
  _id: ObjectId,
  source: "pgn_paste" | "chesscom_url",
  chesscomGameId: String | null,
  pgn: String,
  pgnHash: String,            // unique index — dedup / cache key
  white: { username: String, rating: Number | null },
  black: { username: String, rating: Number | null },
  timeControl: String | null,
  result: String,              // "1-0" | "0-1" | "1/2-1/2"
  ecoCode: String | null,
  playedAt: Date | null,
  createdAt: Date,

  analysis: {
    status: "queued" | "running" | "completed" | "failed",
    movesAnalyzed: Number,
    movesTotal: Number,
    errorMessage: String | null,
    updatedAt: Date,
    completedAt: Date | null,

    moves: [
      {
        ply: Number,
        color: "white" | "black",
        san: String,
        fenBefore: String,
        fenAfter: String,
        evalCpPlayed: Number | null,
        evalCpBest: Number | null,
        evalMate: Number | null,
        bestMoveSan: String | null,
        winPercentLoss: Number | null,
        moveAccuracy: Number | null,
        classification: "best" | "excellent" | "good" | "inaccuracy" |
                         "mistake" | "blunder" | "brilliant" | "book" | null,
        engineDepth: Number | null
      }
      // ...
    ],

    playerSummaries: [
      {
        color: "white" | "black",
        accuracyPct: Number,
        acpl: Number,
        bestCount: Number,
        excellentCount: Number,
        goodCount: Number,
        inaccuracyCount: Number,
        mistakeCount: Number,
        blunderCount: Number,
        brilliantCount: Number
      }
      // one per color
    ]
  }
}
```

**Indexes**
- `pgnHash`: unique — dedup/cache lookups.
- `chesscomGameId`: sparse — fast lookup when re-submitting the same URL.
- `analysis.status`: supports admin/debug queries for stuck jobs.

Job *progress* lives in Redis (fast, ephemeral, polled frequently); only the
*final* result is persisted to MongoDB, written once by the analyze service
when a job completes. This keeps write volume to MongoDB low even though
progress updates happen after every move.

## 5. API contract

```
POST /api/games
  body: { pgn: string } | { url: string }
  -> 201 { gameId, jobId, status: "queued" | "completed" (on cache hit) }

GET /api/games/:gameId
  -> 200 { gameId, white, black, ratings, timeControl, result, ecoCode, status }

GET /api/games/:gameId/analysis
  -> 200 { moves: [...], playerSummaries: [...] }   (only once status = completed)
  -> 409 if job is still running (include jobId so client can poll)

GET /api/jobs/:jobId
  -> 200 { status, movesAnalyzed, movesTotal, error? }   (read from Redis)

WS /api/jobs/:jobId/stream     (optional, v2)
  -> pushes { movesAnalyzed, movesTotal } as the job progresses
```

`gameId` is the MongoDB `_id` (ObjectId), returned to clients as its hex
string. `jobId` can simply be the same value, since there's a 1:1 mapping
between a game and its analysis job.

## 6. Suggested tech stack

| Layer            | Choice                                                        |
|-------------------|----------------------------------------------------------------|
| Frontend          | React + TypeScript, `react-chessboard`, `chess.js`            |
| API gateway       | Node.js + TypeScript (Fastify) — HTTP, validation, chess.com fetch, enqueue/poll |
| Queue             | Redis (Streams + consumer groups)                              |
| **Analyze service** | **C** — pool of processes managing Stockfish subprocesses over UCI pipes; `hiredis` for the queue, `mongo-c-driver` for results |
| Engine            | Stockfish binary (native), one long-lived subprocess per in-flight game |
| Database          | MongoDB                                                         |
| Deployment        | Docker Compose — see §8                                        |

**Why C for the analyze service**: this is the only part of the system doing
sustained CPU-bound work — managing many concurrent Stockfish subprocesses
and running the accuracy math across every move of every game. C avoids
garbage-collection pauses, keeps per-worker memory overhead low (so more
concurrent Stockfish subprocesses fit per host), and gives direct control
over process spawning and pipe I/O (`fork`/`exec`/`poll` or `epoll`) without
a runtime in the way. The API gateway, by contrast, is I/O-bound and benefits
more from a higher-level language's development speed than from raw
performance — that's why it stays on Node/TS.

## 7. Error handling & limits

- Reject PGNs above a max move count (e.g. 300 plies) and max file size to
  bound worst-case analysis time.
- Reject malformed PGN/URL input with a clear 400 before it ever reaches the
  queue.
- Per-move engine timeout (e.g. 5s) with automatic Stockfish process restart
  on hang, handled inside the C analyze service.
- Per-job overall timeout (e.g. 5 minutes); mark the job `failed` past that
  with an `errorMessage` rather than leaving it `running` forever — an
  unacknowledged Redis stream entry should eventually be reclaimed and
  retried a bounded number of times before giving up.
- Basic IP-based rate limiting on `POST /api/games` (at the API gateway) to
  prevent someone from flooding the worker pool.

## 8. Deployment (Docker Compose)

Five services: `frontend`, `api-gateway`, `redis`, `mongodb`, and
`analyze-service` (the only one that needs to scale with load).

```yaml
version: "3.9"

services:
  frontend:
    build: ./frontend
    ports:
      - "3000:3000"
    depends_on:
      - api-gateway

  api-gateway:
    build: ./api-gateway
    ports:
      - "8080:8080"
    environment:
      - REDIS_URL=redis://redis:6379
      - MONGO_URL=mongodb://mongodb:27017/chess_analyzer
    depends_on:
      - redis
      - mongodb

  analyze-service:
    build: ./analyze-service        # Dockerfile compiles the C binary and
                                     # bundles a Stockfish binary alongside it
    environment:
      - REDIS_URL=redis://redis:6379
      - MONGO_URL=mongodb://mongodb:27017/chess_analyzer
      - STOCKFISH_PATH=/usr/local/bin/stockfish
      - WORKER_CONCURRENCY=4
    depends_on:
      - redis
      - mongodb
    deploy:
      replicas: 3                   # scale independently of the API

  redis:
    image: redis:7-alpine
    volumes:
      - redis-data:/data

  mongodb:
    image: mongo:7
    volumes:
      - mongo-data:/data/db

volumes:
  redis-data:
  mongo-data:
```

`docker compose up --scale analyze-service=N` (or the `deploy.replicas` field
above, under Swarm) lets you scale the CPU-heavy piece independently of
everything else — the API gateway and database don't need to grow at the
same rate as engine throughput does.

## 9. Suggested build order

1. **PGN/URL ingestion** — parse pasted PGN, validate, extract headers. No
   engine yet; just prove the input handling and return game metadata.
2. **Chess.com URL support** — fetch + resolve a game from a pasted URL via
   the chess.com public API.
3. **MongoDB + Redis up via Docker Compose** — get both running locally early
   so every later step can persist/queue against real services, not mocks.
4. **Single-move Stockfish call in C** — get one Stockfish subprocess talking
   over UCI from a small C program, analyze a single position, print the raw
   cp eval. Prove the engine integration in isolation before wiring up the
   full game loop.
5. **Full single-game analysis (synchronous, no queue)** — loop over every
   move in a small test game from the C binary, confirm move-by-move logic,
   win%/accuracy conversion, and classification math are correct against a
   few known chess.com games.
6. **Wire up Redis Streams + MongoDB writes** — turn the C binary into the
   real analyze service: consume jobs from Redis, write progress to the
   Redis hash, write the final document to MongoDB on completion.
7. **API gateway** — implement the HTTP endpoints in §5, enqueue jobs,
   read progress/results, add the dedup/cache check against MongoDB.
8. **Frontend** — chessboard, move list with classification badges, eval
   graph, polling UI for in-progress jobs.
9. **Hardening** — timeouts, rate limiting, process-restart-on-hang,
   max game-size limits, Redis consumer-group retry/dead-letter handling.