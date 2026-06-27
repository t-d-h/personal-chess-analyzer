.PHONY: setup dev test check clean stop

setup:
	cd src/api-gateway && npm install
	venv/bin/pip install pytest httpx pytest-asyncio pymongo redis

dev:
	docker compose up -d
	@sleep 2
	cd src/api-gateway && npm run dev & echo $$! > .pid

stop:
	-kill $$(cat src/api-gateway/.pid)
	-rm src/api-gateway/.pid
	docker compose down

test:
	venv/bin/pytest tests/

check: test
