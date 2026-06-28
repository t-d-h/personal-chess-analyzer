export interface PostGameResponse {
  gameId: string;
  jobId: string;
  status: string;
}

export interface JobStatus {
  jobId: string;
  status: 'queued' | 'running' | 'completed' | 'failed';
  movesAnalyzed: number;
  movesTotal: number;
  error: string | null;
}

export interface GameMeta {
  gameId: string;
  source: 'pgn_paste' | 'chesscom_url';
  white: { username: string; rating: number };
  black: { username: string; rating: number };
  timeControl: string;
  result: string;
  ecoCode: string;
  playedAt: string | null;
  createdAt: string;
  status: string;
}

export interface AnalysisMove {
  ply: number;
  color: 'white' | 'black';
  san: string;
  fenBefore: string;
  fenAfter: string;
  evalCpPlayed: number | null;
  evalCpBest: number | null;
  evalMate: number | null;
  bestMoveSan: string | null;
  winPercentLoss: number;
  moveAccuracy: number;
  classification: string;
  engineDepth: number;
}

export interface PlayerSummary {
  color: 'white' | 'black';
  accuracyPct: number;
  acpl: number;
  bestCount: number;
  excellentCount: number;
  goodCount: number;
  inaccuracyCount: number;
  mistakeCount: number;
  blunderCount: number;
  brilliantCount: number;
  estimatedRating: number;
  openingAccuracy: number;
  midgameAccuracy: number;
  endgameAccuracy: number;
}

export interface AnalysisResponse {
  gameId: string;
  moves: AnalysisMove[];
  playerSummaries: PlayerSummary[];
}

export async function postGame(body: { pgn: string } | { url: string }): Promise<PostGameResponse> {
  const res = await fetch('/api/games', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body)
  });
  if (!res.ok) {
    const errData = await res.json().catch(() => ({}));
    throw new Error(errData.error || `HTTP error ${res.status}`);
  }
  return res.json();
}

export async function getJob(jobId: string): Promise<JobStatus> {
  const res = await fetch(`/api/jobs/${jobId}`);
  if (!res.ok) {
    const errData = await res.json().catch(() => ({}));
    throw new Error(errData.error || `HTTP error ${res.status}`);
  }
  return res.json();
}

export async function getGameMeta(gameId: string): Promise<GameMeta> {
  const res = await fetch(`/api/games/${gameId}`);
  if (!res.ok) {
    const errData = await res.json().catch(() => ({}));
    throw new Error(errData.error || `HTTP error ${res.status}`);
  }
  return res.json();
}

export async function getAnalysis(gameId: string): Promise<AnalysisResponse> {
  const res = await fetch(`/api/games/${gameId}/analysis`);
  if (!res.ok) {
    const errData = await res.json().catch(() => ({}));
    const err = new Error(errData.error || `HTTP error ${res.status}`) as any;
    err.status = res.status;
    err.details = errData;
    throw err;
  }
  return res.json();
}
