"""
F03 — Infra Bootstrap tests (Docker Compose + Redis + MongoDB)
Verification: pytest tests/test_infra.py

Requires:
  - API gateway running on $API_BASE_URL (managed by conftest.py)
  - Redis + MongoDB up via Docker Compose (managed by conftest.py)

Run:
  pytest tests/test_infra.py -v
"""

import os
import subprocess
import uuid

import httpx
import pytest

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
COMPOSE_FILE = os.path.join(REPO_ROOT, "deploy", "docker-compose.yml")

API_BASE = os.getenv("API_BASE_URL", "http://localhost:8080")

TEST_MONGO_DB = os.getenv("MONGO_DB", "chess_analyzer_test")


def _unique_pgn() -> str:
    tag = uuid.uuid4().hex[:8]
    return (
        f'[Event "Infra{tag}"]\n[White "W"]\n[Black "B"]\n'
        f'[Result "*"]\n\n1. e4 e5 *'
    )


@pytest.fixture(scope="module")
def client() -> httpx.Client:
    api_base = os.getenv("API_BASE_URL", "http://localhost:8080")
    with httpx.Client(base_url=api_base, timeout=10.0) as c:
        yield c


class TestRedis:
    def test_ping(self) -> None:
        result = subprocess.run(
            [
                "docker", "compose", "-f", COMPOSE_FILE,
                "exec", "-T", "redis",
                "redis-cli", "ping",
            ],
            capture_output=True,
            text=True,
        )
        assert result.returncode == 0
        assert "PONG" in result.stdout

    def test_consumer_group_exists(self) -> None:
        result = subprocess.run(
            [
                "docker", "compose", "-f", COMPOSE_FILE,
                "exec", "-T", "redis",
                "redis-cli", "XINFO", "GROUPS", "chess:analysis-jobs",
            ],
            capture_output=True,
            text=True,
        )
        assert result.returncode == 0
        assert "workers" in result.stdout

    def test_xadd_after_post(self, client: httpx.Client) -> None:
        resp = client.post("/api/games", json={"pgn": _unique_pgn()})
        assert resp.status_code == 201

        result = subprocess.run(
            [
                "docker", "compose", "-f", COMPOSE_FILE,
                "exec", "-T", "redis",
                "redis-cli", "XLEN", "chess:analysis-jobs",
            ],
            capture_output=True,
            text=True,
        )
        assert result.returncode == 0
        length = int(result.stdout.strip())
        assert length >= 1


class TestMongoDB:
    def test_ping(self) -> None:
        result = subprocess.run(
            [
                "docker", "compose", "-f", COMPOSE_FILE,
                "exec", "-T", "mongodb",
                "mongosh", "--eval", "db.adminCommand('ping')", "--quiet",
            ],
            capture_output=True,
            text=True,
        )
        assert result.returncode == 0
        assert "ok" in result.stdout

    def _get_index_names(self) -> list[str]:
        result = subprocess.run(
            [
                "docker", "compose", "-f", COMPOSE_FILE,
                "exec", "-T", "mongodb",
                "mongosh", "--quiet", "--eval",
                f"db.getSiblingDB('{TEST_MONGO_DB}').games.getIndexes().map(i=>i.name).join('\\n')",
            ],
            capture_output=True,
            text=True,
        )
        assert result.returncode == 0, result.stderr
        return [line.strip() for line in result.stdout.strip().splitlines() if line.strip()]

    def test_games_collection_has_pgnhash_index(self) -> None:
        names = self._get_index_names()
        assert "idx_pgn_hash" in names

    def test_games_collection_has_chesscomid_index(self) -> None:
        names = self._get_index_names()
        assert "idx_chesscom_game_id" in names

    def test_games_collection_has_analysis_status_index(self) -> None:
        names = self._get_index_names()
        assert "idx_analysis_status" in names
