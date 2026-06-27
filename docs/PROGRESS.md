# Project Progress

## Current State
- Latest commit: (to be created)
- Test status: passing (make check)
- Lint: N/A

## Completed
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

## In Progress
- None

## Known Issues
- None

## Next Steps
- Implement F05 (C analyze-service full game loop and move classification)
