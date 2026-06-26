import type { FastifyInstance, FastifyReply } from "fastify";
import { ObjectId, type Filter } from "mongodb";
import { parsePgn } from "../services/pgn";
import { fetchPgnFromUrl } from "../services/chesscom";
import { getGamesCollection } from "../services/db";
import { enqueueJob, getRedis } from "../services/redis";
import type { GameDocument } from "../models/game";

interface PostGamesBody {
  pgn?: unknown;
  url?: unknown;
}

function isValidObjectId(id: string): boolean {
  return /^[a-f\d]{24}$/i.test(id);
}

async function findExisting(
  pgnHash: string,
  chesscomGameId: string | null
): Promise<GameDocument | null> {
  const col = getGamesCollection();
  const query: Filter<GameDocument> = chesscomGameId
    ? { $or: [{ chesscomGameId }, { pgnHash }] }
    : { pgnHash };
  return col.findOne(query, {
    projection: { _id: 1, "analysis.status": 1 },
  });
}

function sendCached(reply: FastifyReply, doc: GameDocument): void {
  const gameId = doc._id!.toHexString();
  reply.code(200).send({
    gameId,
    jobId: gameId,
    status: doc.analysis.status,
    cached: true,
  });
}

export async function gamesRoutes(app: FastifyInstance): Promise<void> {
  /**
   * POST /api/games
   * Body: { pgn: string } | { url: string }
   * Returns: 201 { gameId, jobId, status }
   */
  app.post<{ Body: PostGamesBody }>(
    "/api/games",
    {
      schema: {
        body: {
          type: "object",
          properties: {
            pgn: { type: "string" },
            url: { type: "string" },
          },
        },
        response: {
          200: {
            type: "object",
            properties: {
              gameId: { type: "string" },
              jobId: { type: "string" },
              status: { type: "string" },
              cached: { type: "boolean" },
            },
          },
          201: {
            type: "object",
            properties: {
              gameId: { type: "string" },
              jobId: { type: "string" },
              status: { type: "string" },
            },
          },
        },
      },
    },
    async (request, reply) => {
      const body = request.body ?? {};

      const hasPgn = "pgn" in body && body.pgn;
      const hasUrl = "url" in body && body.url;

      if (hasPgn && hasUrl) {
        return reply.code(400).send({ error: "provide either pgn or url, not both" });
      }

      if (!hasPgn && !hasUrl) {
        return reply.code(400).send({ error: "pgn or url is required" });
      }

      // ── F02: chess.com URL path ──────────────────────────────────────────
      if (hasUrl) {
        if (typeof body.url !== "string") {
          return reply.code(400).send({ error: "url must be a string" });
        }

        let fetchResult;
        try {
          fetchResult = await fetchPgnFromUrl(body.url);
        } catch (err) {
          const msg = err instanceof Error ? err.message : String(err);
          if (msg === "not a chess.com game URL") {
            return reply.code(400).send({ error: msg });
          }
          if (msg === "game not found or not public") {
            return reply.code(400).send({ error: msg });
          }
          if (msg === "invalid PGN from chess.com") {
            return reply.code(400).send({ error: msg });
          }
          if (msg === "chess.com API unavailable") {
            return reply.code(502).send({ error: msg });
          }
          throw err;
        }

        let parsed;
        try {
          parsed = parsePgn(fetchResult.pgn);
        } catch (err) {
          const msg = err instanceof Error ? err.message : "invalid PGN";
          if (
            msg.startsWith("pgn is required") ||
            msg.startsWith("invalid PGN") ||
            msg.startsWith("game too long")
          ) {
            return reply.code(400).send({ error: "invalid PGN from chess.com" });
          }
          throw err;
        }

        const existing = await findExisting(parsed.pgnHash, fetchResult.gameId);
        if (existing) {
          return sendCached(reply, existing);
        }

        const col = getGamesCollection();
        const now = new Date();

        const doc = {
          source: "chesscom_url" as const,
          chesscomGameId: fetchResult.gameId,
          pgn: parsed.pgn,
          pgnHash: parsed.pgnHash,
          white: parsed.white,
          black: parsed.black,
          timeControl: parsed.timeControl,
          result: parsed.result,
          ecoCode: parsed.ecoCode,
          playedAt: parsed.playedAt,
          createdAt: now,
          analysis: {
            status: "queued" as const,
            movesAnalyzed: 0,
            movesTotal: parsed.plyCount,
            errorMessage: null,
            updatedAt: now,
            completedAt: null,
            moves: [],
            playerSummaries: [],
          },
        };

        let gameId: string;
        try {
          const result = await col.insertOne(doc);
          gameId = result.insertedId.toHexString();
        } catch (err: unknown) {
          const mongoErr = err as { code?: number };
          if (mongoErr.code === 11000) {
            const dup = await findExisting(parsed.pgnHash, fetchResult.gameId);
            if (dup) {
              return sendCached(reply, dup);
            }
          }
          throw err;
        }

        await enqueueJob(gameId, fetchResult.pgn);
        request.log.info({ gameId }, "game queued via chess.com URL");

        return reply.code(201).send({
          gameId,
          jobId: gameId,
          status: "queued",
        });
      }

      // ── F01: PGN paste path ─────────────────────────────────────────────
      if (typeof body.pgn !== "string") {
        return reply.code(400).send({ error: "pgn is required" });
      }

      let parsed;
      try {
        parsed = parsePgn(body.pgn);
      } catch (err) {
        const msg = err instanceof Error ? err.message : "invalid PGN";
        if (
          msg.startsWith("pgn is required") ||
          msg.startsWith("invalid PGN") ||
          msg.startsWith("game too long")
        ) {
          return reply.code(400).send({ error: msg });
        }
        throw err;
      }

        const existing = await findExisting(parsed.pgnHash, null);
        if (existing) {
          return sendCached(reply, existing);
        }

        const col = getGamesCollection();
        const now = new Date();

        const doc = {
          source: "pgn_paste" as const,
          pgn: parsed.pgn,
          pgnHash: parsed.pgnHash,
          white: parsed.white,
          black: parsed.black,
          timeControl: parsed.timeControl,
          result: parsed.result,
          ecoCode: parsed.ecoCode,
          playedAt: parsed.playedAt,
          createdAt: now,
          analysis: {
            status: "queued" as const,
            movesAnalyzed: 0,
            movesTotal: parsed.plyCount,
            errorMessage: null,
            updatedAt: now,
            completedAt: null,
            moves: [],
            playerSummaries: [],
          },
        };

        let gameId: string;
        try {
          const result = await col.insertOne(doc);
          gameId = result.insertedId.toHexString();
        } catch (err: unknown) {
          const mongoErr = err as { code?: number };
          if (mongoErr.code === 11000) {
            const dup = await findExisting(parsed.pgnHash, null);
            if (dup) {
              return sendCached(reply, dup);
            }
          }
          throw err;
        }

        await enqueueJob(gameId, body.pgn);
        request.log.info({ gameId }, "game queued");

        return reply.code(201).send({
          gameId,
          jobId: gameId,
          status: "queued",
        });
    }
  );

  app.get<{ Params: { gameId: string } }>(
    "/api/games/:gameId",
    async (request, reply) => {
      const { gameId } = request.params;

      if (!isValidObjectId(gameId)) {
        return reply.code(404).send({ error: "game not found" });
      }

      const col = getGamesCollection();
      const doc = await col.findOne(
        { _id: new ObjectId(gameId) },
        { projection: { pgn: 0, "analysis.moves": 0, "analysis.playerSummaries": 0 } }
      );

      if (!doc) {
        return reply.code(404).send({ error: "game not found" });
      }

      return reply.code(200).send({
        gameId: doc._id.toHexString(),
        source: doc.source,
        white: doc.white,
        black: doc.black,
        timeControl: doc.timeControl,
        result: doc.result,
        ecoCode: doc.ecoCode,
        playedAt: doc.playedAt,
        createdAt: doc.createdAt,
        status: doc.analysis.status,
      });
    }
  );

  app.get<{ Params: { gameId: string } }>(
    "/api/games/:gameId/analysis",
    async (request, reply) => {
      const { gameId } = request.params;

      if (!isValidObjectId(gameId)) {
        return reply.code(404).send({ error: "game not found" });
      }

      const col = getGamesCollection();
      const doc = await col.findOne(
        { _id: new ObjectId(gameId) },
        { projection: { "analysis.status": 1, "analysis.moves": 1, "analysis.playerSummaries": 1, "analysis.errorMessage": 1, "analysis.movesAnalyzed": 1, "analysis.movesTotal": 1 } }
      );

      if (!doc) {
        return reply.code(404).send({ error: "game not found" });
      }

      const analysis = doc.analysis;

      if (analysis.status === "completed") {
        return reply.code(200).send({
          gameId,
          moves: analysis.moves,
          playerSummaries: analysis.playerSummaries,
        });
      }

      if (analysis.status === "failed") {
        return reply.code(500).send({
          error: "analysis failed",
          gameId,
          errorMessage: analysis.errorMessage,
        });
      }

      const redis = getRedis();
      const progress = await redis.hgetall(`job:${gameId}:progress`);

      return reply.code(409).send({
        error: "analysis in progress",
        gameId,
        jobId: gameId,
        status: analysis.status,
        movesAnalyzed: parseInt(progress.movesAnalyzed ?? String(analysis.movesAnalyzed ?? 0), 10),
        movesTotal: analysis.movesTotal,
      });
    }
  );
}
