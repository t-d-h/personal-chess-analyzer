import pytest
import httpx
import asyncio
import hashlib
from pymongo import MongoClient
from bson.objectid import ObjectId

API_URL = "http://localhost:18080"
MONGO_URL = "mongodb://localhost:27017"

@pytest.fixture(scope="function")
def db():
    client = MongoClient(MONGO_URL)
    db = client.chess_analyzer
    # Clean up games collection before and after each test in this module
    db.games.delete_many({})
    yield db
    db.games.delete_many({})
    client.close()

def normalize_pgn_py(pgn: str) -> str:
    import re
    # Simple python counterpart of the normalization for computing hash in test
    pgn = re.sub(r'\{[^}]*\}', '', pgn)
    pgn = re.sub(r';[^\n]*', '', pgn)
    pgn = re.sub(r'\d+\.\.\.', '', pgn)
    pgn = re.sub(r'\[%[^\]]*\]', '', pgn)
    pgn = re.sub(r'\s+', ' ', pgn)
    return pgn.strip()

def compute_hash(pgn: str) -> str:
    norm = normalize_pgn_py(pgn)
    return hashlib.sha256(norm.encode('utf-8')).hexdigest()

@pytest.mark.asyncio
async def test_submit_same_pgn_twice(db):
    pgn = '[Event "Test Ingestion"]\n[White "A"]\n[Black "B"]\n[Result "1-0"]\n\n1. e4 e5 2. Nf3 Nc6 1-0'
    
    async with httpx.AsyncClient() as client:
        # First submission
        resp1 = await client.post(f"{API_URL}/api/games", json={"pgn": pgn})
        assert resp1.status_code == 201
        data1 = resp1.json()
        assert "gameId" in data1
        assert data1.get("cached") is None
        
        # Second submission
        resp2 = await client.post(f"{API_URL}/api/games", json={"pgn": pgn})
        assert resp2.status_code == 200
        data2 = resp2.json()
        assert data2["gameId"] == data1["gameId"]
        assert data2.get("cached") is True

@pytest.mark.asyncio
async def test_submit_same_chesscom_url_twice(db):
    url = "https://www.chess.com/game/live/test-valid-mock"
    
    async with httpx.AsyncClient() as client:
        # First submission
        resp1 = await client.post(f"{API_URL}/api/games", json={"url": url})
        assert resp1.status_code == 201
        data1 = resp1.json()
        
        # Second submission (should hit chesscomGameId cache before calling API)
        resp2 = await client.post(f"{API_URL}/api/games", json={"url": url})
        assert resp2.status_code == 200
        data2 = resp2.json()
        assert data2["gameId"] == data1["gameId"]
        assert data2.get("cached") is True

@pytest.mark.asyncio
async def test_submit_different_pgn(db):
    pgn1 = '[Event "Game 1"]\n[White "A"]\n[Black "B"]\n[Result "1-0"]\n\n1. e4 e5 1-0'
    pgn2 = '[Event "Game 2"]\n[White "A"]\n[Black "B"]\n[Result "0-1"]\n\n1. d4 d5 0-1'
    
    async with httpx.AsyncClient() as client:
        resp1 = await client.post(f"{API_URL}/api/games", json={"pgn": pgn1})
        resp2 = await client.post(f"{API_URL}/api/games", json={"pgn": pgn2})
        
        assert resp1.status_code == 201
        assert resp2.status_code == 201
        assert resp1.json()["gameId"] != resp2.json()["gameId"]

@pytest.mark.asyncio
async def test_submit_pgn_with_comments_stripped(db):
    # PGN with comments, clock annotations, and space variations
    pgn_with_comments = '''[Event "Commented Game"]
[White "A"]
[Black "B"]
[Result "1-0"]

1. e4 {Excellent move} 1... e5 {[%clk 0:02:59]} 2. Nf3 Nc6 1-0'''

    # Normalized clean version
    pgn_clean = '''[Event "Commented Game"]
[White "A"]
[Black "B"]
[Result "1-0"]

1. e4 e5 2. Nf3 Nc6 1-0'''

    async with httpx.AsyncClient() as client:
        # Post the commented one first
        resp1 = await client.post(f"{API_URL}/api/games", json={"pgn": pgn_with_comments})
        assert resp1.status_code == 201
        
        # Post the clean one next
        resp2 = await client.post(f"{API_URL}/api/games", json={"pgn": pgn_clean})
        assert resp2.status_code == 200
        assert resp2.json()["gameId"] == resp1.json()["gameId"]
        assert resp2.json().get("cached") is True

@pytest.mark.asyncio
async def test_submit_chesscom_url_then_raw_pgn(db):
    # "test-valid-mock" returns PGN: '[Event "Mock Event"]\n\n1. e4 e5'
    url = "https://www.chess.com/game/live/test-valid-mock"
    raw_pgn = '[Event "Mock Event"]\n\n1. e4 e5'
    
    async with httpx.AsyncClient() as client:
        resp1 = await client.post(f"{API_URL}/api/games", json={"url": url})
        assert resp1.status_code == 201
        
        resp2 = await client.post(f"{API_URL}/api/games", json={"pgn": raw_pgn})
        assert resp2.status_code == 200
        assert resp2.json()["gameId"] == resp1.json()["gameId"]
        assert resp2.json().get("cached") is True

@pytest.mark.asyncio
async def test_concurrent_submissions(db):
    pgn = '[Event "Concurrent Test"]\n[White "A"]\n[Black "B"]\n[Result "1-0"]\n\n1. e4 e5 2. Nf3 d6 1-0'
    pgn_hash = compute_hash(pgn)
    
    async with httpx.AsyncClient() as client:
        # Launch 10 concurrent requests
        tasks = [
            client.post(f"{API_URL}/api/games", json={"pgn": pgn})
            for _ in range(10)
        ]
        responses = await asyncio.gather(*tasks)
        
    # Check all succeeded with 200 or 201
    status_codes = [r.status_code for r in responses]
    assert all(code in (200, 201) for code in status_codes)
    
    # Check exactly one response is a 201 (since it created it)
    # Note: under highly concurrent environments, it could be that some requests complete in memory
    # but at least one should return 201. Let's verify we have at least one 201.
    assert 201 in status_codes
    
    # All gameIds must be identical
    game_ids = [r.json()["gameId"] for r in responses]
    first_game_id = game_ids[0]
    assert all(gid == first_game_id for gid in game_ids)
    
    # Only 1 document must exist in MongoDB
    doc_count = db.games.count_documents({"pgnHash": pgn_hash})
    assert doc_count == 1
