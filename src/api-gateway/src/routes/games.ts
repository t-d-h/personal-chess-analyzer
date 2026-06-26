import type { FastifyInstance } from "fastify";
import { parsePgn } from "../services/pgn";
import { fetchPgnFromUrl } from "../services/chesscom";
import { getGamesCollection } from "../services/db";

interface PostGamesBody {
  pgn?: unknown;
  url?: unknown;
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
            const existing = await col.findOne(
              { pgnHash: parsed.pgnHash },
              { projection: { _id: 1, "analysis.status": 1 } }
            );
            if (existing) {
              gameId = existing._id.toHexString();
              return reply.code(201).send({
                gameId,
                jobId: gameId,
                status: existing.analysis.status,
              });
            }
          }
          throw err;
        }

        request.log.info({ gameId }, "game queued via chess.com URL (Redis stub)");

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

      const col = getGamesCollection();
      const now = new Date();

      const doc = {
        source: "pgn_paste" as const,
        chesscomGameId: null,
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
          const existing = await col.findOne(
            { pgnHash: parsed.pgnHash },
            { projection: { _id: 1, "analysis.status": 1 } }
          );
          if (existing) {
            gameId = existing._id.toHexString();
            return reply.code(201).send({
              gameId,
              jobId: gameId,
              status: existing.analysis.status,
            });
          }
        }
        throw err;
      }

      request.log.info({ gameId }, "game queued (Redis stub)");

      return reply.code(201).send({
        gameId,
        jobId: gameId,
        status: "queued",
      });
    }
  );
}
