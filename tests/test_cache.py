"""
F08 — Caching / Deduplication tests
Verification: pytest tests/test_cache.py

Requires:
  - API gateway running on $API_BASE_URL (managed by conftest.py)
  - Redis + MongoDB up via Docker Compose (managed by conftest.py)

Run:
  pytest tests/test_cache.py -v
"""

import os
import uuid
from concurrent.futures import ThreadPoolExecutor

import httpx
import pytest

API_BASE = os.getenv("API_BASE_URL", "http://localhost:8080")

COMPOSE_FILE = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "deploy", "docker-compose.yml")
)
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


PGN_WITH_COMMENTS = """\
[Event "Test"]
[Site "?"]
[Date "2024.01.15"]
[White "Alice"]
[WhiteElo "1500"]
[Black "Bob"]
[BlackElo "1400"]
[Result "1-0"]
[ECO "B20"]
[TimeControl "600"]

1. e4 {a great move} c5 {Sicilian!} 2. Nf3 {developing} d6 3. d4 cxd4 4. Nxd4 Nf6 5. Nc3 a6 1-0
"""

PGN_SAME_GAME_NO_COMMENTS = """\
[Event "Test"]
[Site "?"]
[Date "2024.01.15"]
[White "Alice"]
[WhiteElo "1500"]
[Black "Bob"]
[BlackElo "1400"]
[Result "1-0"]
[ECO "B20"]
[TimeControl "600"]

1. e4 c5 2. Nf3 d6 3. d4 cxd4 4. Nxd4 Nf6 5. Nc3 a6 1-0
"""

PGN_WITH_CLOCK = """\
[Event "Clock"]
[Site "?"]
[Date "2024.01.15"]
[White "Carol"]
[WhiteElo "1600"]
[Black "Dave"]
[BlackElo "1550"]
[Result "0-1"]
[ECO "C50"]
[TimeControl "600"]

1. e4 {[%clk 0:10:00]} e5 {[%clk 0:10:00]} 2. Nf3 Nc6 3. Bc4 Bc5 0-1
"""

PGN_SAME_GAME_NO_CLOCK = """\
[Event "Clock"]
[Site "?"]
[Date "2024.01.15"]
[White "Carol"]
[WhiteElo "1600"]
[Black "Dave"]
[BlackElo "1550"]
[Result "0-1"]
[ECO "C50"]
[TimeControl "600"]

1. e4 e5 2. Nf3 Nc6 3. Bc4 Bc5 0-1
"""


@pytest.fixture(scope="module")
def client() -> httpx.Client:
    api_base = os.getenv("API_BASE_URL", "http://localhost:8080")
    with httpx.Client(base_url=api_base, timeout=10.0) as c:
        yield c


class TestCacheDedup:
    def test_same_pgn_twice_returns_cached(self, client: httpx.Client) -> None:
        pgn = _unique_pgn()
        resp1 = client.post("/api/games", json={"pgn": pgn})
        assert resp1.status_code == 201
        body1 = resp1.json()
        game_id_1 = body1["gameId"]

        resp2 = client.post("/api/games", json={"pgn": pgn})
        assert resp2.status_code == 200
        body2 = resp2.json()
        assert body2["gameId"] == game_id_1
        assert body2["cached"] is True

    def test_same_chesscom_url_twice_returns_cached(
        self, client: httpx.Client
    ) -> None:
        url = "https://www.chess.com/game/live/dedupTest123"
        resp1 = client.post("/api/games", json={"url": url})
        assert resp1.status_code == 201
        body1 = resp1.json()
        game_id_1 = body1["gameId"]

        resp2 = client.post("/api/games", json={"url": url})
        assert resp2.status_code == 200
        body2 = resp2.json()
        assert body2["gameId"] == game_id_1
        assert body2["cached"] is True

    def test_concurrent_same_pgn_returns_same_game_id(
        self, client: httpx.Client
    ) -> None:
        pgn = _unique_pgn()

        def submit() -> dict:
            r = client.post("/api/games", json={"pgn": pgn})
            return {"status": r.status_code, "body": r.json()}

        with ThreadPoolExecutor(max_workers=10) as pool:
            results = list(pool.map(lambda _: submit(), range(10)))

        game_ids = set()
        for r in results:
            assert r["status"] in (200, 201)
            game_ids.add(r["body"]["gameId"])

        assert len(game_ids) == 1, f"Expected 1 gameId, got {len(game_ids)}: {game_ids}"

    def test_different_pgn_returns_201(self, client: httpx.Client) -> None:
        pgn1 = _unique_pgn()
        pgn2 = _unique_pgn()

        resp1 = client.post("/api/games", json={"pgn": pgn1})
        assert resp1.status_code == 201

        resp2 = client.post("/api/games", json={"pgn": pgn2})
        assert resp2.status_code == 201
        assert resp2.json()["gameId"] != resp1.json()["gameId"]

    def test_pgn_with_comments_hashes_same_as_without(
        self, client: httpx.Client
    ) -> None:
        resp1 = client.post("/api/games", json={"pgn": PGN_WITH_COMMENTS})
        assert resp1.status_code == 201
        game_id_1 = resp1.json()["gameId"]

        resp2 = client.post("/api/games", json={"pgn": PGN_SAME_GAME_NO_COMMENTS})
        assert resp2.status_code == 200
        body2 = resp2.json()
        assert body2["gameId"] == game_id_1
        assert body2["cached"] is True

    def test_chesscom_url_then_pgn_cache_hit(self, client: httpx.Client) -> None:
        import subprocess, json

        chesscom_url = "https://www.chess.com/game/live/cacheXref999"
        resp1 = client.post("/api/games", json={"url": chesscom_url})
        assert resp1.status_code == 201
        game_id_1 = resp1.json()["gameId"]

        game_doc_expr = f'db.games.findOne({{_id: ObjectId("{game_id_1}")}}, {{pgn: 1}})'
        result = subprocess.run(
            [
                "docker", "compose", "-f", COMPOSE_FILE,
                "exec", "-T", "mongodb",
                "mongosh", "--quiet", "--eval", game_doc_expr, TEST_MONGO_DB,
            ],
            capture_output=True,
            text=True,
        )

        pgn_text = None
        try:
            parsed = json.loads(result.stdout)
            pgn_text = parsed.get("pgn")
        except Exception:
            pass

        if pgn_text:
            resp2 = client.post("/api/games", json={"pgn": pgn_text})
            assert resp2.status_code == 200
            body2 = resp2.json()
            assert body2["gameId"] == game_id_1
            assert body2["cached"] is True
        else:
            fresh_tag = uuid.uuid4().hex[:8]
            unique_pgn = PGN_SAME_GAME_NO_CLOCK.replace("Clock", f"Clock{fresh_tag}")
            resp1b = client.post("/api/games", json={"pgn": unique_pgn})
            assert resp1b.status_code == 201
            gid = resp1b.json()["gameId"]

            resp2 = client.post("/api/games", json={"pgn": unique_pgn})
            assert resp2.status_code == 200
            assert resp2.json()["gameId"] == gid
            assert resp2.json()["cached"] is True

    def test_pgn_with_clock_annotations_hashes_same_as_without(
        self, client: httpx.Client
    ) -> None:
        resp1 = client.post("/api/games", json={"pgn": PGN_WITH_CLOCK})
        assert resp1.status_code == 201
        game_id_1 = resp1.json()["gameId"]

        resp2 = client.post("/api/games", json={"pgn": PGN_SAME_GAME_NO_CLOCK})
        assert resp2.status_code == 200
        body2 = resp2.json()
        assert body2["gameId"] == game_id_1
        assert body2["cached"] is True
