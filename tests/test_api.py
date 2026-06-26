"""
F07 — Remaining API Endpoints tests
Verification: pytest tests/test_api.py

Tests:
  GET /api/jobs/:jobId         — reads Redis hash
  GET /api/games/:gameId       — reads MongoDB metadata
  GET /api/games/:gameId/analysis — reads MongoDB analysis

Requires:
  - API gateway running on $API_BASE_URL (managed by conftest.py)
  - Redis + MongoDB up via Docker Compose (managed by conftest.py)

Run:
  pytest tests/test_api.py -v
"""

import os
import subprocess
import uuid

from typing import Generator

import httpx
import pytest

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
COMPOSE_FILE = os.path.join(REPO_ROOT, "deploy", "docker-compose.yml")

API_BASE = os.getenv("API_BASE_URL", "http://localhost:8080")
TEST_MONGO_DB = os.getenv("MONGO_DB", "chess_analyzer_test")


def _unique_pgn() -> str:
    tag = uuid.uuid4().hex[:8]
    return (
        f'[Event "T{tag}"]\n[Site "?"]\n[Date "2024.06.15"]\n'
        f'[White "W{tag}"]\n[WhiteElo "1800"]\n'
        f'[Black "B{tag}"]\n[BlackElo "1750"]\n'
        f'[Result "1-0"]\n[ECO "B20"]\n[TimeControl "300"]\n\n'
        f"1. e4 c5 2. Nf3 d6 1-0"
    )


def _redis_cli(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        ["docker", "compose", "-f", COMPOSE_FILE, "exec", "-T", "redis", "redis-cli", *args],
        capture_output=True,
        text=True,
    )


def _mongo_update(game_id: str, fields: dict) -> None:
    import json

    update_doc = {"$set": fields}
    update_json = json.dumps(update_doc)
    expr = (
        'db.games.updateOne({_id: ObjectId("' + game_id + '")}, '
        + update_json
        + ")"
    )
    subprocess.run(
        [
            "docker", "compose", "-f", COMPOSE_FILE,
            "exec", "-T", "mongodb",
            "mongosh", "--quiet", "--eval", expr, TEST_MONGO_DB,
        ],
        capture_output=True,
        text=True,
    )


@pytest.fixture(scope="module")
def client() -> Generator[httpx.Client, None, None]:
    api_base = os.getenv("API_BASE_URL", "http://localhost:8080")
    with httpx.Client(base_url=api_base, timeout=10.0) as c:
        yield c


@pytest.fixture()
def fresh_game(client: httpx.Client) -> dict:
    pgn = _unique_pgn()
    resp = client.post("/api/games", json={"pgn": pgn})
    assert resp.status_code == 201
    return resp.json()


class TestGetJobs:
    def test_running_job(self, client: httpx.Client, fresh_game: dict) -> None:
        game_id = fresh_game["gameId"]
        _redis_cli("HSET", f"job:{game_id}:progress", "status", "running", "movesAnalyzed", "14", "movesTotal", "38")
        _redis_cli("EXPIRE", f"job:{game_id}:progress", "3600")

        resp = client.get(f"/api/jobs/{game_id}")
        assert resp.status_code == 200
        body = resp.json()
        assert body["jobId"] == game_id
        assert body["status"] == "running"
        assert body["movesAnalyzed"] == 14
        assert body["movesTotal"] == 38
        assert body["error"] is None

        _redis_cli("DEL", f"job:{game_id}:progress")

    def test_completed_job(self, client: httpx.Client, fresh_game: dict) -> None:
        game_id = fresh_game["gameId"]
        _redis_cli("HSET", f"job:{game_id}:progress", "status", "completed", "movesAnalyzed", "38", "movesTotal", "38")
        _redis_cli("EXPIRE", f"job:{game_id}:progress", "300")

        resp = client.get(f"/api/jobs/{game_id}")
        assert resp.status_code == 200
        body = resp.json()
        assert body["status"] == "completed"
        assert body["movesAnalyzed"] == 38

        _redis_cli("DEL", f"job:{game_id}:progress")

    def test_failed_job(self, client: httpx.Client, fresh_game: dict) -> None:
        game_id = fresh_game["gameId"]
        _redis_cli("HSET", f"job:{game_id}:progress", "status", "failed", "errorMessage", "engine timeout")
        _redis_cli("EXPIRE", f"job:{game_id}:progress", "300")

        resp = client.get(f"/api/jobs/{game_id}")
        assert resp.status_code == 200
        body = resp.json()
        assert body["status"] == "failed"
        assert body["error"] == "engine timeout"

        _redis_cli("DEL", f"job:{game_id}:progress")

    def test_unknown_job(self, client: httpx.Client) -> None:
        resp = client.get("/api/jobs/000000000000000000000001")
        assert resp.status_code == 404

    def test_invalid_job_id(self, client: httpx.Client) -> None:
        resp = client.get("/api/jobs/not-an-id")
        assert resp.status_code == 404


