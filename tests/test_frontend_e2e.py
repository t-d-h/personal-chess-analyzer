"""
test_frontend_e2e.py — Playwright E2E tests for the React frontend (F09).

Requires:
  - Frontend dev server running (started by the frontend_server fixture)
  - API gateway + Redis + MongoDB running (from conftest session fixture)

The tests seed MongoDB with completed analysis data directly (same approach
as test_api.py) because the analyze-service consumer (F06) may not be
running yet.

Run:
  pytest tests/test_frontend_e2e.py -v
"""

import json
import os
import subprocess
import time
import uuid
from typing import Generator

import httpx
import pytest

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
FRONTEND_DIR = os.path.join(REPO_ROOT, "src", "frontend")
COMPOSE_FILE = os.path.join(REPO_ROOT, "deploy", "docker-compose.yml")

API_BASE = os.getenv("API_BASE_URL", "http://localhost:8080")
TEST_MONGO_DB = os.getenv("MONGO_DB", "chess_analyzer_test")
FRONTEND_PORT = int(os.getenv("TEST_FRONTEND_PORT", "3100"))


def _unique_pgn() -> str:
    tag = uuid.uuid4().hex[:8]
    return (
        f'[Event "T{tag}"]\n[Site "?"]\n[Date "2024.06.15"]\n'
        f'[White "W{tag}"]\n[WhiteElo "1800"]\n'
        f'[Black "B{tag}"]\n[BlackElo "1750"]\n'
        f'[Result "1-0"]\n[ECO "B20"]\n[TimeControl "300"]\n\n'
        f"1. e4 c5 2. Nf3 d6 1-0"
    )


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


def _redis_cli(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        ["docker", "compose", "-f", COMPOSE_FILE, "exec", "-T", "redis", "redis-cli", *args],
        capture_output=True,
        text=True,
    )


def _mongo_update(game_id: str, fields: dict) -> None:
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


