"""
F06 — Redis Stream Consumer tests
Verification: pytest tests/test_analyze_service.py

Tests the C analyze-service worker (analyze-worker) which:
  1. Reads jobs from Redis stream (XREADGROUP)
  2. Runs Stockfish analysis via analyze-game subprocess
  3. Writes progress to Redis hash (job:<id>:progress)
  4. Writes final result to MongoDB
  5. XACK after successful MongoDB write

Requires:
  - Docker Compose: redis + mongodb running
  - API gateway running on $API_BASE_URL (started by conftest.py)
  - analyze-worker binary built: src/analyze-service/bin/analyze-worker
  - Stockfish in PATH

Run:
  pytest tests/test_analyze_service.py -v
"""

import json
import os
import signal
import subprocess
import time
import uuid
from typing import Generator

import httpx
import pytest

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
COMPOSE_FILE = os.path.join(REPO_ROOT, "deploy", "docker-compose.yml")
TEST_MONGO_DB = os.getenv("MONGO_DB", "chess_analyzer_test")
TEST_MONGO_URL = os.getenv(
    "TEST_MONGO_URL", f"mongodb://localhost:27018/{TEST_MONGO_DB}"
)

WORKER_BIN = os.path.join(REPO_ROOT, "src", "analyze-service", "bin", "analyze-worker")
STREAM_KEY = "chess:analysis-jobs"
CONSUMER_GROUP = "workers"


def _unique_pgn() -> str:
    tag = uuid.uuid4().hex[:8]
    return (
        f'[Event "F06{tag}"]\n[Site "?"]\n[Date "2024.06.20"]\n'
        f'[White "W{tag}"]\n[WhiteElo "1800"]\n'
        f'[Black "B{tag}"]\n[BlackElo "1750"]\n'
        f'[Result "1-0"]\n[ECO "B20"]\n[TimeControl "300"]\n\n'
        f"1. e4 c5 2. Nf3 d6 3. d4 cxd4 4. Nxd4 Nf6 1-0"
    )


def _mongo_find_game(game_id: str) -> dict:
    """Query MongoDB directly for a game document."""
    expr = f'EJSON.stringify(db.games.findOne({{_id: ObjectId("{game_id}")}}))'
    result = subprocess.run(
        [
            "docker", "compose", "-f", COMPOSE_FILE,
            "exec", "-T", "mongodb",
            "mongosh", "--quiet", "--eval", expr, TEST_MONGO_DB,
        ],
        capture_output=True,
        text=True,
    )
    if result.returncode == 0 and result.stdout.strip():
        try:
            return json.loads(result.stdout)
        except json.JSONDecodeError:
            pass
    return {}


def _redis_hgetall(key: str) -> dict:
    """Read all fields from a Redis hash."""
    result = subprocess.run(
        ["docker", "compose", "-f", COMPOSE_FILE,
         "exec", "-T", "redis",
         "redis-cli", "HGETALL", key],
        capture_output=True,
        text=True,
    )
    fields = {}
    if result.returncode == 0:
        lines = [line.strip() for line in result.stdout.strip().split("\n") if line.strip()]
        for i in range(0, len(lines), 2):
            if i + 1 < len(lines):
                fields[lines[i]] = lines[i + 1]
    return fields


def _xreadgroup_count() -> int:
    """Count pending entries in the stream consumer group."""
    result = subprocess.run(
        ["docker", "compose", "-f", COMPOSE_FILE,
         "exec", "-T", "redis",
         "redis-cli", "XPENDING", STREAM_KEY, CONSUMER_GROUP],
        capture_output=True,
        text=True,
    )
    if result.returncode == 0 and result.stdout.strip():
        lines = result.stdout.strip().split("\n")
        if lines:
            first = lines[0].split()
            if first:
                try:
                    return int(first[1])
                except (ValueError, IndexError):
                    pass
    return 0


@pytest.fixture(scope="module")
def client() -> Generator[httpx.Client, None, None]:
    api_base = os.getenv("API_BASE_URL", "http://localhost:8080")
    with httpx.Client(base_url=api_base, timeout=10.0) as c:
        yield c


@pytest.fixture(scope="module")
def worker_process() -> Generator[subprocess.Popen, None, None]:
    """Start the analyze-worker process for the test module."""
    env = {
        **os.environ,
        "REDIS_URL": "redis://localhost:6379",
        "MONGO_URL": TEST_MONGO_URL,
        "MONGO_DB": TEST_MONGO_DB,
        "WORKER_CONCURRENCY": "1",
        "ENGINE_DEPTH": "12",
    }
    log_path = os.path.join(REPO_ROOT, "worker.log")
    log_file = open(log_path, "w")
    proc = subprocess.Popen(
        [WORKER_BIN],
        env=env,
        stdout=log_file,
        stderr=log_file,
        preexec_fn=os.setsid,
        cwd=REPO_ROOT,
    )
    # Give the worker time to connect and reclaim stale entries
    time.sleep(3)
    yield proc
    # Teardown
    try:
        os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
        proc.wait(timeout=5)
    except Exception:
        pass
    finally:
        log_file.close()


