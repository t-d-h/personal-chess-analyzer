# Session Handoff

## Current State
- Features F01–F09 fully implemented and verified.
- F06 (Redis stream C worker) and F09 (React + TS Frontend) fully verified and 100% passing.
- 59/59 tests passing in the total test suite (`make check` passes completely).
- Clean exit code across pytest, eslint/tsc linting, ruff, and mypy checks.

## What Changed (this session)
- **CORS Support added to API Gateway**: Installed `@fastify/cors` (version `^8.3.0` to match Fastify v4) and registered it in `src/api-gateway/src/app.ts` to allow cross-origin requests from the React frontend dev server. This successfully resolved the browser E2E test failures.
- **Robust EINTR signal handling in C worker**: Modified read/write loops in `worker.c` (specifically `pgn_to_fens` and `run_analyze_game`) to handle `EINTR` signals instead of failing or closing pipes prematurely. This resolved intermittent failures caused by asynchronous `SIGCHLD` signals.
- **Buffer/Stack Overrun Bug Fix**: Null-terminated `fens_buf` in `worker.c` before passing it to `run_analyze_game` to prevent `strlen` from reading garbage memory from the stack and sending invalid inputs to Stockfish.
- **feature_list.json / PROGRESS.md**: Updated F06 and F09 states to completed with verification evidence.

## Key Design Decisions
- Registered `@fastify/cors` with wildcard origin `*` for both local testing and cross-origin container-to-container communication.
- Implemented loop retries on `errno == EINTR` in all C blocking read/write calls to prevent premature termination of pipes when receiving asynchronous signals.
- Null-terminated dynamically filled buffers in the worker script to guarantee memory safety.

## Environment / Running Tests
- Docker compose services `redis` and `mongodb` are running.
- Build worker binary: `make build-analyze`
- API Gateway PORT: 18080 or 8080 (if port 8080 is not occupied).
- Clean run command: `make check`

## Next Steps (F10 — Hardening)
1. Implement IP-based rate limiting on `POST /api/games`.
2. Restrict PGN size (max 300 plies or file size limit) on ingestion.
3. Add a per-move Stockfish timeout (5s) with automatic engine restart.
4. Add a per-job worker timeout (5min) marking jobs failed and XACK-ing.
