# Session Handoff

## Current State
- Features F01–F10 fully implemented and verified.
- 66/66 tests passing in the total test suite (`make check` passes completely).
- Clean exit code across pytest, eslint/tsc linting, ruff, and mypy checks.

## What Changed (this session)
- **F10 — Hardening implemented**:
  - **API gateway rate limiting**: Installed `@fastify/rate-limit@^9.0.0` (compatible with Fastify v4). Configured globally with `RATE_LIMIT_MAX` and `RATE_LIMIT_WINDOW` env vars (default: 10 req/min). Added `trustProxy` support via `TRUST_PROXY=1` env var.
  - **PGN size limits**: Added `bodyLimit: 600KB` to Fastify options. Added `Buffer.byteLength` check for 512KB PGN limit in both PGN paste and chess.com URL code paths.
  - **Per-move engine timeout**: Rewrote `uci.c` to use `poll()` + raw fd read buffer instead of `FILE*` buffered I/O. The `sf_read_line_raw()` function polls the stdout pipe with a configurable timeout (`MOVE_TIMEOUT_MS`, default 5000ms). On timeout, `sf_analyze_fen()` returns -1, `main_game.c` kills the Stockfish process group (`setpgid` + `kill(-pgid, SIGKILL)`), restarts via `sf_restart()`, records the move as null eval, and continues.
  - **Per-job overall timeout**: Added `job_timeout_secs` to `WorkerCtx` (default `JOB_TIMEOUT_SECS=300`, configurable via `JOB_TIMEOUT` env var). `run_analyze_game()` in `worker.c` uses `poll()` with a computed remaining timeout after each read. On timeout (return code -2), the worker marks the job as `failed` with `errorMessage="job timed out after 5 minutes"` and XACKs the stream entry.
  - **Config module**: Created `src/api-gateway/src/config.ts` for shared rate-limit constants.
  - **Tests**: 7 tests in `tests/test_hardening.py` covering rate limit headers, 429 on excess, PGN > 512KB rejection, 301-ply rejection, 300-ply acceptance, move-timeout null eval, and timeout-then-continue-after-restart.

## Key Design Decisions
- Switched from `FILE*` (fdopen) to raw `read()` with an 8KB internal read buffer in `StockfishProc` to fix the classic `poll()` + `FILE*` buffering conflict where `fgets` buffers more data than one line, causing `poll` to report "no data" when the kernel pipe is empty but the `FILE*` buffer still has data.
- Used process groups (`setpgid`) and `kill(-pgid, SIGKILL)` to ensure orphaned child processes from the Stockfish subprocess (e.g., `sleep` from a mock shell script) are properly cleaned up on timeout.
- Set `RATE_LIMIT_MAX=1000` in the test conftest to prevent rate limiting from interfering with other test suites. The hardening test starts its own dedicated server with `RATE_LIMIT_MAX=5`.

## Environment / Running Tests
- Docker compose services `redis` and `mongodb` are running.
- Build worker binary: `make build-analyze`
- API Gateway PORT: 18080 (test), 8080 (default)
- Clean run command: `make check`

## Env Vars Added (F10)
| Variable | Default | Description |
|----------|---------|-------------|
| `JOB_TIMEOUT` | `300` | Per-job hard deadline (seconds) |
| `MOVE_TIMEOUT_MS` | `5000` | Per-move Stockfish response deadline (ms) |
| `RATE_LIMIT_MAX` | `10` | Max POST /api/games per window |
| `RATE_LIMIT_WINDOW` | `60000` | Rate limit window (ms) |
| `TRUST_PROXY` | `0` | Set to `1` to trust X-Forwarded-For header |

## Next Steps
- All planned features (F01-F10) are complete.