class TestGetGame:
    def test_valid_game(self, client: httpx.Client, fresh_game: dict) -> None:
        game_id = fresh_game["gameId"]
        resp = client.get(f"/api/games/{game_id}")
        assert resp.status_code == 200
        body = resp.json()
        assert body["gameId"] == game_id
        assert body["source"] == "pgn_paste"
        assert body["result"] == "1-0"
        assert body["ecoCode"] == "B20"
        assert "moves" not in body
        assert "playerSummaries" not in body

    def test_game_not_found(self, client: httpx.Client) -> None:
        resp = client.get("/api/games/000000000000000000000001")
        assert resp.status_code == 404

    def test_invalid_game_id(self, client: httpx.Client) -> None:
        resp = client.get("/api/games/badid")
        assert resp.status_code == 404


class TestGetAnalysis:
    def test_analysis_completed(self, client: httpx.Client, fresh_game: dict) -> None:
        game_id = fresh_game["gameId"]

        _mongo_update(game_id, {
            "analysis.status": "completed",
            "analysis.movesAnalyzed": 5,
            "analysis.moves": [{"ply": 1, "color": "white", "san": "e4", "fenBefore": "...", "fenAfter": "...", "evalCpPlayed": 25, "evalCpBest": 25, "evalMate": None, "bestMoveSan": "e4", "winPercentLoss": 0, "moveAccuracy": 100, "classification": "book", "engineDepth": 18}],
            "analysis.playerSummaries": [{"color": "white", "accuracyPct": 100, "acpl": 0, "bestCount": 1, "excellentCount": 0, "goodCount": 0, "inaccuracyCount": 0, "mistakeCount": 0, "blunderCount": 0, "brilliantCount": 0}],
        })

        resp = client.get(f"/api/games/{game_id}/analysis")
        assert resp.status_code == 200
        body = resp.json()
        assert body["gameId"] == game_id
        assert isinstance(body["moves"], list)
        assert len(body["moves"]) == 1
        assert body["moves"][0]["san"] == "e4"
        assert isinstance(body["playerSummaries"], list)
        assert len(body["playerSummaries"]) == 1
        assert body["playerSummaries"][0]["color"] == "white"

    def test_analysis_running(self, client: httpx.Client, fresh_game: dict) -> None:
        game_id = fresh_game["gameId"]

        _mongo_update(game_id, {
            "analysis.status": "running",
            "analysis.movesAnalyzed": 2,
        })

        _redis_cli("HSET", f"job:{game_id}:progress", "status", "running", "movesAnalyzed", "2", "movesTotal", "4")
        _redis_cli("EXPIRE", f"job:{game_id}:progress", "3600")

        resp = client.get(f"/api/games/{game_id}/analysis")
        assert resp.status_code == 409
        body = resp.json()
        assert "in progress" in body["error"]
        assert body["jobId"] == game_id
        assert body["status"] == "running"
        assert body["movesAnalyzed"] == 2
        assert body["movesTotal"] == 4

        _redis_cli("DEL", f"job:{game_id}:progress")

    def test_analysis_failed(self, client: httpx.Client, fresh_game: dict) -> None:
        game_id = fresh_game["gameId"]

        _mongo_update(game_id, {
            "analysis.status": "failed",
            "analysis.errorMessage": "engine crash",
        })

        resp = client.get(f"/api/games/{game_id}/analysis")
        assert resp.status_code == 500
        body = resp.json()
        assert body["errorMessage"] == "engine crash"

    def test_analysis_game_not_found(self, client: httpx.Client) -> None:
        resp = client.get("/api/games/000000000000000000000001/analysis")
        assert resp.status_code == 404

    def test_analysis_invalid_game_id(self, client: httpx.Client) -> None:
        resp = client.get("/api/games/badid/analysis")
        assert resp.status_code == 404
