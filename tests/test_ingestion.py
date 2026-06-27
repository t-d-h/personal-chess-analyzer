import pytest
import httpx
import asyncio
from pymongo import MongoClient
from bson.objectid import ObjectId

API_URL = "http://localhost:18080"
MONGO_URL = "mongodb://localhost:27017"

VALID_PGN = '''[Event "FIDE World Cup 2017"]
[Site "Tbilisi GEO"]
[Date "2017.09.09"]
[Round "4.1"]
[White "Carlsen,M"]
[Black "Bu Xiangzhi"]
[Result "0-1"]
[WhiteElo "2827"]
[BlackElo "2710"]
[EventDate "2017.09.03"]
[ECO "C55"]

1. e4 e5 2. Nf3 Nc6 3. Bc4 Nf6 4. d3 h6 5. O-O d6 6. c3 g6 7. Re1 Bg7
8. h3 O-O 9. Nbd2 Re8 10. b4 a6 11. a4 Be6 12. Bxe6 Rxe6 13. Bb2 d5
14. Qc2 Qd7 15. Nb3 b6 16. Rad1 Rae8 17. b5 axb5 18. axb5 Na7 19. c4 d4
20. Ra1 Nc8 21. Ra6 Nh5 22. Bc1 Rf6 23. Nh2 Kh7 24. Bd2 Rg8 25. Rea1 Bf8
26. Ra8 Bd6 27. R1a6 Re6 28. Qc1 g5 29. Qd1 Nf6 30. Ng4 Nxg4 31. hxg4 Ree8
32. Qf3 Ne7 33. R8a7 Qe6 34. Nc1 Ng6 35. g3 Nf8 36. Na2 Nd7 37. Bb4 Nf6
38. Bxd6 cxd6 39. Nb4 Kg7 40. Nd5 Nxd5 41. cxd5 Qf6 42. Qxf6+ Kxf6
43. Rxb6 Rd8 44. Raa6 Ke7 45. Rb7+ Rd7 46. Raa7 Rxb7 47. Rxb7+ Kf6
48. Rd7 Rb8 49. Rxd6+ Kg7 50. b6 0-1'''

long_moves = []
for i in range(1, 156):
    if i % 2 == 1:
        long_moves.append(f"{i}. Nf3 Nf6")
    else:
        long_moves.append(f"{i}. Ng1 Ng8")
LONG_PGN = '[Event "Long Game"]\n\n' + " ".join(long_moves)


@pytest.fixture(scope="session")
def db():
    client = MongoClient(MONGO_URL)
    db = client.chess_analyzer
    yield db
    client.drop_database("chess_analyzer")
    client.close()

@pytest.mark.asyncio
async def test_valid_pgn(db):
    async with httpx.AsyncClient() as client:
        response = await client.post(f"{API_URL}/api/games", json={"pgn": VALID_PGN})
    
    assert response.status_code == 201
    data = response.json()
    assert "gameId" in data
    assert "jobId" in data
    assert data["status"] == "queued"

    game = db.games.find_one({"_id": ObjectId(data["gameId"])})
    assert game is not None
    assert game["white"]["username"] == "Carlsen,M"
    assert game["black"]["username"] == "Bu Xiangzhi"
    assert game["white"]["rating"] == 2827
    assert game["black"]["rating"] == 2710
    assert game["result"] == "0-1"
    assert game["ecoCode"] == "C55"
    assert game["timeControl"] is None
    assert game["analysis"]["status"] == "queued"

@pytest.mark.asyncio
async def test_empty_body():
    async with httpx.AsyncClient() as client:
        response = await client.post(f"{API_URL}/api/games", json={})
    assert response.status_code == 400
    assert "pgn or url is required" in response.text

@pytest.mark.asyncio
async def test_garbage_pgn():
    async with httpx.AsyncClient() as client:
        response = await client.post(f"{API_URL}/api/games", json={"pgn": "garbage"})
    assert response.status_code == 400
    assert "invalid PGN" in response.text

@pytest.mark.asyncio
async def test_long_pgn():
    async with httpx.AsyncClient() as client:
        response = await client.post(f"{API_URL}/api/games", json={"pgn": LONG_PGN})
    assert response.status_code == 400
    assert "game too long" in response.text

@pytest.mark.asyncio
async def test_deduplication(db):
    async with httpx.AsyncClient() as client:
        response1 = await client.post(f"{API_URL}/api/games", json={"pgn": VALID_PGN})
        response2 = await client.post(f"{API_URL}/api/games", json={"pgn": VALID_PGN})
    
    assert response1.status_code == 201
    assert response2.status_code == 201
    assert response1.json()["gameId"] == response2.json()["gameId"]
