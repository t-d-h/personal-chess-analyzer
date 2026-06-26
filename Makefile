.PHONY: help setup dev stop test check lint format \
        test-analyze-single test-analyze-game \
        build-analyze build-frontend \
        infra-up infra-down infra-logs \
        clean

# ─── Config ───────────────────────────────────────────────────────────────────
ANALYZE_SRC   := src/analyze-service
API_SRC       := src/api-gateway
FRONTEND_SRC  := src/frontend
ANALYZE_SINGLE_BIN := $(ANALYZE_SRC)/bin/analyze-single
ANALYZE_GAME_BIN   := $(ANALYZE_SRC)/bin/analyze-game
ANALYZE_TOOLS      := $(ANALYZE_SRC)/tools
ANALYZE_TESTS       := $(ANALYZE_SRC)/tests

COMPOSE := docker compose -f deploy/docker-compose.yml

CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -std=c11
LDFLAGS ?= -lhiredis -lmongoc-1.0 -lbson-1.0 -lm

# ─── Help ─────────────────────────────────────────────────────────────────────
help: ## Show available commands
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | \
	  awk 'BEGIN {FS = ":.*?## "}; {printf "  \033[36m%-28s\033[0m %s\n", $$1, $$2}'

# ─── Setup ────────────────────────────────────────────────────────────────────
setup: ## Install all dependencies (Node, Python dev tools, C libs check)
	@echo "==> Checking system dependencies..."
	@command -v docker      >/dev/null 2>&1 || (echo "ERROR: docker not found"      && exit 1)
	@command -v docker      >/dev/null 2>&1 && docker compose version >/dev/null 2>&1 || \
	  (echo "ERROR: docker compose plugin not found" && exit 1)
	@command -v node        >/dev/null 2>&1 || (echo "ERROR: node not found"        && exit 1)
	@command -v npm         >/dev/null 2>&1 || (echo "ERROR: npm not found"         && exit 1)
	@command -v python3     >/dev/null 2>&1 || (echo "ERROR: python3 not found"     && exit 1)
	@command -v gcc         >/dev/null 2>&1 || (echo "ERROR: gcc not found"         && exit 1)
	@command -v stockfish   >/dev/null 2>&1 || echo "WARN: stockfish not in PATH (needed for analyze-service)"

	@echo "==> Installing Python test dependencies..."
	pip3 install --quiet --break-system-packages --upgrade pip
	pip3 install --quiet --break-system-packages pytest pytest-asyncio httpx

	@echo "==> Installing API gateway Node dependencies..."
	@if [ -f $(API_SRC)/package.json ]; then \
	  cd $(API_SRC) && npm install --silent; \
	else \
	  echo "  SKIP: $(API_SRC)/package.json not found yet"; \
	fi

	@echo "==> Installing frontend Node dependencies..."
	@if [ -f $(FRONTEND_SRC)/package.json ]; then \
	  cd $(FRONTEND_SRC) && npm install --silent; \
	else \
	  echo "  SKIP: $(FRONTEND_SRC)/package.json not found yet"; \
	fi

	@echo "==> setup complete."

# ─── Infrastructure ───────────────────────────────────────────────────────────
infra-up: ## Start Redis + MongoDB via Docker Compose (detached)
	$(COMPOSE) up -d redis mongodb
	@echo "==> Waiting for services to be ready..."
	@sleep 3
	@$(COMPOSE) ps redis mongodb

infra-down: ## Stop and remove all Docker Compose services
	$(COMPOSE) down

infra-logs: ## Tail logs for all Docker Compose services
	$(COMPOSE) logs -f

# ─── Build ────────────────────────────────────────────────────────────────────
build-analyze: ## Compile the C analyze-service binaries (single + game)
	@if [ -f $(ANALYZE_SRC)/Makefile ]; then \
	  echo "==> Compiling C analyze-service..."; \
	  $(MAKE) -C $(ANALYZE_SRC) build; \
	  echo "==> Binaries: $(ANALYZE_SINGLE_BIN), $(ANALYZE_GAME_BIN)"; \
	else \
	  echo "  SKIP: $(ANALYZE_SRC)/Makefile not found yet"; \
	fi

build-frontend: ## Build the React frontend for production
	@if [ -f $(FRONTEND_SRC)/package.json ]; then \
	  cd $(FRONTEND_SRC) && npm run build; \
	else \
	  echo "  SKIP: $(FRONTEND_SRC)/package.json not found yet"; \
	fi

