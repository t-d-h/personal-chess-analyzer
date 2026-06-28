.PHONY: setup dev test check clean stop e2e

setup:
	cd src/api-gateway && npm install
	cd frontend && npm install
	venv/bin/pip install pytest httpx pytest-asyncio pymongo redis playwright
	venv/bin/playwright install chromium

dev:
	docker compose up -d
	@sleep 2
	cd src/api-gateway && nohup npm run dev > dev.log 2>&1 & echo $$! > src/api-gateway/.pid
	cd frontend && nohup npm run dev > dev.log 2>&1 & echo $$! > frontend/.pid

stop:
	-kill $$(cat src/api-gateway/.pid) 2>/dev/null || true
	-lsof -t -i :18080 | xargs -r kill -9 2>/dev/null || true
	-rm -f src/api-gateway/.pid
	-kill $$(cat frontend/.pid) 2>/dev/null || true
	-lsof -t -i :3000 | xargs -r kill -9 2>/dev/null || true
	-rm -f frontend/.pid
	docker compose down

test:
	cd analyze-service && make build-worker
	venv/bin/pytest tests/

test-analyze-single:
	cd analyze-service && make test-single

test-analyze-game:
	cd analyze-service && make test-game

check: test test-analyze-single test-analyze-game

e2e:
	venv/bin/python tests/run_e2e.py


