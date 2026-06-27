"""
conftest.py — Session-scoped fixtures for integration tests.

Lifecycle:
  1. Start mock chess.com API server on a random port.
  2. Start Redis + MongoDB via Docker Compose.
  3. Wait for MongoDB to be healthy.
  4. Start the API gateway subprocess on a dedicated test port
     with CHESSCOM_API_BASE pointing at the mock server.
  5. Wait for /health to respond.
  6. Yield — tests run.
  7. Teardown: stop API gateway, then docker compose down.

Set environment variable SKIP_INFRA=1 to skip fixture startup
(useful when running against an already-running stack).
"""

import json
import os
import signal
import subprocess
import threading
import time
from http.server import HTTPServer, BaseHTTPRequestHandler

import httpx
import pytest

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
API_GATEWAY_DIR = os.path.join(REPO_ROOT, "src", "api-gateway")
COMPOSE_FILE = os.path.join(REPO_ROOT, "deploy", "docker-compose.yml")

TEST_PORT = int(os.getenv("TEST_API_PORT", "18080"))
TEST_MONGO_DB = os.getenv("MONGO_DB", "chess_analyzer_test")
TEST_MONGO_URL = os.getenv(
    "TEST_MONGO_URL", f"mongodb://localhost:27018/{TEST_MONGO_DB}"
)
TEST_REDIS_URL = os.getenv("TEST_REDIS_URL", "redis://localhost:6379")
STARTUP_TIMEOUT = 60


def _clean_games_collection() -> None:
    empty_filter = json.dumps({})
    subprocess.run(
        [
            "docker", "compose", "-f", COMPOSE_FILE,
            "exec", "-T", "mongodb",
            "mongosh", "--quiet", "--eval",
            f"db.getSiblingDB('{TEST_MONGO_DB}').games.deleteMany({empty_filter})",
        ],
        capture_output=True,
    )


@pytest.fixture(autouse=True, scope="module")
def _clean_games():
    _clean_games_collection()
    yield


SAMPLE_PGN = """\
[Event "Live Chess"]
[Site "Chess.com"]
[Date "2024.03.10"]
[White "MagnusCarlsen"]
[WhiteElo "2850"]
[Black "HikaruNakamura"]
[BlackElo "2750"]
[Result "1-0"]
[ECO "C50"]
[TimeControl "600"]

1. e4 e5 2. Nf3 Nc6 3. Bc4 Bc5 4. d3 Nf6 1-0
"""


class ChesscomMockHandler(BaseHTTPRequestHandler):
    def do_GET(self) -> None:
        if self.path.startswith("/pub/game/"):
            game_id = self.path.split("/pub/game/")[1]
            if game_id == "notfound":
                self.send_response(404)
                self.send_header("Content-Type", "application/json")
                self.end_headers()
                self.wfile.write(json.dumps({"error": "not found"}).encode())
            elif game_id == "nopgn":
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.end_headers()
                self.wfile.write(json.dumps({"game": {}}).encode())
            else:
                unique_pgn = SAMPLE_PGN.replace(
                    "Live Chess", f"Game {game_id}"
                )
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.end_headers()
                self.wfile.write(json.dumps({"pgn": unique_pgn}).encode())
        else:
            self.send_response(404)
            self.end_headers()

    def log_message(self, format: str, *args: object) -> None:
        pass


@pytest.fixture(scope="session")
def mock_chesscom_url():
    server = HTTPServer(("127.0.0.1", 0), ChesscomMockHandler)
    port = server.server_address[1]
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    url = f"http://127.0.0.1:{port}"
    yield url
    server.shutdown()


