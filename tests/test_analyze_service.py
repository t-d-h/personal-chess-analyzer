import os
import time
import subprocess
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

@pytest.fixture
def worker():
    # Build first to make sure we run the latest binary
    subprocess.run(["make", "build-worker"], cwd="analyze-service", check=True)
    
    # Start worker process
    proc = subprocess.Popen(
        ["./analyze-service/bin/analyze-worker"],
        env={
            **os.environ,
            "REDIS_URL": REDIS_URL,
            "MONGO_URL": MONGO_URL,
            "WORKER_CONCURRENCY": "1",
            "ENGINE_DEPTH": "10",  # Shallow depth for faster tests
            "BOOK_PLIES": "4"
        }
    )
    time.sleep(1)  # Allow worker to start and connect
    yield proc
    
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()

def test_successful_analysis(worker, redis_client, mongo_client):
    # Clear stream and collection first to ensure clean test
    try:
        redis_client.xtrim("chess:analysis-jobs", maxlen=0)
    except Exception:
        pass
    db = mongo_client.chess_analyzer
    db.games.delete_many({})
    
    pgn_data = """[Event "Scholar's Mate"]
[Site "Local Test"]
[Date "2026.06.28"]
[Round "1"]
[White "Player1"]
[Black "Player2"]
[Result "1-0"]

1. e4 e5 2. Qh5 Nc6 3. Bc4 Nf6 4. Qxf7# 1-0"""
    
    # Submit game via API Gateway
    with httpx.Client() as client:
        resp = client.post(f"{API_URL}/api/games", json={"pgn": pgn_data})
        assert resp.status_code == 201
        data = resp.json()
        game_id = data["gameId"]
        
    # Poll progress hash to verify "running" state
    running_detected = False
    start_time = time.time()
    progress_key = f"job:{game_id}:progress"
    
    while time.time() - start_time < 15:
        progress = redis_client.hgetall(progress_key)
        if progress.get("status") == "running":
            running_detected = True
        elif progress.get("status") == "completed":
            break
        time.sleep(0.05)
        
    # Poll MongoDB for completion
    completed = False
    game_doc = None
    start_time = time.time()
    
    while time.time() - start_time < 20:
        game_doc = db.games.find_one({"_id": ObjectId(game_id)})
        if game_doc and game_doc.get("analysis", {}).get("status") == "completed":
            completed = True
            break
        time.sleep(0.5)
        
    assert completed is True, "Analysis did not complete in time"
    assert running_detected is True, "Did not detect 'running' state in progress hash"
    
    # Verify Redis progress hash final state and TTL
    progress = redis_client.hgetall(progress_key)
    assert progress.get("status") == "completed"
    assert progress.get("movesAnalyzed") == "7"
    assert progress.get("movesTotal") == "7"
    
    ttl = redis_client.ttl(progress_key)
    assert 0 < ttl <= 300
    
    # Verify MongoDB document
    analysis = game_doc["analysis"]
    assert analysis["status"] == "completed"
    assert analysis["movesAnalyzed"] == 7
    assert len(analysis["moves"]) == 7
    assert len(analysis["playerSummaries"]) == 2
    assert "completedAt" in analysis
    assert "updatedAt" in analysis
    
    # Verify XACK called: no pending stream entries
    pending = redis_client.xpending("chess:analysis-jobs", "workers")
    assert pending["pending"] == 0

def test_failed_analysis(worker, redis_client, mongo_client):
    try:
        redis_client.xtrim("chess:analysis-jobs", maxlen=0)
    except Exception:
        pass
    db = mongo_client.chess_analyzer
    db.games.delete_many({})
    
    # Manually insert game to MongoDB
    game_id = str(db.games.insert_one({
        "analysis": {
            "status": "queued",
            "movesAnalyzed": 0,
            "movesTotal": 0,
            "errorMessage": None,
            "completedAt": None,
            "moves": [],
            "playerSummaries": []
        }
    }).inserted_id)
    
    # Push invalid PGN task directly to Redis stream
    redis_client.xadd("chess:analysis-jobs", {"gameId": game_id, "pgn": "invalid pgn content"})
    
    # Poll MongoDB for failure status
    failed = False
    game_doc = None
    start_time = time.time()
    
    while time.time() - start_time < 15:
        game_doc = db.games.find_one({"_id": ObjectId(game_id)})
        if game_doc and game_doc.get("analysis", {}).get("status") == "failed":
            failed = True
            break
        time.sleep(0.2)
        
    assert failed is True, "Worker did not mark the job as failed"
    
    # Verify errorMessage is populated
    analysis = game_doc["analysis"]
    assert analysis["status"] == "failed"
    assert analysis["errorMessage"] is not None
    assert len(analysis["errorMessage"]) > 0
    
    # Verify Redis progress hash
    progress_key = f"job:{game_id}:progress"
    progress = redis_client.hgetall(progress_key)
    assert progress.get("status") == "failed"
    assert progress.get("errorMessage") == analysis["errorMessage"]
    
    ttl = redis_client.ttl(progress_key)
    assert 0 < ttl <= 300
    
    # Verify stream entry acknowledged
    pending = redis_client.xpending("chess:analysis-jobs", "workers")
    assert pending["pending"] == 0
