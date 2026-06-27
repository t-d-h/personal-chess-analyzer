import { FastifyInstance, FastifyRequest, FastifyReply } from 'fastify';
import { parseAndValidatePgn } from '../services/pgn';
import { getGamesCollection } from '../services/db';
import { GameDocument } from '../models/game';

interface PostGamesBody {
  pgn: string;
}

export default async function (fastify: FastifyInstance) {
  fastify.post('/api/games', async (request: FastifyRequest<{ Body: PostGamesBody }>, reply: FastifyReply) => {
    const { pgn } = request.body || {};

    if (!pgn) {
      return reply.status(400).send({ error: 'pgn is required' });
    }

    let parsed;
    try {
      parsed = parseAndValidatePgn(pgn);
    } catch (err: any) {
      return reply.status(400).send({ error: err.message });
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
    const gameDoc: GameDocument = {
      source: "pgn_paste",
      chesscomGameId: null,
      pgn: pgn,
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

    const insertResult = await gamesCollection.insertOne(gameDoc);

    return reply.status(201).send({
      gameId: insertResult.insertedId,
      jobId: insertResult.insertedId,
      status: 'queued'
    });
  });
}
