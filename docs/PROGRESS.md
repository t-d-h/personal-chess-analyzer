# Project Progress

## Current State
- Latest commit: F10 — Hardening (rate limiting, input size guards, engine timeouts)
- Test status: 66/66 passing (`pytest tests/`); `make test-analyze-single` passing; `make test-analyze-game` passing
- Lint: tsc --noEmit clean (0 errors); C compiles with -Wall -Wextra zero warnings
- F06 worker (analyze-worker) binary built and compiles clean with pkg-config

## Completed
- [x] Create Makefile with setup, dev, test, check, clean targets
- [x] Create deploy/docker-compose.yml (5-service stack: frontend, api-gateway, analyze-service, redis, mongodb)
- [x] F01 — PGN ingestion (POST /api/games) — 11/11 tests passing
- [x] F02 — Chess.com URL ingestion (POST /api/games with url field) — 8/8 tests passing
- [x] F03 — Infra bootstrap (Docker Compose + Redis XADD + MongoDB indexes) — 7/7 tests passing
- [x] F04 — Single-position Stockfish analysis in C — `make test-analyze-single` passing
- [x] F05 — Full game analysis in C — `make test-analyze-game` passing
- [x] F06 — Redis stream consumer (C worker with XREADGROUP, progress hash, MongoDB write) — 6/6 tests passing
- [x] F07 — Remaining API endpoints (GET jobs/:id, GET games/:id, GET games/:id/analysis) — 13/13 tests passing
- [x] F08 — Caching / Deduplication (pgnHash + chesscomGameId dedup, 200 cached:true) — 7/7 tests passing
- [x] F09 — Frontend — React + TypeScript UI — 7/7 E2E tests passing
- [x] F10 — Hardening (rate limiting, PGN size limits, engine timeouts) — 7/7 tests passing

## In Progress
- None

## Known Issues
- Port 8080 is occupied on dev machine by Coc Coc CS 1.6 Server; local development API gateway uses port 18080 or other free ports.
- MongoDB host port changed to 27018 to avoid conflict with existing `mongodb_container`.
- `ts-node-dev` has a caching issue with .js module resolution; use `node dist/index.js` for tests.
- pip3 install requires `--break-system-packages` flag on this system.
- chesscomGameId unique sparse index requires dropping the old non-unique index on first startup; `db.ts` handles this automatically.

## Next Steps
- All planned features (F01-F10) are complete.
