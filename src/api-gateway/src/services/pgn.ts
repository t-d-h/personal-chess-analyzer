import { Chess } from 'chess.js';
import crypto from 'crypto';

export interface ParsedPgn {
  whiteUsername: string;
  whiteRating: number | null;
  blackUsername: string;
  blackRating: number | null;
  timeControl: string | null;
  result: string;
  ecoCode: string | null;
  playedAt: Date | null;
  plyCount: number;
  pgnHash: string;
}

export function parseAndValidatePgn(pgn: string): ParsedPgn {
  const chess = new Chess();
  try {
    chess.loadPgn(pgn);
  } catch (error: any) {
    throw new Error(`invalid PGN: ${error.message}`);
  }

  const history = chess.history();
  const plyCount = history.length;
  
  if (plyCount > 300) {
    throw new Error('game too long (max 300 plies)');
  }

  const headers = chess.header();

  const whiteUsername = headers['White'] || '?';
  const whiteRatingStr = headers['WhiteElo'];
  const whiteRating = whiteRatingStr && whiteRatingStr !== '?' ? parseInt(whiteRatingStr, 10) : null;

  const blackUsername = headers['Black'] || '?';
  const blackRatingStr = headers['BlackElo'];
  const blackRating = blackRatingStr && blackRatingStr !== '?' ? parseInt(blackRatingStr, 10) : null;

  const timeControl = headers['TimeControl'] || null;
  const result = headers['Result'] || '*';
  const ecoCode = headers['ECO'] || null;

  let playedAt = null;
  if (headers['Date'] && headers['Date'] !== '????.??.??') {
    let dateStr = headers['Date'].replace(/\./g, '-');
    if (headers['StartTime']) {
      dateStr += `T${headers['StartTime']}`;
    }
    const parsedDate = new Date(dateStr);
    if (!isNaN(parsedDate.getTime())) {
      playedAt = parsedDate;
    }
  }

  // Normalize PGN for hash: comprehensive normalization as per F08 design
  const normalizedPgn = normalizePgn(pgn);
  const pgnHash = crypto.createHash('sha256').update(normalizedPgn).digest('hex');

  return {
    whiteUsername,
    whiteRating,
    blackUsername,
    blackRating,
    timeControl,
    result,
    ecoCode,
    playedAt,
    plyCount,
    pgnHash
  };
}

export function normalizePgn(pgn: string): string {
  return pgn
    .replace(/\{[^}]*\}/g, '')     // strip { comments }
    .replace(/;[^\n]*/g, '')        // strip ; end-of-line comments
    .replace(/\d+\.\.\./g, '')      // strip black move numbers like "1..."
    .replace(/\[%[^\]]*\]/g, '')    // strip [%clk ...] clock annotations
    .replace(/\s+/g, ' ')           // collapse whitespace
    .trim();
}

