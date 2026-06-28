import os
import time
import subprocess
import pymongo
import redis
import httpx
from playwright.sync_api import sync_playwright

REDIS_URL = os.environ.get("REDIS_URL", "redis://localhost:6379")
MONGO_URL = os.environ.get("MONGO_URL", "mongodb://localhost:27017")
FRONTEND_URL = "http://localhost:3000"
PLAYER_NAME = "deborah80rojas105"


def get_latest_game_url(username: str) -> str:
    """Fetch the latest game URL for a Chess.com player."""
    url = f"https://api.chess.com/pub/player/{username}/games"
    response = httpx.get(url, timeout=10)
    response.raise_for_status()
    games = response.json().get("games", [])
    if not games:
        raise ValueError(f"No games found for player {username}")
    latest = games[0]
    game_url = latest["url"]
    print(f"Found latest game for {username}: {game_url}")
    return game_url

def main():
    print("Connecting to Redis and MongoDB...")
    redis_client = redis.Redis.from_url(REDIS_URL, decode_responses=True)
    mongo_client = pymongo.MongoClient(MONGO_URL)
    db = mongo_client.chess_analyzer

    print("Clearing existing game/job cache to ensure the analysis runs fresh...")
    try:
        redis_client.flushall()
    except Exception as e:
        print(f"Warning clearing redis: {e}")
    
    res = db.games.delete_many({})
    print(f"Deleted {res.deleted_count} games from MongoDB.")

    print("Building worker...")
    subprocess.run(["make", "build-worker"], cwd="src/analyze-service", check=True)

    print("Starting worker process...")
    worker_env = {
        **os.environ,
        "REDIS_URL": REDIS_URL,
        "MONGO_URL": MONGO_URL,
        "WORKER_CONCURRENCY": "1",
        "ENGINE_DEPTH": "8",  # shallow depth for faster execution in E2E
        "BOOK_PLIES": "4"
    }
    worker_proc = subprocess.Popen(
        ["./src/analyze-service/bin/analyze-worker"],
        env=worker_env
    )
    time.sleep(2)  # Give worker time to spin up

    try:
        with sync_playwright() as p:
            print("Launching browser (headed)...")
            browser = p.chromium.launch(headless=False)
            page = browser.new_page()

            print(f"Navigating to {FRONTEND_URL}...")
            page.goto(FRONTEND_URL)

            print("Waiting for form...")
            page.wait_for_selector("#analysis-form")

            print(f"Fetching latest game for player {PLAYER_NAME}...")
            game_url = get_latest_game_url(PLAYER_NAME)
            print(f"Entering Chess.com URL: {game_url}")
            page.fill("#url-input", game_url)
            
            print("Submitting for analysis...")
            page.click("#submit-button")
            
            print("Waiting for progress container...")
            page.wait_for_selector("#progress-container", timeout=10000)
            
            print("Analysis started. Watching progress...")
            
            start_time = time.time()
            completed = False
            while time.time() - start_time < 90:
                if page.is_visible("#chessboard-container"):
                    print("\nAnalysis completed successfully!")
                    completed = True
                    break
                
                if page.is_visible("#progress-status"):
                    status = page.text_content("#progress-status")
                    bar = page.locator("#progress-bar-fill")
                    style = bar.get_attribute("style") if bar.count() > 0 else ""
                    print(f"Status: {status} | Progress: {style}", end="\r")
                
                time.sleep(0.5)
            
            if not completed:
                print("\nTimeout waiting for analysis to complete!")
            else:
                time.sleep(1.0)
                print("Setting board to starting position...")
                page.click(".start-row")
                time.sleep(1.0)
                
                # Retrieve the number of moves/plies from "#move-counter"
                counter_text = page.text_content("#move-counter")
                if counter_text:
                    parts = counter_text.split("/")
                    if len(parts) == 2:
                        total_plies = int(parts[1].strip())
                        print(f"Stepping through {total_plies} plies to show pieces moving...")
                        for ply in range(1, total_plies + 1):
                            print(f"Moving to ply {ply}...")
                            page.click("#next-move-btn")
                            time.sleep(1.0)
            
            print("\nBrowser is open. You can view the analysis page.")
            print("Press Ctrl+C or enter anything in terminal to close the browser and exit...")
            try:
                input()
            except KeyboardInterrupt:
                pass
            
            browser.close()
    finally:
        print("Stopping worker process...")
        worker_proc.terminate()
        try:
            worker_proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            worker_proc.kill()
        
        redis_client.close()
        mongo_client.close()
        print("Done!")

if __name__ == "__main__":
    main()