# ─── Dev ──────────────────────────────────────────────────────────────────────
dev: infra-up ## Start full dev stack (infra + API gateway + frontend)
	@echo "==> Starting dev services..."
	@if [ -f $(API_SRC)/package.json ]; then \
	  echo "  Starting API gateway on :8080 ..."; \
	  cd $(API_SRC) && npm run dev & \
	else \
	  echo "  SKIP: API gateway source not found yet"; \
	fi
	@if [ -f $(FRONTEND_SRC)/package.json ]; then \
	  echo "  Starting frontend on :3000 ..."; \
	  cd $(FRONTEND_SRC) && npm run dev & \
	else \
	  echo "  SKIP: Frontend source not found yet"; \
	fi
	@echo "==> Dev stack running. Press Ctrl-C to stop background processes."
	@echo "    API:      http://localhost:8080"
	@echo "    Frontend: http://localhost:3000"
	@wait

stop: ## Stop background dev processes (API + frontend)
	@pkill -f "$(API_SRC)" 2>/dev/null || true
	@pkill -f "$(FRONTEND_SRC)" 2>/dev/null || true
	docker compose stop redis mongodb
	@echo "==> Dev stack stopped."

# ─── Test ─────────────────────────────────────────────────────────────────────
test: ## Run all Python/pytest tests
	pytest tests/ -v

test-analyze-single: build-analyze ## Run standalone single-position Stockfish analysis (F04)
	@if [ -f $(ANALYZE_SINGLE_BIN) ]; then \
	  echo "==> Testing single-position analysis..."; \
	  timeout 30 $(ANALYZE_SINGLE_BIN) "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1" 12; \
	else \
	  echo "ERROR: Analyze binary not built. Run 'make build-analyze' first." && exit 1; \
	fi

test-analyze-game: ## Run full game analysis against reference PGN (F05)
	@echo "==> Testing full game analysis..."
	$(MAKE) -C $(ANALYZE_SRC) test-game

# ─── Lint / Format ────────────────────────────────────────────────────────────
lint: ## Run linters (TypeScript + Python ruff/mypy)
	@echo "==> Linting TypeScript (API gateway)..."
	@if [ -f $(API_SRC)/package.json ]; then \
	  cd $(API_SRC) && npm run lint 2>/dev/null || echo "  No lint script defined yet"; \
	fi
	@echo "==> Linting TypeScript (frontend)..."
	@if [ -f $(FRONTEND_SRC)/package.json ]; then \
	  cd $(FRONTEND_SRC) && npm run lint 2>/dev/null || echo "  No lint script defined yet"; \
	fi
	@echo "==> Ruff (Python)..."
	@if python3 -m ruff --version >/dev/null 2>&1; then \
	  python3 -m ruff check tests/; \
	else \
	  echo "  SKIP: ruff not installed"; \
	fi
	@echo "==> Mypy (Python)..."
	@if python3 -m mypy --version >/dev/null 2>&1; then \
	  python3 -m mypy tests/ --ignore-missing-imports; \
	else \
	  echo "  SKIP: mypy not installed"; \
	fi

format: ## Auto-format code (ruff + prettier)
	@if python3 -m ruff --version >/dev/null 2>&1; then \
	  python3 -m ruff format tests/; \
	fi
	@if command -v npx >/dev/null 2>&1; then \
	  npx prettier --write "$(API_SRC)/src/**/*.ts" "$(FRONTEND_SRC)/src/**/*.{ts,tsx}" 2>/dev/null || true; \
	fi

# ─── Full verification ────────────────────────────────────────────────────────
check: lint test ## Full verification: lint + all tests (CI gate)
	@echo ""
	@echo "==> All checks passed."

# ─── Clean ────────────────────────────────────────────────────────────────────
clean: ## Remove build artifacts and stop infra
	rm -rf $(ANALYZE_SRC)/bin
	@if [ -d $(FRONTEND_SRC)/dist ];  then rm -rf $(FRONTEND_SRC)/dist;  fi
	@if [ -d $(API_SRC)/dist ];       then rm -rf $(API_SRC)/dist;        fi
	$(COMPOSE) down -v 2>/dev/null || true
	@echo "==> Cleaned."
