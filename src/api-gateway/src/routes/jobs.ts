import type { FastifyInstance } from "fastify";
import { getRedis } from "../services/redis";

function isValidObjectId(id: string): boolean {
  return /^[a-f\d]{24}$/i.test(id);
}

export async function jobsRoutes(app: FastifyInstance): Promise<void> {
  app.get<{ Params: { jobId: string } }>(
    "/api/jobs/:jobId",
    async (request, reply) => {
      const { jobId } = request.params;

      if (!isValidObjectId(jobId)) {
        return reply.code(404).send({ error: "job not found" });
      }

      const redis = getRedis();
      const data = await redis.hgetall(`job:${jobId}:progress`);

      if (!data || Object.keys(data).length === 0) {
        return reply.code(404).send({ error: "job not found" });
      }

      return reply.code(200).send({
        jobId,
        status: data.status ?? "queued",
        movesAnalyzed: parseInt(data.movesAnalyzed ?? "0", 10),
        movesTotal: parseInt(data.movesTotal ?? "0", 10),
        error: data.errorMessage ?? null,
      });
    }
  );
}
