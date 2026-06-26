# Session Handoff

## Current State
- Features F01–F05 fully implemented and verified.
- F05 adds full game analysis: per-move eval, win% loss, accuracy, move classification.

## What Changed
- `src/analyze-service/src/analyzer.h` — MoveResult, PlayerSummary, GameAnalysis structs; Classification enum; win_percent, move_accuracy, classify, classification_str, compute_player_summary API
- `src/analyze-service/src/analyzer.c` — Implementation of win% conversion (logistic formula), move accuracy (exponential decay), classification logic (book/best/excellent/good/inaccuracy/mistake/blunder/brilliant), player summary aggregation
- `src/analyze-service/src/main_game.c` — CLI entry point for full game: reads FENs from stdin, sidecar JSON with SAN+captures, runs Stockfish multipv=2 on each position, computes per-move analysis, outputs JSON to stdout
- `src/analyze-service/tools/pgn_to_fens.js` — Node.js helper using chess.js: expands PGN → newline-separated FEN list (stdout) + sidecar JSON with SAN moves and capture flags
- `src/analyze-service/tools/package.json` + `node_modules/chess.js` — chess.js dependency for PGN parsing
- `src/analyze-service/tests/reference_game.pgn` — Scholar's Mate game (7 plies, known outcome)
- `src/analyze-service/tests/check_output.py` — Validates output JSON: structural checks, range checks, classification validity, player summary consistency
- `src/analyze-service/Makefile` — Added `build-game`, `tools`, `test-game`, `distclean` targets
- Top-level `Makefile` — Updated `ANALYZE_GAME_BIN`, `test-analyze-game` delegates to sub-Makefile
- `docs/feature_list.json` — F05 state → completed

## Key Design Decisions
- Two-phase pipeline: `pgn_to_fens.js` (Node/chess.js) → `analyze-game` (C/Stockfish) to avoid reimplementing chess board in C
- Sidecar JSON carries SAN + capture info; FENs piped via stdin
- multipv=2 used to detect sacrifices (BRILLIANT): requires is_capture + is_best_move + 2nd-best move within 50cp
- BOOK_PLIES=10: first 10 plies classified as "book" regardless of eval
- Black's cp values are negated from engine's perspective (engine always reports from white's POV)

## Verification
- `make test-analyze-single` → `cp=-34 best=c7c5` (F04 still passing)
- `make test-analyze-game` → `VALID: 7 moves, white accuracy=100.0%, black accuracy=100.0%`
- `make check` → All checks passed (26 Python tests + lint)
- C binaries compile with `-Wall -Wextra` and zero warnings

## Known Issues / Environment Notes
- Port 8080 occupied → use PORT=8081 locally
- MongoDB host port: 27018
- Stockfish binary at `/usr/games/stockfish` (Stockfish 16)
- `STOCKFISH_PATH` env var supported for override

## Next Steps
- F06: Redis stream consumer — analyze-service consumes jobs from Redis, writes progress/results
