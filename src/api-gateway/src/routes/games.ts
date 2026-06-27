import { FastifyInstance, FastifyRequest, FastifyReply } from 'fastify';
import { parseAndValidatePgn } from '../services/pgn';
import { getGamesCollection } from '../services/db';
import { GameDocument } from '../models/game';
import { fetchPgnFromUrl } from '../services/chesscom';
import { getRedis } from '../services/redis';

interface PostGamesBody {
  pgn?: string;
  url?: string;
}

export default async function (fastify: FastifyInstance) {
  fastify.post('/api/games', async (request: FastifyRequest<{ Body: PostGamesBody }>, reply: FastifyReply) => {
    const { pgn, url } = request.body || {};

    if (pgn && url) {
      return reply.status(400).send({ error: 'cannot provide both pgn and url' });
    }
    if (!pgn && !url) {
      return reply.status(400).send({ error: 'pgn or url is required' });
    }

    let finalPgn = pgn;
    let chesscomGameId: string | null = null;
    let source: "pgn_paste" | "chesscom_url" = "pgn_paste";

    if (url) {
      try {
        const result = await fetchPgnFromUrl(url);
        finalPgn = result.pgn;
        chesscomGameId = result.gameId;
        source = "chesscom_url";
      } catch (err: any) {
        if (err.message === 'chess.com API unavailable') {
          return reply.status(502).send({ error: err.message });
        }
        return reply.status(400).send({ error: err.message });
      }
    }

    let parsed;
    try {
      parsed = parseAndValidatePgn(finalPgn!);
    } catch (err: any) {
      return reply.status(400).send({ error: url ? 'invalid PGN from chess.com' : err.message });
    }

    const gamesCollection = getGamesCollection();

    // Check dedup (for F08 compatibility, though fully F08 might need more logic)
    const existing = await gamesCollection.findOne({ pgnHash: parsed.pgnHash });
    if (existing) {
      return reply.status(201).send({
        gameId: existing._id,
        jobId: existing._id,
        status: existing.analysis.status
      });
    }

    const now = new Date();
    const gameDoc: any = {
      source: source,
      pgn: finalPgn!,
      pgnHash: parsed.pgnHash,
      white: { username: parsed.whiteUsername, rating: parsed.whiteRating },
      black: { username: parsed.blackUsername, rating: parsed.blackRating },
      timeControl: parsed.timeControl,
      result: parsed.result,
      ecoCode: parsed.ecoCode,
      playedAt: parsed.playedAt,
      createdAt: now,
      analysis: {
        status: "queued",
        movesAnalyzed: 0,
        movesTotal: parsed.plyCount,
        errorMessage: null,
        updatedAt: now,
        completedAt: null,
        moves: [],
        playerSummaries: []
      }
    };

    if (chesscomGameId) {
      gameDoc.chesscomGameId = chesscomGameId;
    }

    const insertResult = await gamesCollection.insertOne(gameDoc);
    const gameId = insertResult.insertedId.toString();

    const redis = getRedis();
    await redis.xadd('chess:analysis-jobs', '*', 'gameId', gameId, 'pgn', finalPgn!);

    return reply.status(201).send({
      gameId: gameId,
      jobId: gameId,
      status: 'queued'
    });
  });
}
