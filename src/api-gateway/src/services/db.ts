import { MongoClient, Db, Collection } from 'mongodb';
import { GameDocument } from '../models/game';

const MONGO_URL = process.env.MONGO_URL || 'mongodb://localhost:27017';
const DB_NAME = 'chess_analyzer';

let client: MongoClient;
let db: Db;

export async function connectDB() {
  if (!client) {
    client = new MongoClient(MONGO_URL);
    await client.connect();
    db = client.db(DB_NAME);
    
    // Ensure pgnHash index for F08 dedup
    await db.collection('games').createIndex({ pgnHash: 1 }, { unique: true });
    await db.collection('games').createIndex({ chesscomGameId: 1 }, { unique: true, sparse: true });
    await db.collection('games').createIndex({ "analysis.status": 1 });
    console.log('Connected to MongoDB');
  }
}

export function getDb(): Db {
  if (!db) {
    throw new Error('Database not connected. Call connectDB() first.');
  }
  return db;
}

export function getGamesCollection(): Collection<GameDocument> {
  return getDb().collection<GameDocument>('games');
}

export async function disconnectDB() {
  if (client) {
    await client.close();
  }
}
