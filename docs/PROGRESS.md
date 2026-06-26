# Project Progress

## Current State
- Latest commit: F08 — Caching / Deduplication (POST /api/games)
- Test status: 46/46 passing (`pytest tests/`); `make test-analyze-single` passing; `make test-analyze-game` passing
- Lint: tsc --noEmit clean (0 errors); C compiles with -Wall -Wextra zero warnings

## Completed
- [x] Create Makefile with setup, dev, test, check, clean targets
- [x] Create deploy/docker-compose.yml (5-service stack: frontend, api-gateway, analyze-service, redis, mongodb)
- [x] F01 — PGN ingestion (POST /api/games) — 11/11 tests passing
- [x] F02 — Chess.com URL ingestion (POST /api/games with url field) — 8/8 tests passing
- [x] F03 — Infra bootstrap (Docker Compose + Redis XADD + MongoDB indexes) — 7/7 tests passing
- [x] F04 — Single-position Stockfish analysis in C — `make test-analyze-single` passing
- [x] F05 — Full game analysis in C — `make test-analyze-game` passing
- [x] F07 — Remaining API endpoints (GET jobs/:id, GET games/:id, GET games/:id/analysis) — 13/13 tests passing
- [x] F08 — Caching / Deduplication (pgnHash + chesscomGameId dedup, 200 cached:true) — 7/7 tests passing

## In Progress
- None

## Known Issues
- Port 8080 is occupied on dev machine by unknown process; API gateway runs on 8081 locally.
- MongoDB host port changed to 27018 to avoid conflict with existing `mongodb_container`.
- `ts-node-dev` has a caching issue with .js module resolution; use `node dist/index.js` for tests.
- pip3 install requires `--break-system-packages` flag on this system.
- chesscomGameId unique sparse index requires dropping the old non-unique index on first startup; `db.ts` handles this automatically.

## Next Steps
- F06: Redis stream consumer — analyze-service reads jobs, writes progress to Redis hash and results to MongoDB
- F09: Frontend — React + TypeScript UI
