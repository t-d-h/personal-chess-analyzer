export type Classification =
  | "best"
  | "excellent"
  | "good"
  | "inaccuracy"
  | "mistake"
  | "blunder"
  | "brilliant"
  | "book";

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
  classification: Classification | null;
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

export interface PostGameResponse {
  gameId: string;
  jobId: string;
  status: string;
  cached?: boolean;
}

export interface JobStatus {
  jobId: string;
  status: string;
  movesAnalyzed: number;
  movesTotal: number;
  error: string | null;
}

export interface GameMeta {
  gameId: string;
  source: "pgn_paste" | "chesscom_url";
  white: { username: string; rating: number | null };
  black: { username: string; rating: number | null };
  timeControl: string | null;
  result: string;
  ecoCode: string | null;
  playedAt: string | null;
  createdAt: string;
  status: string;
}

export interface AnalysisResponse {
  gameId: string;
  moves: MoveData[];
  playerSummaries: PlayerSummary[];
}

const API_BASE = import.meta.env.VITE_API_BASE_URL ?? "";

async function request<T>(path: string, init?: RequestInit): Promise<T> {
  const res = await fetch(`${API_BASE}${path}`, {
    headers: { "Content-Type": "application/json", ...((init?.headers as Record<string, string>) ?? {}) },
    ...init,
  });
  if (!res.ok) {
    const body = await res.json().catch(() => ({ error: res.statusText }));
    throw new Error(body.error ?? `HTTP ${res.status}`);
  }
  return res.json() as Promise<T>;
}

export function postGame(body: { pgn: string } | { url: string }): Promise<PostGameResponse> {
  return request<PostGameResponse>("/api/games", { method: "POST", body: JSON.stringify(body) });
}

export function getJob(jobId: string): Promise<JobStatus> {
  return request<JobStatus>(`/api/jobs/${jobId}`);
}

export function getGameMeta(gameId: string): Promise<GameMeta> {
  return request<GameMeta>(`/api/games/${gameId}`);
}

export function getAnalysis(gameId: string): Promise<AnalysisResponse> {
  return request<AnalysisResponse>(`/api/games/${gameId}/analysis`);
}
