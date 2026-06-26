import { createHash } from "crypto";
import { Chess } from "chess.js";

const MAX_PLIES = 300;

export interface ParsedPgn {
  pgn: string;
  pgnHash: string;
  white: { username: string; rating: number | null };
  black: { username: string; rating: number | null };
  timeControl: string | null;
  result: string;
  ecoCode: string | null;
  playedAt: Date | null;
  plyCount: number;
}

/**
 * Normalize PGN before hashing: strip move-time/clock comments and collapse
 * whitespace so two copies of the same game hash identically.
 */
function normalizePgn(raw: string): string {
  return raw
    .replace(/\{[^}]*\}/g, "") // strip { comments }
    .replace(/;[^\n]*/g, "") // strip ; end-of-line comments
    .replace(/\s+/g, " ")
    .trim();
}

function sha256(text: string): string {
  return createHash("sha256").update(text, "utf8").digest("hex");
}

function parseRating(value: string | undefined): number | null {
  if (!value || value === "?" || value === "-") return null;
  const n = parseInt(value, 10);
  return isNaN(n) ? null : n;
}

function parsePlayedAt(
  date: string | undefined,
  startTime: string | undefined
): Date | null {
  if (!date || date === "????.??.??") return null;
  const iso = startTime ? `${date}T${startTime}` : date;
  const d = new Date(iso);
  return isNaN(d.getTime()) ? null : d;
}

/**
 * Validate and parse a raw PGN string.
 * Throws an Error with a human-readable message on failure.
 */
export function parsePgn(raw: string): ParsedPgn {
  if (!raw || typeof raw !== "string" || raw.trim() === "") {
    throw new Error("pgn is required");
  }

  let chess: Chess;
  try {
    chess = new Chess();
    chess.loadPgn(raw);
  } catch (err) {
    const msg = err instanceof Error ? err.message : String(err);
    throw new Error(`invalid PGN: ${msg}`);
  }

  const headers = chess.header();
  const history = chess.history();
  const plyCount = history.length;

  if (plyCount > MAX_PLIES) {
    throw new Error(`game too long (max ${MAX_PLIES} plies)`);
  }

  const normalized = normalizePgn(raw);

  return {
    pgn: raw,
    pgnHash: sha256(normalized),
    white: {
      username: headers["White"] ?? "?",
      rating: parseRating(headers["WhiteElo"] ?? undefined),
    },
    black: {
      username: headers["Black"] ?? "?",
      rating: parseRating(headers["BlackElo"] ?? undefined),
    },
    timeControl: headers["TimeControl"] ?? null,
    result: headers["Result"] ?? "*",
    ecoCode: headers["ECO"] ?? null,
    playedAt: parsePlayedAt(headers["Date"] ?? undefined, headers["StartTime"] ?? undefined),
    plyCount,
  };
}
