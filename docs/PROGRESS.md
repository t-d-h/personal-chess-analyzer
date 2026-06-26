# Project Progress

## Current State
- Latest commit: (pending F02 commit)
- Test status: 19/19 passing (`pytest tests/`)
- Lint: tsc --noEmit clean (0 errors)

## Completed
- [x] Create Makefile with setup, dev, test, check, clean targets
- [x] Create deploy/docker-compose.yml (5-service stack: frontend, api-gateway, analyze-service, redis, mongodb)
- [x] F01 — PGN ingestion (POST /api/games) — 11/11 tests passing
- [x] F02 — Chess.com URL ingestion (POST /api/games with url field) — 8/8 tests passing

## In Progress
- None

## Known Issues
- Port 8080 is occupied on dev machine by unknown process; API gateway runs on 8081 locally.
- MongoDB host port changed to 27018 to avoid conflict with existing `mongodb_container`.
- `ts-node-dev` has a caching issue with .js module resolution; use `node dist/index.js` for tests.

## Next Steps
- F03: Docker Compose integration + Redis XADD wiring
