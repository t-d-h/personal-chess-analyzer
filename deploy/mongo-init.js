// MongoDB initialization script — runs once on first container start
// Creates indexes required by ARCHITECTURE.md §4

db = db.getSiblingDB("chess_analyzer");

db.createCollection("games");

db.games.createIndex({ pgnHash: 1 }, { unique: true, name: "idx_pgn_hash" });
db.games.createIndex(
  { chesscomGameId: 1 },
  { sparse: true, name: "idx_chesscom_game_id" }
);
db.games.createIndex(
  { "analysis.status": 1 },
  { name: "idx_analysis_status" }
);
db.games.createIndex(
  { createdAt: 1 },
  { expireAfterSeconds: 2592000, name: "idx_ttl_30d" } // 30-day TTL
);

print("chess_analyzer: indexes created.");
