"""
F10 — Hardening tests
Verification: pytest tests/test_hardening.py

Covers:
  1. API gateway rate limiting on POST /api/games
  2. PGN size limits (512KB body, 300 plies)
  3. Per-move engine timeout (5s) with auto-restart
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

API_BASE = os.getenv("API_BASE_URL", "http://localhost:8080")
REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
API_GATEWAY_DIR = os.path.join(REPO_ROOT, "src", "api-gateway")
ANALYZE_SERVICE_DIR = os.path.join(REPO_ROOT, "src", "analyze-service")


def _unique_pgn() -> str:
    tag = uuid.uuid4().hex[:8]
    return (
        f'[Event "T{tag}"]\n[Site "?"]\n[Date "2024.06.15"]\n'
        f'[White "W{tag}"]\n[WhiteElo "1800"]\n'
        f'[Black "B{tag}"]\n[BlackElo "1750"]\n'
        f'[Result "1-0"]\n[ECO "B20"]\n[TimeControl "300"]\n\n'
        f"1. e4 c5 2. Nf3 d6 1-0"
    )


def _make_long_pgn(num_full_moves: int) -> str:
    header = (
        '[Event "Long"]\n[White "?"]\n[Black "?"]\n[Result "*"]\n\n'
    )
    white_moves = ["Nf3", "Ng1"]
    black_moves = ["Nf6", "Ng8"]
    move_text = []
    for i in range(num_full_moves):
        move_num = i + 1
        w = white_moves[i % 2]
        b = black_moves[i % 2]
        move_text.append(f"{move_num}. {w} {b}")
    return header + " ".join(move_text) + " *"


@pytest.fixture(scope="module")
def client() -> Generator[httpx.Client, None, None]:
    api_base = os.getenv("API_BASE_URL", "http://localhost:8080")
    with httpx.Client(base_url=api_base, timeout=10.0) as c:
        yield c


# ─── Rate limiting tests ──────────────────────────────────────────────────


class TestRateLimit:
    @pytest.fixture(autouse=True)
    def _rate_limit_server(self):
        test_port = int(os.getenv("TEST_API_PORT", "18080")) + 1
        rate_limit_url = f"http://localhost:{test_port}"

        env = {
            **os.environ,
            "PORT": str(test_port),
            "MONGO_URL": os.getenv("TEST_MONGO_URL", "mongodb://localhost:27018/chess_analyzer_test"),
            "MONGO_DB": os.getenv("MONGO_DB", "chess_analyzer_test"),
            "REDIS_URL": os.getenv("TEST_REDIS_URL", "redis://localhost:6379"),
            "LOG_LEVEL": "warn",
            "TRUST_PROXY": "1",
            "RATE_LIMIT_MAX": "5",
            "RATE_LIMIT_WINDOW": "30000",
        }

        proc = subprocess.Popen(
            ["node", "dist/index.js"],
            cwd=API_GATEWAY_DIR,
            env=env,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            preexec_fn=os.setsid,
        )

        deadline = time.time() + 30
        while time.time() < deadline:
            try:
                r = httpx.get(f"{rate_limit_url}/health", timeout=2)
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
            pytest.fail("Rate limit test server did not start")

        os.environ["RATE_LIMIT_API_URL"] = rate_limit_url
        yield rate_limit_url

        try:
            os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
            proc.wait(timeout=5)
        except Exception:
            pass

    def test_rate_limit_headers_present(self) -> None:
        url = os.environ["RATE_LIMIT_API_URL"]
        resp = httpx.post(f"{url}/api/games", json={"pgn": _unique_pgn()}, timeout=10.0)
        assert resp.status_code in (200, 201)
        assert "x-ratelimit-limit" in resp.headers
        assert "x-ratelimit-remaining" in resp.headers

    def test_exceeding_rate_limit_returns_429(self) -> None:
        url = os.environ["RATE_LIMIT_API_URL"]
        for i in range(20):
            resp = httpx.post(f"{url}/api/games", json={"pgn": _unique_pgn()}, timeout=10.0)
            if resp.status_code == 429:
                break
        else:
            pytest.fail("Expected 429 after exceeding limit, but all 20 requests succeeded")

        assert resp.status_code == 429


# ─── Input size guard tests ──────────────────────────────────────────────


class TestInputSizeLimits:
    def test_pgn_over_512kb_returns_400(self, client: httpx.Client) -> None:
        big_pgn = "x" * (513 * 1024)
        resp = client.post("/api/games", json={"pgn": big_pgn})
        assert resp.status_code == 400
        body = resp.json()
        assert "too large" in body.get("error", "").lower() or "PGN" in body.get("error", "")

    def test_301_ply_game_returns_400(self, client: httpx.Client) -> None:
        long_pgn = _make_long_pgn(151)
        resp = client.post("/api/games", json={"pgn": long_pgn})
        assert resp.status_code == 400
        body = resp.json()
        assert "too long" in body.get("error", "")

    def test_300_ply_game_returns_201(self, client: httpx.Client) -> None:
        pgn_150 = _make_long_pgn(150)
        resp = client.post("/api/games", json={"pgn": pgn_150})
        assert resp.status_code in (200, 201)


# ─── Engine timeout tests ────────────────────────────────────────────────


class TestEngineTimeout:
    @staticmethod
    def _write_mock_stockfish_hang(path: str, hang_marker: str) -> None:
        script = f"""#!/bin/sh
