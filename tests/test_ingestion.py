"""
F01 — PGN Ingestion tests
Verification: pytest tests/test_ingestion.py

Requires:
  - API gateway running on $API_BASE_URL (default http://localhost:8080)
  - MongoDB accessible to the gateway

Run against a live server:
  docker compose -f deploy/docker-compose.yml up -d redis mongodb api-gateway
  pytest tests/test_ingestion.py -v

Or with a pytest-managed subprocess (via conftest.py fixture).
"""

import os
import pytest
import httpx

API_BASE = os.getenv("API_BASE_URL", "http://localhost:8080")

# ─── Sample PGN fixtures ──────────────────────────────────────────────────────

SHORT_GAME_PGN = """\
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

MINIMAL_PGN = """\
[Event "?"]
[White "?"]
[Black "?"]
[Result "*"]

1. e4 e5 *
"""


def _make_long_pgn(num_full_moves: int) -> str:
    """
    Generate a valid PGN with exactly num_full_moves * 2 plies.

    Uses a provably legal repeating pattern:
      White: alternates Nf3 / Ng1
      Black: alternates Nf6 / Ng8
    All moves are legal from the starting position; the knights just shuffle.
    """
    header = (
        "[Event \"Long\"]\n[White \"?\"]\n[Black \"?\"]\n[Result \"*\"]\n\n"
    )
    # White move patterns (Ng1<->f3), Black move patterns (Ng8<->f6)
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
def client() -> httpx.Client:
    with httpx.Client(base_url=API_BASE, timeout=10.0) as c:
        yield c


# ─── Tests ────────────────────────────────────────────────────────────────────


class TestPostGames:
    def test_valid_pgn_returns_201_with_game_id(self, client: httpx.Client) -> None:
        resp = client.post("/api/games", json={"pgn": SHORT_GAME_PGN})
        assert resp.status_code == 201
        body = resp.json()
        assert "gameId" in body
        assert "jobId" in body
        assert body["status"] == "queued"
        assert len(body["gameId"]) == 24  # MongoDB ObjectId hex

    def test_game_id_equals_job_id(self, client: httpx.Client) -> None:
        resp = client.post("/api/games", json={"pgn": SHORT_GAME_PGN})
        assert resp.status_code == 201
        body = resp.json()
        assert body["gameId"] == body["jobId"]

    def test_minimal_pgn_returns_201(self, client: httpx.Client) -> None:
        resp = client.post("/api/games", json={"pgn": MINIMAL_PGN})
        assert resp.status_code == 201

    def test_empty_body_returns_400(self, client: httpx.Client) -> None:
        resp = client.post("/api/games", json={})
        assert resp.status_code == 400
        assert "error" in resp.json()

    def test_missing_pgn_field_returns_400(self, client: httpx.Client) -> None:
        resp = client.post("/api/games", json={"not_pgn": "something"})
        assert resp.status_code == 400

    def test_garbage_pgn_returns_400(self, client: httpx.Client) -> None:
        resp = client.post("/api/games", json={"pgn": "this is not a chess game !!!"})
        assert resp.status_code == 400
        body = resp.json()
        assert "invalid PGN" in body.get("error", "")

    def test_empty_string_pgn_returns_400(self, client: httpx.Client) -> None:
        resp = client.post("/api/games", json={"pgn": ""})
        assert resp.status_code == 400

    def test_valid_pgn_submitted_twice_returns_201_both_times(
        self, client: httpx.Client
    ) -> None:
        """Dedup is F08 — for now both insertions should succeed."""
        resp1 = client.post("/api/games", json={"pgn": MINIMAL_PGN})
        resp2 = client.post("/api/games", json={"pgn": MINIMAL_PGN})
        # Both should succeed (or at worst both 201) — no dedup yet
        assert resp1.status_code == 201
        assert resp2.status_code == 201

    def test_301_ply_game_returns_400(self, client: httpx.Client) -> None:
        long_pgn = _make_long_pgn(151)  # 151 full moves = 302 plies > 300
        resp = client.post("/api/games", json={"pgn": long_pgn})
        assert resp.status_code == 400
        body = resp.json()
        assert "too long" in body.get("error", "")

    def test_300_ply_game_returns_201(self, client: httpx.Client) -> None:
        pgn_150 = _make_long_pgn(150)  # 150 full moves = 300 plies
        resp = client.post("/api/games", json={"pgn": pgn_150})
        assert resp.status_code == 201

    def test_metadata_parsed_correctly(self, client: httpx.Client) -> None:
        """Metadata verification via GET /api/games/:id — added in F07.
        For now just confirm 201 and gameId are present."""
        resp = client.post("/api/games", json={"pgn": SHORT_GAME_PGN})
        assert resp.status_code == 201
        assert "gameId" in resp.json()