COMPLETED_MOVES = [
    {"ply": 1, "color": "white", "san": "e4", "fenBefore": "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "fenAfter": "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1", "evalCpPlayed": 25, "evalCpBest": 25, "evalMate": None, "bestMoveSan": "e4", "winPercentLoss": 0, "moveAccuracy": 100, "classification": "book", "engineDepth": 18},
    {"ply": 2, "color": "black", "san": "c5", "fenBefore": "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1", "fenAfter": "rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq c6 0 2", "evalCpPlayed": 20, "evalCpBest": 25, "evalMate": None, "bestMoveSan": "c5", "winPercentLoss": 1.2, "moveAccuracy": 97.8, "classification": "excellent", "engineDepth": 18},
    {"ply": 3, "color": "white", "san": "Nf3", "fenBefore": "rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq c6 0 2", "fenAfter": "rnbqkbnr/pp1ppppp/8/2p5/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 1 2", "evalCpPlayed": 30, "evalCpBest": 30, "evalMate": None, "bestMoveSan": "Nf3", "winPercentLoss": 0, "moveAccuracy": 100, "classification": "best", "engineDepth": 18},
    {"ply": 4, "color": "black", "san": "d6", "fenBefore": "rnbqkbnr/pp1ppppp/8/2p5/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 1 2", "fenAfter": "rnbqkbnr/pp2pppp/3p4/2p5/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 0 3", "evalCpPlayed": 15, "evalCpBest": 30, "evalMate": None, "bestMoveSan": "e6", "winPercentLoss": 3.5, "moveAccuracy": 94.2, "classification": "good", "engineDepth": 18},
]

COMPLETED_SUMMARIES = [
    {"color": "white", "accuracyPct": 98.5, "acpl": 0, "bestCount": 1, "excellentCount": 1, "goodCount": 0, "inaccuracyCount": 0, "mistakeCount": 0, "blunderCount": 0, "brilliantCount": 0},
    {"color": "black", "accuracyPct": 96.0, "acpl": 2.5, "bestCount": 0, "excellentCount": 1, "goodCount": 1, "inaccuracyCount": 0, "mistakeCount": 0, "blunderCount": 0, "brilliantCount": 0},
]


def _seed_completed_game(client: httpx.Client) -> str:
    resp = client.post("/api/games", json={"pgn": _unique_pgn()})
    assert resp.status_code == 201
    game_id = resp.json()["gameId"]

    _mongo_update(game_id, {
        "analysis.status": "completed",
        "analysis.movesAnalyzed": 4,
        "analysis.movesTotal": 4,
        "analysis.completedAt": "2024-06-15T12:00:00Z",
        "analysis.moves": COMPLETED_MOVES,
        "analysis.playerSummaries": COMPLETED_SUMMARIES,
    })

    _redis_cli("HSET", f"job:{game_id}:progress", "status", "completed", "movesAnalyzed", "4", "movesTotal", "4")
    _redis_cli("EXPIRE", f"job:{game_id}:progress", "300")

    return game_id


@pytest.fixture(scope="module")
def client(api_server: str) -> Generator[httpx.Client, None, None]:
    with httpx.Client(base_url=api_server, timeout=10.0) as c:
        yield c


@pytest.fixture(scope="module")
def frontend_server(api_server: str):  # type: ignore[type-arg]
    env = {
        **os.environ,
        "VITE_API_BASE_URL": api_server,
    }
    proc = subprocess.Popen(
        ["npx", "vite", "--port", str(FRONTEND_PORT), "--strictPort"],
        cwd=FRONTEND_DIR,
        env=env,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )

    base = f"http://localhost:{FRONTEND_PORT}"
    deadline = time.time() + 20
    while time.time() < deadline:
        try:
            r = httpx.get(base, timeout=2, follow_redirects=True)
            if r.status_code == 200:
                break
        except Exception:
            pass
        time.sleep(0.5)
    else:
        proc.terminate()
        pytest.skip("Frontend dev server did not start in time")

    yield base

    proc.terminate()
    try:
        proc.wait(timeout=5)
    except Exception:
        proc.kill()


@pytest.fixture(autouse=True)
def _clean():
    _clean_games_collection()
    yield


pytest_plugins = ["pytest_playwright"]


class TestFrontendE2E:
    def test_home_page_has_input_form(self, frontend_server: str, page):
        page.goto(frontend_server)
        textarea = page.query_selector("textarea")
        button = page.query_selector('button[type="submit"]')
        assert textarea is not None, "PGN textarea not found on home page"
        assert button is not None, "Analyze button not found on home page"

    def test_submit_valid_pgn_shows_progress(
        self, frontend_server: str, page, client: httpx.Client
    ):
        page.goto(frontend_server)
        pgn = _unique_pgn()
        textarea = page.query_selector("textarea")
        textarea.fill(pgn)
        button = page.query_selector('button[type="submit"]')
        button.click()
        progress = page.wait_for_selector(".progress-track", timeout=10000)
        assert progress is not None, "Progress bar did not appear after PGN submission"

    def test_analysis_page_renders_chessboard(
        self, frontend_server: str, page, client: httpx.Client
    ):
        game_id = _seed_completed_game(client)
        page.goto(f"{frontend_server}/analysis/{game_id}")
        board = page.wait_for_selector(".chessboard-wrapper", timeout=10000)
        assert board is not None, "Chessboard did not render on analysis page"

    def test_move_navigation_updates_board(
        self, frontend_server: str, page, client: httpx.Client
    ):
        game_id = _seed_completed_game(client)
        page.goto(f"{frontend_server}/analysis/{game_id}")
        page.wait_for_selector(".chessboard-wrapper", timeout=10000)
        next_btn = page.wait_for_selector('button[aria-label="Next move"]', timeout=5000)
        next_btn.click()
        ply_label = page.query_selector(".ply-label")
        assert ply_label is not None, "Ply label not found after clicking next"

    def test_eval_graph_renders(
        self, frontend_server: str, page, client: httpx.Client
    ):
        game_id = _seed_completed_game(client)
        page.goto(f"{frontend_server}/analysis/{game_id}")
        svg = page.wait_for_selector(".eval-graph-container svg", timeout=10000)
        assert svg is not None, "Eval graph SVG did not render"

    def test_player_accuracy_visible(
        self, frontend_server: str, page, client: httpx.Client
    ):
        game_id = _seed_completed_game(client)
        page.goto(f"{frontend_server}/analysis/{game_id}")
        accuracy = page.wait_for_selector(".accuracy", timeout=10000)
        assert accuracy is not None, "Player accuracy element not found"
        text = accuracy.inner_text()
        assert "%" in text, f"Expected accuracy percentage, got: {text}"

    def test_submit_chesscom_url_shows_progress(
        self, frontend_server: str, page, client: httpx.Client
    ):
        page.goto(frontend_server)
        url = "https://chess.com/game/live/e2e-url-test"
        textarea = page.query_selector("textarea")
        textarea.fill(url)
        button = page.query_selector('button[type="submit"]')
        button.click()
        progress = page.wait_for_selector(".progress-track", timeout=10000)
        assert progress is not None, "Progress bar did not appear after chess.com URL submission"