@pytest.fixture(scope="session", autouse=True)
def api_server(mock_chesscom_url: str):  # type: ignore[type-arg]
    """
    Start infra + API gateway once per test session.
    Sets the API_BASE_URL environment variable for tests that read it.
    """
    skip_infra = os.getenv("SKIP_INFRA", "0") == "1"

    if skip_infra:
        api_base = os.getenv("API_BASE_URL", "http://localhost:8080")
        os.environ["API_BASE_URL"] = api_base
        yield api_base
        return

    # ── 1. Start infra ────────────────────────────────────────────────────────
    print("\n[conftest] Starting Redis + MongoDB...", flush=True)
    subprocess.run(
        ["docker", "compose", "-f", COMPOSE_FILE, "up", "-d", "redis", "mongodb"],
        check=True,
        capture_output=True,
    )

    # ── 2a. Wait for Redis ──────────────────────────────────────────────────
    for attempt in range(20):
        result = subprocess.run(
            [
                "docker", "compose", "-f", COMPOSE_FILE,
                "exec", "-T", "redis",
                "redis-cli", "ping",
            ],
            capture_output=True,
        )
        if result.returncode == 0:
            break
        time.sleep(2)
    else:
        raise RuntimeError("Redis did not become healthy within 40s")

    import socket
    for attempt in range(30):
        try:
            with socket.create_connection(("localhost", 6379), timeout=1):
                break
        except OSError:
            time.sleep(1)
    else:
        raise RuntimeError("Redis host port 6379 did not become ready")

    print("[conftest] Redis ready.", flush=True)

    # ── 2b. Wait for MongoDB ───────────────────────────────────────────────
    for attempt in range(40):
        result = subprocess.run(
            [
                "docker", "compose", "-f", COMPOSE_FILE,
                "exec", "-T", "mongodb",
                "mongosh", "--eval", "db.adminCommand('ping')", "--quiet",
            ],
            capture_output=True,
        )
        if result.returncode == 0:
            break
        time.sleep(2)
    else:
        raise RuntimeError("MongoDB did not become healthy within 80s")

    for attempt in range(30):
        try:
            with socket.create_connection(("localhost", 27018), timeout=1):
                break
        except OSError:
            time.sleep(1)
    else:
        raise RuntimeError("MongoDB host port 27018 did not become ready")

    print("[conftest] MongoDB ready.", flush=True)

    # ── 2c. Clean test database ──────────────────────────────────────────────
    subprocess.run(
        [
            "docker", "compose", "-f", COMPOSE_FILE,
            "exec", "-T", "mongodb",
            "mongosh", "--quiet", "--eval",
            f"db.getSiblingDB('{TEST_MONGO_DB}').dropDatabase()",
        ],
        capture_output=True,
    )
    print("[conftest] Test database cleaned.", flush=True)

    # ── 3. Build + Start API gateway subprocess ─────────────────────────────
    print("[conftest] Building API gateway...", flush=True)
    subprocess.run(
        ["npm", "run", "build"],
        cwd=API_GATEWAY_DIR,
        check=True,
        capture_output=True,
    )

    env = {
        **os.environ,
        "PORT": str(TEST_PORT),
        "MONGO_URL": TEST_MONGO_URL,
        "MONGO_DB": TEST_MONGO_DB,
        "REDIS_URL": TEST_REDIS_URL,
        "LOG_LEVEL": "warn",
        "NODE_ENV": "test",
        "CHESSCOM_API_BASE": mock_chesscom_url,
        "TRUST_PROXY": "1",
        "RATE_LIMIT_MAX": "1000",
        "RATE_LIMIT_WINDOW": "60000",
    }

    proc = subprocess.Popen(
        ["node", "dist/index.js"],
        cwd=API_GATEWAY_DIR,
        env=env,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        preexec_fn=os.setsid,
    )

    api_base = f"http://localhost:{TEST_PORT}"

    # ── 4. Wait for /health ───────────────────────────────────────────────────
    deadline = time.time() + STARTUP_TIMEOUT
    while time.time() < deadline:
        if proc.poll() is not None:
            raise RuntimeError(
                f"API gateway exited unexpectedly (rc={proc.returncode})"
            )
        try:
            r = httpx.get(f"{api_base}/health", timeout=2)
            if r.status_code == 200:
                break
        except Exception:
            pass
        time.sleep(1)
    else:
        try:
            os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
        except Exception:
            pass
        raise RuntimeError(
            f"API gateway did not start within {STARTUP_TIMEOUT}s"
        )

    os.environ["API_BASE_URL"] = api_base
    print(f"[conftest] API gateway ready at {api_base}", flush=True)

    yield api_base

    # ── 5. Teardown ───────────────────────────────────────────────────────────
    print("\n[conftest] Stopping API gateway...", flush=True)
    try:
        os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
        proc.wait(timeout=10)
    except Exception:
        pass

    print("[conftest] Tearing down infra...", flush=True)
    subprocess.run(
        ["docker", "compose", "-f", COMPOSE_FILE, "down", "-v"],
        check=False,
        capture_output=True,
    )
