# Session Handoff

## Current State
- Feature F01 (PGN ingestion) fully implemented and verified.
- New files: src/api-gateway/** (Fastify/TypeScript service), tests/test_ingestion.py, tests/conftest.py

## What Changed
- `src/api-gateway/` — full Fastify/TypeScript API gateway skeleton
  - `src/models/game.ts` — TypeScript types matching MongoDB doc schema
  - `src/services/db.ts` — MongoDB singleton with index creation
  - `src/services/pgn.ts` — chess.js PGN validation + header extraction + SHA-256 hash
  - `src/routes/games.ts` — POST /api/games handler (Redis stub, MongoDB real)
  - `src/app.ts` — Fastify factory with /health route
  - `src/index.ts` — entrypoint
  - `package.json`, `tsconfig.json`, `Dockerfile`
- `tests/test_ingestion.py` — 11 F01 integration tests
- `tests/conftest.py` — session fixture: starts redis+mongo via compose, builds + runs node dist/index.js
- `deploy/docker-compose.yml` — full 5-service stack
- `deploy/mongo-init.js` — MongoDB index init script
- `deploy/.env.example` — all env vars documented

## Verification
- `pytest tests/test_ingestion.py -v` → 11 passed in 0.14s
- `tsc --noEmit` → 0 errors

## Known Issues / Environment Notes
- Port 8080 occupied on this machine by unknown process → use PORT=8081 locally
- MongoDB host port: 27018 (to avoid conflict with existing `mongodb_container` on 27017)
- When running tests: use `SKIP_INFRA=1 API_BASE_URL=http://localhost:8081` if infra already up
- `ts-node-dev` module caching issue with .js extensions — conftest uses `node dist/index.js`
- conftest auto-runs `npm run build` before starting the test server

## Next Steps
- F02: chess.com URL ingestion (`tests/test_chesscom.py`)
- F03: Docker Compose + Redis XADD integration (`tests/test_infra.py`)