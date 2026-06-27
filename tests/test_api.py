import os
import datetime
import pytest
import redis
import pymongo
import httpx
from bson import ObjectId

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

def test_get_job_unknown(redis_client):
    # Valid ObjectId but not in Redis
    with httpx.Client() as client:
        resp = client.get(f"{API_URL}/api/jobs/507f1f77bcf86cd799439011")
        assert resp.status_code == 404
        assert resp.json() == {"error": "job not found"}

    # Invalid ID format
    with httpx.Client() as client:
        resp = client.get(f"{API_URL}/api/jobs/badid")
        assert resp.status_code == 404
        assert resp.json() == {"error": "job not found"}

def test_get_job_running(redis_client):
    job_id = "507f1f77bcf86cd799439012"
    progress_key = f"job:{job_id}:progress"
    
    redis_client.delete(progress_key)
    redis_client.hset(progress_key, mapping={
        "status": "running",
        "movesAnalyzed": "15",
        "movesTotal": "40"
    })
    
    with httpx.Client() as client:
        resp = client.get(f"{API_URL}/api/jobs/{job_id}")
        assert resp.status_code == 200
        assert resp.json() == {
            "jobId": job_id,
            "status": "running",
            "movesAnalyzed": 15,
            "movesTotal": 40,
            "error": None
        }
    redis_client.delete(progress_key)

def test_get_job_completed(redis_client):
    job_id = "507f1f77bcf86cd799439013"
    progress_key = f"job:{job_id}:progress"
    
    redis_client.delete(progress_key)
    redis_client.hset(progress_key, mapping={
        "status": "completed",
        "movesAnalyzed": "30",
        "movesTotal": "30"
    })
    
    with httpx.Client() as client:
        resp = client.get(f"{API_URL}/api/jobs/{job_id}")
        assert resp.status_code == 200
        assert resp.json() == {
            "jobId": job_id,
            "status": "completed",
            "movesAnalyzed": 30,
            "movesTotal": 30,
            "error": None
        }
    redis_client.delete(progress_key)

def test_get_game_badid():
    # Bad ID format
    with httpx.Client() as client:
        resp = client.get(f"{API_URL}/api/games/badid")
        assert resp.status_code == 404
        assert resp.json() == {"error": "game not found"}

    # Valid ID but not in DB
    with httpx.Client() as client:
        resp = client.get(f"{API_URL}/api/games/507f1f77bcf86cd79943901a")
        assert resp.status_code == 404
        assert resp.json() == {"error": "game not found"}

def test_get_game_metadata(mongo_client):
    db = mongo_client.chess_analyzer
    game_id = ObjectId("507f1f77bcf86cd79943901b")
    db.games.delete_one({"_id": game_id})

    db.games.insert_one({
        "_id": game_id,
        "source": "pgn_paste",
        "chesscomGameId": None,
        "pgn": '1. e4 e5 2. Nf3',
        "pgnHash": "somehashval",
        "white": { "username": "Alice", "rating": 1500 },
        "black": { "username": "Bob", "rating": 1480 },
        "timeControl": "600+0",
        "result": "1-0",
        "ecoCode": "B20",
        "playedAt": None,
        "createdAt": datetime.datetime.now(datetime.timezone.utc),
        "analysis": {
            "status": "completed",
            "movesAnalyzed": 2,
            "movesTotal": 2,
            "errorMessage": None,
            "completedAt": None,
            "moves": [ { "ply": 1, "san": "e4", "classification": "best" } ],
            "playerSummaries": [ { "color": "white", "accuracyPct": 94.2 } ]
        }
    })

    with httpx.Client() as client:
        resp = client.get(f"{API_URL}/api/games/{str(game_id)}")
        assert resp.status_code == 200
        data = resp.json()
        
        # Verify metadata fields
        assert data["gameId"] == str(game_id)
        assert data["source"] == "pgn_paste"
        assert data["white"] == { "username": "Alice", "rating": 1500 }
        assert data["black"] == { "username": "Bob", "rating": 1480 }
        assert data["timeControl"] == "600+0"
        assert data["result"] == "1-0"
        assert data["ecoCode"] == "B20"
        assert data["status"] == "completed"
        
        # Projections verify: moves, playerSummaries, and pgn MUST NOT be in metadata response
        assert "pgn" not in data
        assert "analysis" not in data
        assert "moves" not in data
        assert "playerSummaries" not in data

    db.games.delete_one({"_id": game_id})

