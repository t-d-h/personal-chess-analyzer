import { MongoClient, Collection, Db } from "mongodb";
import type { GameDocument } from "../models/game";

let client: MongoClient | null = null;
let db: Db | null = null;

export async function connectDb(url: string, dbName: string): Promise<void> {
  if (client) return; // already connected
  client = new MongoClient(url);
  await client.connect();
  db = client.db(dbName);

  // Ensure indexes (idempotent — createIndex is a no-op if the index already exists)
  const col = db.collection<GameDocument>("games");
  await col.createIndex({ pgnHash: 1 }, { unique: true, name: "idx_pgn_hash" });
  try {
    await col.dropIndex("idx_chesscom_game_id");
  } catch {
    // index may not exist yet or collection is empty
  }
  await col.createIndex(
    { chesscomGameId: 1 },
    { unique: true, sparse: true, name: "idx_chesscom_game_id" }
  );
  await col.createIndex(
    { "analysis.status": 1 },
    { name: "idx_analysis_status" }
  );
}

export function getDb(): Db {
  if (!db) throw new Error("DB not connected — call connectDb() first");
  return db;
}

export function getGamesCollection(): Collection<GameDocument> {
  return getDb().collection<GameDocument>("games");
}

export async function closeDb(): Promise<void> {
  if (client) {
    await client.close();
    client = null;
    db = null;
  }
}
