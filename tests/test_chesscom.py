"""
F02 — Chess.com URL Ingestion tests
Verification: pytest tests/test_chesscom.py

Requires:
  - API gateway running on $API_BASE_URL (managed by conftest.py)
  - Mock chess.com API server (started by conftest, CHESSCOM_API_BASE passed to gateway)
  - MongoDB accessible to the gateway

Run:
  pytest tests/test_chesscom.py -v
"""

import os

import httpx
import pytest

API_BASE = os.getenv("API_BASE_URL", "http://localhost:8080")


@pytest.fixture(scope="module")
def client() -> httpx.Client:
    api_base = os.getenv("API_BASE_URL", "http://localhost:8080")
    with httpx.Client(base_url=api_base, timeout=10.0) as c:
        yield c


class TestChesscomUrlIngestion:
    def test_valid_chesscom_live_url_returns_201(
        self, client: httpx.Client
    ) -> None:
        resp = client.post(
            "/api/games",
            json={"url": "https://www.chess.com/game/live/12345"},
        )
        assert resp.status_code == 201, resp.text
        body = resp.json()
        assert "gameId" in body
        assert body["status"] == "queued"

    def test_valid_chesscom_daily_url_returns_201(
        self, client: httpx.Client
    ) -> None:
        resp = client.post(
            "/api/games",
            json={"url": "https://www.chess.com/game/daily/67890"},
        )
        assert resp.status_code == 201, resp.text

    def test_chesscom_analysis_url_returns_201(
        self, client: httpx.Client
    ) -> None:
        resp = client.post(
            "/api/games",
            json={"url": "https://www.chess.com/analysis/game/live/123abc"},
        )
        assert resp.status_code == 201, resp.text

    def test_non_chesscom_url_returns_400(self, client: httpx.Client) -> None:
        resp = client.post(
            "/api/games",
            json={"url": "https://lichess.org/abc123"},
        )
        assert resp.status_code == 400
        assert "not a chess.com game URL" in resp.json().get("error", "")

    def test_chesscom_game_not_found_returns_400(
        self, client: httpx.Client
    ) -> None:
        resp = client.post(
            "/api/games",
            json={"url": "https://www.chess.com/game/live/notfound"},
        )
        assert resp.status_code == 400
        assert "not found" in resp.json().get("error", "").lower()

    def test_both_pgn_and_url_returns_400(self, client: httpx.Client) -> None:
        resp = client.post(
            "/api/games",
            json={"pgn": "1. e4 e5", "url": "https://www.chess.com/game/live/12345"},
        )
        assert resp.status_code == 400
        assert "both" in resp.json().get("error", "").lower()

    def test_neither_pgn_nor_url_returns_400(self, client: httpx.Client) -> None:
        resp = client.post("/api/games", json={})
        assert resp.status_code == 400

    def test_pgn_still_works_after_f02(
        self, client: httpx.Client
    ) -> None:
        from test_ingestion import SHORT_GAME_PGN

        resp = client.post("/api/games", json={"pgn": SHORT_GAME_PGN})
        assert resp.status_code == 201
