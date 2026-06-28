import os
import time
import subprocess
import pytest
from playwright.sync_api import sync_playwright
import pymongo
import redis

REDIS_URL = os.environ.get("REDIS_URL", "redis://localhost:6379")
MONGO_URL = os.environ.get("MONGO_URL", "mongodb://localhost:27017")
FRONTEND_URL = "http://localhost:3000"

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
    subprocess.run(["make", "build-worker"], cwd="src/analyze-service", check=True)
    
    # Start worker process
    proc = subprocess.Popen(
        ["./src/analyze-service/bin/analyze-worker"],
        env={
            **os.environ,
            "REDIS_URL": REDIS_URL,
            "MONGO_URL": MONGO_URL,
            "WORKER_CONCURRENCY": "1",
            "ENGINE_DEPTH": "8",  # Shallow depth for faster tests
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

def test_frontend_e2e_pgn_paste(worker, redis_client, mongo_client):
    # Clear DB & Stream
    try:
        pending_info = redis_client.xpending("chess:analysis-jobs", "workers")
        if pending_info and pending_info.get("pending", 0) > 0:
            pending_details = redis_client.xpending_range("chess:analysis-jobs", "workers", "-", "+", pending_info["pending"])
            for item in pending_details:
                redis_client.xack("chess:analysis-jobs", "workers", item["message_id"])
    except Exception:
        pass
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

    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True)
        page = browser.new_page()
        
        # Load home page
        page.goto(FRONTEND_URL)
        
        # Check input form is visible
        page.wait_for_selector("#analysis-form")
        assert page.is_visible("#pgn-input")
        assert page.is_visible("#url-input")
        assert page.is_visible("#submit-button")
        
        # Paste valid PGN
        page.fill("#pgn-input", pgn_data)
        page.click("#submit-button")
        
        # Progress bar appears
        page.wait_for_selector("#progress-container")
        assert page.is_visible("#progress-status")
        assert page.locator("#progress-bar-fill").count() > 0
        
        # Wait for completion (polls GET /api/jobs/:id, renders finished analysis)
        # We wait for the chessboard container to appear
        page.wait_for_selector("#chessboard-container", timeout=60000)
        
        # Check that metadata banner is loaded
        assert page.is_visible("#game-meta-banner")
        banner_text = page.text_content("#game-meta-banner")
        assert banner_text is not None
        assert "Player1" in banner_text
        assert "Player2" in banner_text
        
        # Check move list is visible
        assert page.is_visible("#movelist-container")
        
        # Check starting position selection or ply counter
        counter_text = page.text_content("#move-counter")
        assert counter_text is not None
        # It defaults to the final ply
        assert "Ply 7 / 7" in counter_text
        
        # Click prev move button, verify counter updates
        page.click("#prev-move-btn")
        counter_text = page.text_content("#move-counter")
        assert counter_text is not None
        assert "Ply 6 / 7" in counter_text
        
        # Click next move button, verify counter updates
        page.click("#next-move-btn")
        counter_text = page.text_content("#move-counter")
        assert counter_text is not None
        assert "Ply 7 / 7" in counter_text
        
        # Eval graph rendered
        assert page.is_visible("#eval-graph-container")
        svg_element = page.locator("#eval-graph-container svg")
        assert svg_element.count() > 0
        
        # Player accuracy visible
        assert page.is_visible("#player-stats-container")
        stats_content = page.text_content("#player-stats-container")
        assert stats_content is not None
        assert "%" in stats_content
        
        browser.close()

def test_frontend_e2e_chesscom_url(worker, redis_client, mongo_client):
    # Clear DB & Stream
    try:
        pending_info = redis_client.xpending("chess:analysis-jobs", "workers")
        if pending_info and pending_info.get("pending", 0) > 0:
            pending_details = redis_client.xpending_range("chess:analysis-jobs", "workers", "-", "+", pending_info["pending"])
            for item in pending_details:
                redis_client.xack("chess:analysis-jobs", "workers", item["message_id"])
    except Exception:
        pass
    try:
        redis_client.xtrim("chess:analysis-jobs", maxlen=0)
    except Exception:
        pass
    db = mongo_client.chess_analyzer
    db.games.delete_many({})

    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True)
        page = browser.new_page()
        
        # Load home page
        page.goto(FRONTEND_URL)
        page.wait_for_selector("#analysis-form")
        
        # Fill in valid mock Chess.com URL
        page.fill("#url-input", "https://www.chess.com/game/live/test-valid-mock")
        page.click("#submit-button")
        
        # Progress bar appears
        page.wait_for_selector("#progress-container")
        
        # Wait for completion and check board is rendered
        page.wait_for_selector("#chessboard-container", timeout=60000)
        assert page.is_visible("#game-meta-banner")
        
        browser.close()

