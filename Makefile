.PHONY: setup dev test check clean stop

setup:
	cd src/api-gateway && npm install
	venv/bin/pip install pytest httpx pytest-asyncio pymongo redis

dev:
	docker compose up -d
	@sleep 2
	cd src/api-gateway && nohup npm run dev > dev.log 2>&1 & echo $$! > src/api-gateway/.pid

stop:
	-kill $$(cat src/api-gateway/.pid) 2>/dev/null || true
	-lsof -t -i :18080 | xargs -r kill -9 2>/dev/null || true
	-rm -f src/api-gateway/.pid
	docker compose down

test:
	venv/bin/pytest tests/

test-analyze-single:
	cd analyze-service && make test-single

check: test test-analyze-single