def test_get_game_analysis_completed(mongo_client):
    db = mongo_client.chess_analyzer
    game_id = ObjectId("507f1f77bcf86cd79943901c")
    db.games.delete_one({"_id": game_id})

    db.games.insert_one({
        "_id": game_id,
        "source": "pgn_paste",
        "chesscomGameId": None,
        "pgn": '1. e4 e5 2. Nf3',
        "pgnHash": "somehashval",
        "white": { "username": "Alice", "rating": 1500 },
        "black": { "username": "Bob", "rating": 1480 },
        "timeControl": "600+0",
        "result": "1-0",
        "ecoCode": "B20",
        "playedAt": None,
        "createdAt": datetime.datetime.now(datetime.timezone.utc),
        "analysis": {
            "status": "completed",
            "movesAnalyzed": 2,
            "movesTotal": 2,
            "errorMessage": None,
            "completedAt": None,
            "moves": [ { "ply": 1, "san": "e4", "classification": "best" } ],
            "playerSummaries": [ { "color": "white", "accuracyPct": 94.2 } ]
        }
    })

    with httpx.Client() as client:
        resp = client.get(f"{API_URL}/api/games/{str(game_id)}/analysis")
        assert resp.status_code == 200
        data = resp.json()
        assert data["gameId"] == str(game_id)
        assert data["moves"] == [ { "ply": 1, "san": "e4", "classification": "best" } ]
        assert data["playerSummaries"] == [ { "color": "white", "accuracyPct": 94.2 } ]

    db.games.delete_one({"_id": game_id})

def test_get_game_analysis_still_running(mongo_client, redis_client):
    db = mongo_client.chess_analyzer
    game_id = ObjectId("507f1f77bcf86cd79943901d")
    db.games.delete_one({"_id": game_id})
    progress_key = f"job:{str(game_id)}:progress"
    redis_client.delete(progress_key)

    db.games.insert_one({
        "_id": game_id,
        "source": "pgn_paste",
        "chesscomGameId": None,
        "pgn": '1. e4 e5 2. Nf3',
        "pgnHash": "somehashval",
        "white": { "username": "Alice", "rating": 1500 },
        "black": { "username": "Bob", "rating": 1480 },
        "timeControl": "600+0",
        "result": "1-0",
        "ecoCode": "B20",
        "playedAt": None,
        "createdAt": datetime.datetime.now(datetime.timezone.utc),
        "analysis": {
            "status": "running",
            "movesAnalyzed": 3,
            "movesTotal": 20,
            "errorMessage": None,
            "completedAt": None,
            "moves": [],
            "playerSummaries": []
        }
    })

    # Test 1: Still running, no Redis progress hash (falls back to DB values)
    with httpx.Client() as client:
        resp = client.get(f"{API_URL}/api/games/{str(game_id)}/analysis")
        assert resp.status_code == 409
        data = resp.json()
        assert data["error"] == "analysis in progress"
        assert data["jobId"] == str(game_id)
        assert data["status"] == "running"
        assert data["movesAnalyzed"] == 3
        assert data["movesTotal"] == 20

    # Test 2: Still running, with live Redis progress hash
    redis_client.hset(progress_key, mapping={
        "status": "running",
        "movesAnalyzed": "11",
        "movesTotal": "20"
    })

    with httpx.Client() as client:
        resp = client.get(f"{API_URL}/api/games/{str(game_id)}/analysis")
        assert resp.status_code == 409
        data = resp.json()
        assert data["error"] == "analysis in progress"
        assert data["jobId"] == str(game_id)
        assert data["status"] == "running"
        assert data["movesAnalyzed"] == 11
        assert data["movesTotal"] == 20

    db.games.delete_one({"_id": game_id})
    redis_client.delete(progress_key)

def test_get_game_analysis_failed(mongo_client):
    db = mongo_client.chess_analyzer
    game_id = ObjectId("507f1f77bcf86cd79943901e")
    db.games.delete_one({"_id": game_id})

    db.games.insert_one({
        "_id": game_id,
        "source": "pgn_paste",
        "chesscomGameId": None,
        "pgn": '1. e4 e5 2. Nf3',
        "pgnHash": "somehashval",
        "white": { "username": "Alice", "rating": 1500 },
        "black": { "username": "Bob", "rating": 1480 },
        "timeControl": "600+0",
        "result": "1-0",
        "ecoCode": "B20",
        "playedAt": None,
        "createdAt": datetime.datetime.now(datetime.timezone.utc),
        "analysis": {
            "status": "failed",
            "movesAnalyzed": 0,
            "movesTotal": 20,
            "errorMessage": "Stockfish subprocess exited with code 1",
            "completedAt": None,
            "moves": [],
            "playerSummaries": []
        }
    })

    with httpx.Client() as client:
        resp = client.get(f"{API_URL}/api/games/{str(game_id)}/analysis")
        assert resp.status_code == 500
        data = resp.json()
        assert data["error"] == "analysis failed"
        assert data["errorMessage"] == "Stockfish subprocess exited with code 1"

    db.games.delete_one({"_id": game_id})