def test_frontend_e2e_chesscom_review_url(worker, redis_client, mongo_client):
    # Clear DB & Stream
    try:
        pending_info = redis_client.xpending("chess:analysis-jobs", "workers")
        if pending_info and pending_info.get("pending", 0) > 0:
            pending_details = redis_client.xpending_range("chess:analysis-jobs", "workers", "-", "+", pending_info["pending"])
            for item in pending_details:
                redis_client.xack("chess:analysis-jobs", "workers", item["message_id"])
    except Exception:
        pass
    try:
        redis_client.xtrim("chess:analysis-jobs", maxlen=0)
    except Exception:
        pass
    db = mongo_client.chess_analyzer
    db.games.delete_many({})

    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True)
        page = browser.new_page()
        
        # Load home page
        page.goto(FRONTEND_URL)
        page.wait_for_selector("#analysis-form")
        
        # Fill in valid mock Chess.com URL
        page.fill("#url-input", "https://www.chess.com/analysis/game/live/test-valid-mock/review")
        page.click("#submit-button")
        
        # Progress bar appears
        page.wait_for_selector("#progress-container")
        
        # Wait for completion and check board is rendered
        page.wait_for_selector("#chessboard-container", timeout=60000)
        assert page.is_visible("#game-meta-banner")
        
        browser.close()

def test_frontend_e2e_user_specified_url(worker, redis_client, mongo_client):
    # Clear DB & Stream
    try:
        pending_info = redis_client.xpending("chess:analysis-jobs", "workers")
        if pending_info and pending_info.get("pending", 0) > 0:
            pending_details = redis_client.xpending_range("chess:analysis-jobs", "workers", "-", "+", pending_info["pending"])
            for item in pending_details:
                redis_client.xack("chess:analysis-jobs", "workers", item["message_id"])
    except Exception:
        pass
    try:
        redis_client.xtrim("chess:analysis-jobs", maxlen=0)
    except Exception:
        pass
    db = mongo_client.chess_analyzer
    db.games.delete_many({})

    with sync_playwright() as p:
        # Open playwright headless=False
        browser = p.chromium.launch(headless=False)
        page = browser.new_page()
        
        # Load home page
        page.goto(FRONTEND_URL)
        page.wait_for_selector("#analysis-form")
        
        # Paste URL and start analyze
        page.fill("#url-input", "https://www.chess.com/game/live/170638222548")
        page.click("#submit-button")
        
        # Wait for progress bar container to appear
        page.wait_for_selector("#progress-container")
        
        # Wait for completion and check board is rendered
        page.wait_for_selector("#chessboard-container", timeout=60000)
        assert page.is_visible("#game-meta-banner")
        
        # Select starting position to start stepping
        time.sleep(1.0)
        page.click(".start-row")
        time.sleep(1.0)
        
        counter_text = page.text_content("#move-counter")
        if counter_text:
            parts = counter_text.split("/")
            if len(parts) == 2:
                total_plies = int(parts[1].strip())
                for ply in range(1, total_plies + 1):
                    page.click("#next-move-btn")
                    time.sleep(1.0)
        
        browser.close()


