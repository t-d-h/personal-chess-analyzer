import { ObjectId } from "mongodb";

/** Embedded per-move data (populated by analyze-service later) */
export interface MoveData {
  ply: number;
  color: "white" | "black";
  san: string;
  fenBefore: string;
  fenAfter: string;
  evalCpPlayed: number | null;
  evalCpBest: number | null;
  evalMate: number | null;
  bestMoveSan: string | null;
  winPercentLoss: number | null;
  moveAccuracy: number | null;
  classification:
    | "best"
    | "excellent"
    | "good"
    | "inaccuracy"
    | "mistake"
    | "blunder"
    | "brilliant"
    | "book"
    | null;
  engineDepth: number | null;
}

export interface PlayerSummary {
  color: "white" | "black";
  accuracyPct: number;
  acpl: number;
  bestCount: number;
  excellentCount: number;
  goodCount: number;
  inaccuracyCount: number;
  mistakeCount: number;
  blunderCount: number;
  brilliantCount: number;
}

export interface AnalysisData {
  status: "queued" | "running" | "completed" | "failed";
  movesAnalyzed: number;
  movesTotal: number;
  errorMessage: string | null;
  updatedAt: Date;
  completedAt: Date | null;
  moves: MoveData[];
  playerSummaries: PlayerSummary[];
}

export interface GameDocument {
  _id?: ObjectId;
  source: "pgn_paste" | "chesscom_url";
  chesscomGameId?: string;
  pgn: string;
  pgnHash: string;
  white: { username: string; rating: number | null };
  black: { username: string; rating: number | null };
  timeControl: string | null;
  result: string;
  ecoCode: string | null;
  playedAt: Date | null;
  createdAt: Date;
  analysis: AnalysisData;
}
