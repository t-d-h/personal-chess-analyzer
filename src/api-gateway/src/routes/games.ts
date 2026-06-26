import type { FastifyInstance } from "fastify";
import { parsePgn } from "../services/pgn";
import { getGamesCollection } from "../services/db";

interface PostGamesBody {
  pgn?: unknown;
  url?: unknown;
}

export async function gamesRoutes(app: FastifyInstance): Promise<void> {
  /**
   * POST /api/games
   * Body: { pgn: string } — raw PGN paste
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

      // ── F01: PGN paste path ─────────────────────────────────────────────
      if ("pgn" in body || !("url" in body)) {
        if (!body.pgn || typeof body.pgn !== "string") {
          return reply.code(400).send({ error: "pgn is required" });
        }

        let parsed;
        try {
          parsed = parsePgn(body.pgn as string);
        } catch (err) {
          const msg = err instanceof Error ? err.message : "invalid PGN";
          // Distinguish validation errors from internal errors
          if (
            msg.startsWith("pgn is required") ||
            msg.startsWith("invalid PGN") ||
            msg.startsWith("game too long")
          ) {
            return reply.code(400).send({ error: msg });
          }
          throw err; // re-throw unexpected errors → 500
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
          // E11000 = MongoDB duplicate key — same pgnHash already exists.
          // Return the existing document's id (full dedup logic comes in F08).
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

        // TODO (F06): XADD job to Redis stream
        request.log.info({ gameId }, "game queued (Redis stub)");

        return reply.code(201).send({
          gameId,
          jobId: gameId, // 1:1 mapping per architecture
          status: "queued",
        });
      }

      // URL path reserved for F02
      return reply.code(400).send({ error: "url ingestion not yet implemented" });
    }
  );
}
