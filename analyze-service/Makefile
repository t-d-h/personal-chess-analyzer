CC = gcc
CFLAGS = -Wall -Wextra -O2

build:
	mkdir -p bin
	$(CC) $(CFLAGS) src/uci.c src/main_single.c -o bin/analyze-single

test-single: build
	./bin/analyze-single "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1" 12
