CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -std=c11 -D_POSIX_C_SOURCE=200809L
SRC_DIR := src
BIN_DIR := bin

SINGLE_SRCS := $(SRC_DIR)/uci.c $(SRC_DIR)/analyzer.c $(SRC_DIR)/main_single.c
SINGLE_BIN  := $(BIN_DIR)/analyze-single

GAME_SRCS := $(SRC_DIR)/uci.c $(SRC_DIR)/analyzer.c $(SRC_DIR)/main_game.c
GAME_BIN   := $(BIN_DIR)/analyze-game

TOOLS_DIR  := tools
TESTS_DIR  := tests
NODE_MODULES := $(TOOLS_DIR)/node_modules

.PHONY: build build-single build-game tools test-single test-game clean

build: build-single build-game

build-single: $(SINGLE_BIN)

build-game: $(GAME_BIN) tools

$(SINGLE_BIN): $(SINGLE_SRCS) $(SRC_DIR)/uci.h $(SRC_DIR)/analyzer.h
	mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(SINGLE_SRCS) -lm

$(GAME_BIN): $(GAME_SRCS) $(SRC_DIR)/uci.h $(SRC_DIR)/analyzer.h
	mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(GAME_SRCS) -lm

tools: $(NODE_MODULES)

$(NODE_MODULES):
	cd $(TOOLS_DIR) && npm install --silent

test-single: $(SINGLE_BIN)
	@echo "==> Testing single-position analysis..."
	@timeout 30 ./$(SINGLE_BIN) "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1" 12
	@echo "==> PASSED"

test-game: build-game
	@echo "==> Testing full game analysis..."
	@mkdir -p /tmp/analyze-game-test
	@node $(TOOLS_DIR)/pgn_to_fens.js $(TESTS_DIR)/reference_game.pgn /tmp/analyze-game-test/moves.json > /tmp/analyze-game-test/fens.txt
	@timeout 120 ./$(GAME_BIN) --fens - --sidecar /tmp/analyze-game-test/moves.json < /tmp/analyze-game-test/fens.txt > /tmp/analyze-game-test/output.json
	@python3 $(TESTS_DIR)/check_output.py /tmp/analyze-game-test/output.json
	@echo "==> PASSED"

clean:
	rm -rf $(BIN_DIR)

distclean: clean
	rm -rf $(TOOLS_DIR)/node_modules $(TOOLS_DIR)/package-lock.json
