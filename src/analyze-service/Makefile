CC = gcc
CFLAGS = -Wall -Wextra -O2

build:
	mkdir -p bin
	$(CC) $(CFLAGS) src/uci.c src/main_single.c -o bin/analyze-single

build-analyzer:
	mkdir -p bin
	$(CC) $(CFLAGS) src/uci.c src/analyzer.c src/main_game.c -o bin/analyze-game -lm

build-worker:
	mkdir -p bin
	$(CC) $(CFLAGS) src/uci.c src/analyzer.c src/redis_conn.c src/mongo_conn.c src/worker.c src/main_worker.c -o bin/analyze-worker $$(pkg-config --cflags --libs libmongoc-1.0 hiredis) -lpthread -lm

test-single: build
	./bin/analyze-single "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1" 12

test-game: build-analyzer
	node tools/pgn_to_fens.js tests/reference_game.pgn | \
	  ./bin/analyze-game --fens - --book-plies 4 | \
	  python3 tests/check_output.py tests/reference_output.json



