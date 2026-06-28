# Project Progress

## Current State
- Latest commit: 5886e23 ("test: update mock chesscom game PGN to the real 12-move game")
- Test status: passing (make check)
- Lint: passing (vite build passes)

## Completed
- [x] Fix chessboard pieces not moving visually on navigate (due to react-chessboard v5 options prop change)
- [x] Fix cached games loading / API gateway 404 when Redis progress key is expired
- [x] Update mock Chess.com game 170638222548 PGN to match the real 12-move game
- [x] Create Makefile with setup, dev, test, clean.
- [x] Implement F01 (PGN Ingestion API)
  - Created Fastify API Gateway
  - Added PGN validation using chess.js
  - Added MongoDB persistence
  - Setup test container for mongodb
- [x] Implement F02 (Chess.com URL Support)
  - Created chesscom service for fetching game PGNs
  - Added support for mock testing
  - Handled rate limiting/errors properly

- [x] Implement F03 (Docker Compose and Redis queue setup)
  - Added redis to docker-compose
  - Initialized chess:analysis-jobs consumer group on startup
  - Connected POST /api/games to enqueue XADD tasks

- [x] Implement F04 (C analyze-service Stockfish FEN evaluation)
  - Created C CLI tool `analyze-single` to spawn Stockfish via UCI.
  - Implemented UCI info and bestmove parsing for centipawn evaluation.

- [x] Implement F05 (C analyze-service full game loop and move classification)
  - Created `tools/pgn_to_fens.js` to parse PGNs into plies with FENs and UCI-to-SAN legal move mappings.
  - Implemented `analyzer.c` for win%, accuracy decay, and centipawn-loss move classification.
  - Created `main_game.c` implementing an N+1 position analysis loop with a single long-lived Stockfish process.
  - Wired up verification script (`check_output.py`) using a Scholar's Mate reference game.

- [x] Implement F06 (C analyze-service Redis queue consumer and MongoDB persistence)
  - Implemented hiredis and libmongoc connections.
  - Implemented stream worker polling XREADGROUP/XAUTOCLAIM and XACK only after DB updates.
  - Added live progress updates using Redis HSET hash with TTL.
  - Wrote integration tests covering success and failure execution paths.

- [x] Implement F07 (GET /api/jobs/:jobId and GET /api/games/:gameId endpoints)
  - Implemented GET /api/jobs/:jobId reading live progress from Redis hash.
  - Implemented GET /api/games/:gameId fetching game metadata with MongoDB projection.
  - Implemented GET /api/games/:gameId/analysis returning moves/playerSummaries on completion and 409/500 otherwise.
  - Added robust validation and live fallback logic.
  - Wrote integration tests covering all scenarios in tests/test_api.py.

- [x] Implement F09 (React + TS Frontend)
  - Scaffolded React + TypeScript Vite project inside frontend/
  - Created api service, custom hooks (useJobPoller), and components (InputForm, ProgressBar, Chessboard, MoveList, EvalGraph, PlayerStats)
  - Implemented responsive, interactive panels with custom dark mode styling
  - Wrote playwright E2E test suite covering PGN submissions, progress bar updates, move navigation, graph rendering, accuracy stats, and a specific user-requested Chess.com URL E2E test using headless=False.

- [x] Implement F08 (POST /api/games deduplication)
  - Implemented URL pre-check by chesscomGameId to avoid unnecessary external API requests.
  - Normalized PGN to compute SHA-256 pgnHash (ignoring comments/clocks/spaces).
  - Added race condition handling using MongoDB duplicate key E11000 exception catching.
  - Added full test suite in tests/test_cache.py validating all caching/deduplication requirements.

- [x] Implement F11 (Support for Chess.com Analysis Review URLs)
  - Updated frontend InputForm to accept /analysis/game/ URLs.
  - Validated API Gateway parses analysis review URLs natively and deduplicates correctly.
  - Added test_chesscom.py and test_frontend_e2e.py test coverage.

- [x] Implement F12 (Game Rating & Phase-Specific Accuracy)
  - Updated `worker.c` to categorize moves into Opening, Middlegame, and Endgame and track independent accuracy.
  - Implemented heuristic Elo game rating based on accuracy percentages.
  - Updated API typings and PlayerStats frontend component to display the new rating and a Phase Review report card.

- [x] Implement F13 (UI Last Move Indicator)
  - Visually indicate the last move on the chessboard by highlighting the "from" and "to" squares.
  - Rendered a custom arrow between them using `react-chessboard` properties.

## In Progress
- [ ] F15: Scan Chess.com User (Planning)
- [ ] F16: Save Analyzed Games in Memory (Redis Cache) (Planning)

## Known Issues
- None

## Next Steps
- Implement and verify F16 design.
- Review and approve F15 design.
- Propose and approve F14 design, then implement it.
- Implement F10 (Hardening and timeouts)