class TestAnalyzeService:
    def test_submit_game_creates_analysis_in_mongodb(
        self, client: httpx.Client, worker_process: subprocess.Popen
    ) -> None:
        """Submit a game via POST /api/games, wait for worker to analyze it."""
        pgn = _unique_pgn()
        resp = client.post("/api/games", json={"pgn": pgn})
        assert resp.status_code == 201, f"Expected 201, got {resp.status_code}: {resp.text}"
        body = resp.json()
        game_id = body["gameId"]

        # Poll until analysis is complete (up to 60s)
        analysis: dict | None = None
        for _ in range(60):
            game_doc = _mongo_find_game(game_id)
            if game_doc.get("analysis", {}).get("status") == "completed":
                analysis = game_doc.get("analysis", {})
                break
            time.sleep(1)

        assert analysis is not None, (
            f"Analysis not completed for game {game_id} after 60s. "
            f"Doc: {_mongo_find_game(game_id)}"
        )
        assert analysis["status"] == "completed"
        assert "moves" in analysis
        assert len(analysis["moves"]) > 0
        assert all("classification" in m for m in analysis["moves"])

    def test_progress_hash_updated_during_processing(
        self, client: httpx.Client, worker_process: subprocess.Popen
    ) -> None:
        """During analysis, the Redis progress hash should show status=running."""
        pgn = _unique_pgn()
        resp = client.post("/api/games", json={"pgn": pgn})
        assert resp.status_code == 201
        game_id = resp.json()["gameId"]

        progress_key = f"job:{game_id}:progress"
        running_seen = False
        completed_seen = False

        for _ in range(60):
            fields = _redis_hgetall(progress_key)
            if fields.get("status") == "running":
                running_seen = True
                moves_total = int(fields.get("movesTotal", "0"))
                assert moves_total > 0
            if fields.get("status") == "completed":
                completed_seen = True
                break
            time.sleep(1)

        assert running_seen, f"Never saw status=running for {game_id}"
        assert completed_seen, f"Never saw status=completed for {game_id}"

    def test_progress_hash_has_ttl_after_completion(
        self, client: httpx.Client, worker_process: subprocess.Popen
    ) -> None:
        """After completion, the progress hash TTL should be <= 300s."""
        pgn = _unique_pgn()
        resp = client.post("/api/games", json={"pgn": pgn})
        assert resp.status_code == 201
        game_id = resp.json()["gameId"]

        progress_key = f"job:{game_id}:progress"

        # Wait for completion
        for _ in range(60):
            fields = _redis_hgetall(progress_key)
            if fields.get("status") == "completed":
                break
            time.sleep(1)

        # Check TTL
        result = subprocess.run(
            ["docker", "compose", "-f", COMPOSE_FILE,
             "exec", "-T", "redis",
             "redis-cli", "TTL", progress_key],
            capture_output=True,
            text=True,
        )
        assert result.returncode == 0
        ttl = int(result.stdout.strip())
        assert 0 < ttl <= 300, f"Expected 0 < TTL <= 300, got {ttl}"

    def test_xack_called_after_completion(
        self, client: httpx.Client, worker_process: subprocess.Popen
    ) -> None:
        """After job completion, XPENDING should show 0 pending entries."""
        pgn = _unique_pgn()
        resp = client.post("/api/games", json={"pgn": pgn})
        assert resp.status_code == 201
        game_id = resp.json()["gameId"]

        # Wait for analysis to complete
        for _ in range(60):
            game_doc = _mongo_find_game(game_id)
            if game_doc.get("analysis", {}).get("status") == "completed":
                break
            time.sleep(1)

        # Give XACK time to be called
        time.sleep(2)

        pending = _xreadgroup_count()
        assert pending == 0, f"Expected 0 pending entries, got {pending}"

    def test_worker_handles_invalid_pgn(
        self, worker_process: subprocess.Popen
    ) -> None:
        """Worker marks job as failed when PGN is unparseable."""
        # Inject a malformed job directly into the Redis stream
        result = subprocess.run(
            ["docker", "compose", "-f", COMPOSE_FILE,
             "exec", "-T", "redis",
             "redis-cli",
             "XADD", STREAM_KEY, "*",
             "gameId", "00000000000000000000000000",
             "pgn", "definitely not a real pgn {{{<"],
            capture_output=True,
            text=True,
        )
        assert result.returncode == 0

        # Wait for the worker to process it
        time.sleep(10)

        progress_key = "job:00000000000000000000000000:progress"
        fields = _redis_hgetall(progress_key)
        # Worker should have marked it failed
        assert fields.get("status") == "failed", (
            f"Expected status=failed, got: {fields}"
        )
        assert worker_process.poll() is None, "Worker crashed"

    def test_correct_move_count_in_mongodb(
        self, client: httpx.Client, worker_process: subprocess.Popen
    ) -> None:
        """movesAnalyzed in MongoDB matches the actual move count."""
        pgn = _unique_pgn()
        resp = client.post("/api/games", json={"pgn": pgn})
        assert resp.status_code == 201
        game_id = resp.json()["gameId"]

        # The test PGN has 8 plies (moves): 1. e4 c5 2. Nf3 d6 3. d4 cxd4 4. Nxd4 Nf6
        expected_moves = 8

        for _ in range(60):
            game_doc = _mongo_find_game(game_id)
            if game_doc.get("analysis", {}).get("status") == "completed":
                break
            time.sleep(1)

        game_doc = _mongo_find_game(game_id)
        analysis = game_doc.get("analysis", {})
        assert analysis.get("status") == "completed"
        assert analysis.get("movesAnalyzed") == expected_moves, (
            f"Expected {expected_moves} moves, got {analysis.get('movesAnalyzed')}"
        )