import pytest
import redis
import pymongo
import httpx
import os

REDIS_URL = os.environ.get("REDIS_URL", "redis://localhost:6379")
MONGO_URL = os.environ.get("MONGO_URL", "mongodb://localhost:27017")
API_URL = os.environ.get("API_URL", "http://localhost:18080")

@pytest.fixture
def redis_client():
    client = redis.Redis.from_url(REDIS_URL, decode_responses=True)
    yield client
    client.close()

@pytest.fixture
def mongo_client():
    client = pymongo.MongoClient(MONGO_URL)
    yield client
    client.close()

def test_redis_ping(redis_client):
    assert redis_client.ping() is True

def test_mongo_ping(mongo_client):
    db = mongo_client.admin
    result = db.command("ping")
    assert result.get("ok") == 1.0

def test_mongo_indexes(mongo_client):
    db = mongo_client.chess_analyzer
    indexes = list(db.games.list_indexes())
    
    # We expect _id_, pgnHash_1, chesscomGameId_1, analysis.status_1
    index_names = [idx["name"] for idx in indexes]
    
    assert "pgnHash_1" in index_names
    assert "chesscomGameId_1" in index_names
    assert "analysis.status_1" in index_names
    
    # Check specific properties
    for idx in indexes:
        if idx["name"] == "pgnHash_1":
            assert idx.get("unique") is True
        if idx["name"] == "chesscomGameId_1":
            assert idx.get("unique") is True
            assert idx.get("sparse") is True

@pytest.mark.asyncio
async def test_redis_stream_and_group(redis_client):
    # First post a game to trigger XADD
    pgn_data = """[Event "Live Chess"]
[Site "Chess.com"]
[Date "2023.01.01"]
[Round "-"]
[White "Player1"]
[Black "Player2"]
[Result "1-0"]
[WhiteElo "1500"]
[BlackElo "1500"]
[TimeControl "600"]
[Termination "Player1 won by resignation"]

1. e4 c5 1-0"""
    
    async with httpx.AsyncClient() as client:
        resp = await client.post(f"{API_URL}/api/games", json={"pgn": pgn_data})
        assert resp.status_code == 201
        data = resp.json()
        game_id = data["gameId"]

    # Check if stream exists and has elements
    stream_len = redis_client.xlen("chess:analysis-jobs")
    assert stream_len >= 1

    # Check if consumer group exists
    groups = redis_client.xinfo_groups("chess:analysis-jobs")
    group_names = [g["name"] for g in groups]
    assert "workers" in group_names
