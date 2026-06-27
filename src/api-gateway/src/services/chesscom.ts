import fetch from "node-fetch";

const CHESSCOM_URL_RE =
  /^https?:\/\/(?:www\.)?chess\.com\/(?:game|analysis\/game)\/(?:(?:live|daily)\/)?([A-Za-z0-9]+)/;

const CHESSCOM_API_TIMEOUT_MS = 5000;

const USER_AGENT = "personal-chess-analyzer/1.0";

export interface ChesscomFetchResult {
  pgn: string;
  gameId: string;
}

export function parseChesscomUrl(url: string): string {
  const match = CHESSCOM_URL_RE.exec(url);
  if (!match) {
    throw new Error("not a chess.com game URL");
  }
  return match[1];
}

export async function fetchPgnFromUrl(
  url: string
): Promise<ChesscomFetchResult> {
  const gameId = parseChesscomUrl(url);

  const apiBase = process.env["CHESSCOM_API_BASE"] || "https://api.chess.com";
  const apiUrl = `${apiBase}/pub/game/${gameId}`;

  let response: fetch.Response;
  try {
    response = await fetch(apiUrl, {
      headers: { "User-Agent": USER_AGENT },
      signal: AbortSignal.timeout(CHESSCOM_API_TIMEOUT_MS),
    });
  } catch (err) {
    if (err instanceof Error && err.name === "TimeoutError") {
      throw new Error("chess.com API unavailable");
    }
    throw err;
  }

  if (!response.ok) {
    throw new Error("game not found or not public");
  }

  const data = (await response.json()) as { pgn?: string };

  if (!data.pgn || typeof data.pgn !== "string") {
    throw new Error("invalid PGN from chess.com");
  }

  return { pgn: data.pgn, gameId };
}
