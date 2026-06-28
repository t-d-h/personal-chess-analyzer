import { FastifyInstance, FastifyRequest, FastifyReply } from 'fastify';
import { parseAndValidatePgn } from '../services/pgn';
import { getGamesCollection } from '../services/db';
import { GameDocument } from '../models/game';
import { fetchPgnFromUrl } from '../services/chesscom';
import { getRedis } from '../services/redis';
import { ObjectId } from 'mongodb';

function isValidObjectId(id: string): boolean {
  return /^[a-f\d]{24}$/i.test(id);
}


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

    const gamesCollection = getGamesCollection();

    if (url) {
      const match = url.match(/^https?:\/\/(?:www\.)?chess\.com\/(?:analysis\/)?game\/(?:live|daily)\/([^\/?#]+)/i);
      if (!match) {
        return reply.status(400).send({ error: 'not a chess.com game URL' });
      }
      chesscomGameId = match[1];
      source = "chesscom_url";

      // F08: Check chesscomGameId cache hit before fetching
      const existingByUrl = await gamesCollection.findOne({ chesscomGameId });
      if (existingByUrl) {
        return reply.status(200).send({
          gameId: existingByUrl._id.toString(),
          jobId: existingByUrl._id.toString(),
          status: existingByUrl.analysis.status,
          cached: true
        });
      }

      try {
        const result = await fetchPgnFromUrl(url);
        finalPgn = result.pgn;
        chesscomGameId = result.gameId;
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

    // F08: Check pgnHash cache hit
    const existingByHash = await gamesCollection.findOne({ pgnHash: parsed.pgnHash });
    if (existingByHash) {
      // If we got here via a chess.com URL but the existing doc didn't have chesscomGameId set, update it
      if (chesscomGameId && !existingByHash.chesscomGameId) {
        await gamesCollection.updateOne(
          { _id: existingByHash._id },
          { $set: { chesscomGameId } }
        ).catch(() => {});
      }
      return reply.status(200).send({
        gameId: existingByHash._id.toString(),
        jobId: existingByHash._id.toString(),
        status: existingByHash.analysis.status,
        cached: true
      });
    }

    // F08: Insert with concurrency protection
    try {
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

      // Initialize progress hash in Redis to avoid 404 for queued jobs
      await redis.hset(`job:${gameId}:progress`,
        'status', 'queued',
        'movesAnalyzed', '0',
        'movesTotal', String(parsed.plyCount)
      );
      await redis.expire(`job:${gameId}:progress`, 3600);

      return reply.status(201).send({
        gameId: gameId,
        jobId: gameId,
        status: 'queued'
      });
    } catch (err: any) {
      // E11000 duplicate key error handling
      if (err.code === 11000 || (err.message && err.message.includes('E11000'))) {
        const query: any = {};
        if (chesscomGameId) {
          query.$or = [{ pgnHash: parsed.pgnHash }, { chesscomGameId }];
        } else {
          query.pgnHash = parsed.pgnHash;
        }

        const existingAfterRace = await gamesCollection.findOne(query);
        if (existingAfterRace) {
          return reply.status(200).send({
            gameId: existingAfterRace._id.toString(),
            jobId: existingAfterRace._id.toString(),
            status: existingAfterRace.analysis.status,
            cached: true
          });
        }
      }
      throw err;
    }
  });

  interface GameParams {
    gameId: string;
  }

  fastify.get('/api/games/:gameId', async (request: FastifyRequest<{ Params: GameParams }>, reply: FastifyReply) => {
    const { gameId } = request.params;

    if (!isValidObjectId(gameId)) {
      return reply.status(404).send({ error: 'game not found' });
    }

    const gamesCollection = getGamesCollection();
    const game = await gamesCollection.findOne(
      { _id: new ObjectId(gameId) },
      { projection: { pgn: 0, "analysis.moves": 0, "analysis.playerSummaries": 0 } }
    );

    if (!game) {
      return reply.status(404).send({ error: 'game not found' });
    }

    return {
      gameId: game._id!.toString(),
      source: game.source,
      white: game.white,
      black: game.black,
      timeControl: game.timeControl,
      result: game.result,
      ecoCode: game.ecoCode,
      playedAt: game.playedAt,
      createdAt: game.createdAt,
      status: game.analysis.status
    };
  });

  fastify.get('/api/games/:gameId/analysis', async (request: FastifyRequest<{ Params: GameParams }>, reply: FastifyReply) => {
    const { gameId } = request.params;

    if (!isValidObjectId(gameId)) {
      return reply.status(404).send({ error: 'game not found' });
    }

    const gamesCollection = getGamesCollection();
    const game = await gamesCollection.findOne({ _id: new ObjectId(gameId) });

    if (!game) {
      return reply.status(404).send({ error: 'game not found' });
    }

    if (game.analysis.status === 'failed') {
      return reply.status(500).send({
        error: 'analysis failed',
        errorMessage: game.analysis.errorMessage || 'unknown error'
      });
    }

    if (game.analysis.status === 'completed') {
      return {
        gameId: game._id!.toString(),
        moves: game.analysis.moves,
        playerSummaries: game.analysis.playerSummaries
      };
    }

    // Default: queued or running. Check Redis for live progress.
    let status = game.analysis.status;
    let movesAnalyzed = game.analysis.movesAnalyzed;
    let movesTotal = game.analysis.movesTotal;

    try {
      const redis = getRedis();
      const progressData = await redis.hgetall(`job:${gameId}:progress`);
      if (progressData && Object.keys(progressData).length > 0) {
        status = (progressData.status as any) || status;
        if (progressData.movesAnalyzed) {
          movesAnalyzed = parseInt(progressData.movesAnalyzed, 10);
        }
        if (progressData.movesTotal) {
          movesTotal = parseInt(progressData.movesTotal, 10);
        }
      }
    } catch (e) {
      // Ignore redis errors, fallback to MongoDB values
    }

    return reply.status(409).send({
      error: 'analysis in progress',
      jobId: gameId,
      status: status,
      movesAnalyzed: movesAnalyzed,
      movesTotal: movesTotal
    });
  });
}

