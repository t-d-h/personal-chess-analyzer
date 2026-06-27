import { ObjectId } from 'mongodb';

export interface GameDocument {
  _id?: ObjectId;
  source: "pgn_paste" | "chesscom_url";
  chesscomGameId: string | null;
  pgn: string;
  pgnHash: string;
  white: { username: string; rating: number | null };
  black: { username: string; rating: number | null };
  timeControl: string | null;
  result: string;
  ecoCode: string | null;
  playedAt: Date | null;
  createdAt: Date;
  analysis: {
    status: "queued" | "running" | "completed" | "failed";
    movesAnalyzed: number;
    movesTotal: number;
    errorMessage: string | null;
    updatedAt: Date;
    completedAt: Date | null;
    moves: any[];
    playerSummaries: any[];
  };
}
