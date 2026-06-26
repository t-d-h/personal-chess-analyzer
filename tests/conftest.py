"""
conftest.py — Session-scoped fixtures for F01 integration tests.

Lifecycle:
  1. Start Redis + MongoDB via Docker Compose.
  2. Wait for MongoDB to be healthy.
  3. Start the API gateway subprocess on a dedicated test port.
  4. Wait for /health to respond.
  5. Yield — tests run.
  6. Teardown: stop API gateway, then docker compose down.

Set environment variable SKIP_INFRA=1 to skip fixture startup
(useful when running against an already-running stack).
"""

import os
import signal
import subprocess
import time

import httpx
import pytest

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
API_GATEWAY_DIR = os.path.join(REPO_ROOT, "src", "api-gateway")
COMPOSE_FILE = os.path.join(REPO_ROOT, "deploy", "docker-compose.yml")

# Use a separate port and DB name to avoid colliding with a running dev server
TEST_PORT = int(os.getenv("TEST_API_PORT", "18080"))
TEST_MONGO_DB = "chess_analyzer_test"
TEST_MONGO_URL = os.getenv("TEST_MONGO_URL", "mongodb://localhost:27018")
STARTUP_TIMEOUT = 30  # seconds


@pytest.fixture(scope="session", autouse=True)
def api_server() -> pytest.FixtureRequest:  # type: ignore[type-arg]
    """
    Start infra + API gateway once per test session.
    Sets the API_BASE_URL environment variable for tests that read it.
    """
    skip_infra = os.getenv("SKIP_INFRA", "0") == "1"

    if skip_infra:
        # Tests are expected to point at whatever is already running
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

    # ── 2. Wait for MongoDB ───────────────────────────────────────────────────
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

    print("[conftest] MongoDB ready.", flush=True)

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
        "LOG_LEVEL": "warn",
        "NODE_ENV": "test",
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