while IFS= read -r line; do
    case "$line" in
        uci)   echo "id name mock"; echo "uciok";;
        isready) echo "readyok";;
        ucinewgame) ;;
        position*) ;;
        go*)
            if [ -f "{hang_marker}" ]; then
                rm -f "{hang_marker}"
                sleep 9999
            else
                echo "info depth 18 score cp 50 pv e2e4"
                echo "bestmove e2e4"
            fi
            ;;
        quit) exit 0 ;;
    esac
done
"""
        with open(path, "w") as f:
            f.write(script)
        os.chmod(path, 0o755)

    def test_per_move_timeout_records_null_eval(self) -> None:
        mock_path = "/tmp/mock_stockfish_hang.sh"
        hang_marker = "/tmp/mock_stockfish_hang_marker"
        with open(hang_marker, "w") as f:
            f.write("1")
        self._write_mock_stockfish_hang(mock_path, hang_marker)

        fens = (
            "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1\n"
            "rnbqkbnr/pppppppp/8/8/3P4/8/PPP1PPPP/RNBQKBNR b KQkq - 0 1\n"
        )
        env = {
            **os.environ,
            "STOCKFISH_PATH": mock_path,
            "MOVE_TIMEOUT_MS": "500",
        }

        proc = subprocess.run(
            [
                os.path.join(ANALYZE_SERVICE_DIR, "bin", "analyze-game"),
            ],
            input=fens,
            capture_output=True,
            text=True,
            timeout=10,
            env=env,
            cwd=REPO_ROOT,
        )

        assert proc.returncode == 0, f"analyze-game failed: {proc.stderr}"
        output = json.loads(proc.stdout)
        assert len(output["moves"]) == 1
        move = output["moves"][0]
        assert move["evalCpPlayed"] is None
        assert move["evalCpBest"] is None
        assert move["engineDepth"] == 0

        for p in [mock_path, hang_marker]:
            if os.path.exists(p):
                os.unlink(p)

    def test_per_move_timeout_continues_after_restart(self) -> None:
        mock_path = "/tmp/mock_stockfish_hang_first.sh"
        hang_marker = "/tmp/mock_stockfish_hang_first_marker"
        with open(hang_marker, "w") as f:
            f.write("1")
        self._write_mock_stockfish_hang(mock_path, hang_marker)

        fens = (
            "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1\n"
            "rnbqkbnr/pppppppp/8/8/3P4/8/PPP1PPPP/RNBQKBNR b KQkq - 0 1\n"
            "rnbqkbnr/pppppppp/8/8/3P4/8/PPP1PPPP/RNBQKBNR b KQkq - 0 1\n"
        )
        env = {
            **os.environ,
            "STOCKFISH_PATH": mock_path,
            "MOVE_TIMEOUT_MS": "500",
        }

        proc = subprocess.run(
            [
                os.path.join(ANALYZE_SERVICE_DIR, "bin", "analyze-game"),
            ],
            input=fens,
            capture_output=True,
            text=True,
            timeout=10,
            env=env,
            cwd=REPO_ROOT,
        )

        assert proc.returncode == 0, f"analyze-game failed: {proc.stderr}"
        output = json.loads(proc.stdout)
        assert len(output["moves"]) == 2
        assert output["moves"][0]["evalCpPlayed"] is None
        assert output["moves"][1]["evalCpPlayed"] is not None

        for p in [mock_path, hang_marker]:
            if os.path.exists(p):
                os.unlink(p)
