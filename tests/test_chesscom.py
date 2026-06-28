import pytest
import httpx
from pymongo import MongoClient

API_URL = "http://localhost:18080"
MONGO_URL = "mongodb://localhost:27017"

@pytest.fixture(scope="session")
def db():
    client = MongoClient(MONGO_URL)
    db = client.chess_analyzer
    yield db
    db.games.delete_many({})
    client.close()

@pytest.mark.asyncio
async def test_valid_chesscom_live_url(db):
    async with httpx.AsyncClient() as client:
        # Use a mock ID since real API returns 404 for many games
        response = await client.post(f"{API_URL}/api/games", json={"url": "https://www.chess.com/game/live/test-valid-mock"})
    
    assert response.status_code == 201
    data = response.json()
    assert "gameId" in data
    
    # We find the specific game we just inserted
    game = db.games.find_one({"chesscomGameId": "test-valid-mock"})
    assert game is not None
    assert game["source"] == "chesscom_url"
    assert game["chesscomGameId"] == "test-valid-mock"
    assert game["pgn"] is not None

@pytest.mark.asyncio
async def test_valid_chesscom_review_url(db):
    # Clear any previous document from other tests to ensure a 201 Created response
    db.games.delete_many({"chesscomGameId": "test-valid-mock"})
    async with httpx.AsyncClient() as client:
        # Use a mock ID since real API returns 404 for many games
        response = await client.post(f"{API_URL}/api/games", json={"url": "https://www.chess.com/analysis/game/live/test-valid-mock/review"})
    
    assert response.status_code == 201
    data = response.json()
    assert "gameId" in data
    
    # We find the specific game we just inserted
    game = db.games.find_one({"chesscomGameId": "test-valid-mock"})
    assert game is not None
    assert game["source"] == "chesscom_url"
    assert game["chesscomGameId"] == "test-valid-mock"
    assert game["pgn"] is not None

@pytest.mark.asyncio
async def test_non_chesscom_url():
    async with httpx.AsyncClient() as client:
        response = await client.post(f"{API_URL}/api/games", json={"url": "https://lichess.org/12345"})
    assert response.status_code == 400
    assert "not a chess.com game URL" in response.text

@pytest.mark.asyncio
async def test_unknown_gameid_url():
    async with httpx.AsyncClient() as client:
        response = await client.post(f"{API_URL}/api/games", json={"url": "https://www.chess.com/game/live/999999999999999999"})
    assert response.status_code == 400
    assert "game not found" in response.text

@pytest.mark.asyncio
async def test_chesscom_timeout():
    async with httpx.AsyncClient() as client:
        response = await client.post(f"{API_URL}/api/games", json={"url": "https://www.chess.com/game/live/test-timeout-mock"})
    assert response.status_code == 502
    assert "chess.com API unavailable" in response.text

@pytest.mark.asyncio
async def test_ambiguous_input():
    async with httpx.AsyncClient() as client:
        response = await client.post(f"{API_URL}/api/games", json={"url": "https://www.chess.com/game/live/test-valid-mock", "pgn": "1. e4"})
    assert response.status_code == 400
    assert "cannot provide both pgn and url" in response.text

@pytest.mark.asyncio
async def test_neither_input():
    async with httpx.AsyncClient() as client:
        response = await client.post(f"{API_URL}/api/games", json={})
    assert response.status_code == 400
    assert "pgn or url is required" in response.text

@pytest.mark.asyncio
async def test_valid_chesscom_daily_url():
    # Test regex accepts it, but expect 404 response since we use a fake ID
    async with httpx.AsyncClient() as client:
        response = await client.post(f"{API_URL}/api/games", json={"url": "https://www.chess.com/game/daily/999999999999999999"})
    assert response.status_code == 400
    assert "game not found" in response.text

@pytest.mark.asyncio
async def test_scan_player_success_page_1():
    async with httpx.AsyncClient() as client:
        response = await client.get(f"{API_URL}/api/chesscom/players/test-user-mock/games?page=1&limit=10")
    assert response.status_code == 200
    data = response.json()
    assert "games" in data
    assert "hasMore" in data
    assert len(data["games"]) == 10
    assert data["hasMore"] is True
    g = data["games"][0]
    assert "url" in g
    assert "uuid" in g
    assert "pgn" in g
    assert "white" in g
    assert "black" in g
    assert "timeControl" in g
    assert "timeClass" in g
    assert "rules" in g
    assert "playedAt" in g

@pytest.mark.asyncio
async def test_scan_player_success_page_2():
    async with httpx.AsyncClient() as client:
        response = await client.get(f"{API_URL}/api/chesscom/players/test-user-mock/games?page=2&limit=15")
    assert response.status_code == 200
    data = response.json()
    assert "games" in data
    assert "hasMore" in data
    assert len(data["games"]) == 10
    assert data["hasMore"] is False

@pytest.mark.asyncio
async def test_scan_player_not_found():
    async with httpx.AsyncClient() as client:
        response = await client.get(f"{API_URL}/api/chesscom/players/test-notfound-mock/games")
    assert response.status_code == 404
    assert "player not found" in response.text

@pytest.mark.asyncio
async def test_scan_player_timeout():
    async with httpx.AsyncClient() as client:
        response = await client.get(f"{API_URL}/api/chesscom/players/test-timeout-mock/games")
    assert response.status_code == 502
    assert "chess.com API unavailable" in response.text
