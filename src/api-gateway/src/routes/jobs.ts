import { FastifyInstance, FastifyRequest, FastifyReply } from 'fastify';
import { getRedis } from '../services/redis';
import { getGamesCollection } from '../services/db';

function isValidObjectId(id: string): boolean {
  return /^[a-f\d]{24}$/i.test(id);
}

interface JobParams {
  jobId: string;
}

export default async function (fastify: FastifyInstance) {
  fastify.get('/api/jobs/:jobId', async (request: FastifyRequest<{ Params: JobParams }>, reply: FastifyReply) => {
    const { jobId } = request.params;

    let realJobId = jobId;

    if (!isValidObjectId(jobId)) {
      const gamesCollection = getGamesCollection();
      const game = await gamesCollection.findOne({ chesscomGameId: jobId });
      if (game) {
        realJobId = game._id!.toString();
      } else {
        return reply.status(404).send({ error: 'job not found' });
      }
    }

    const redis = getRedis();
    const data = await redis.hgetall(`job:${realJobId}:progress`);

    if (!data || Object.keys(data).length === 0) {
      return reply.status(404).send({ error: 'job not found' });
    }

    const status = data.status;
    const movesAnalyzed = data.movesAnalyzed ? parseInt(data.movesAnalyzed, 10) : 0;
    const movesTotal = data.movesTotal ? parseInt(data.movesTotal, 10) : 0;
    const error = data.errorMessage || data.error || null;

    return {
      jobId: realJobId,
      status,
      movesAnalyzed,
      movesTotal,
      error
    };
  });
}
