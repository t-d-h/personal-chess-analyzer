import Redis from "ioredis";

const STREAM_KEY = "chess:analysis-jobs";
const CONSUMER_GROUP = "workers";

let redis: Redis | null = null;

export function getRedis(): Redis {
  if (!redis) throw new Error("Redis not connected — call connectRedis() first");
  return redis;
}

export async function connectRedis(url: string): Promise<void> {
  if (redis) return;
  redis = new Redis(url, { lazyConnect: true, maxRetriesPerRequest: null });
  await redis.connect();
  await initConsumerGroup(redis);
}

async function initConsumerGroup(r: Redis): Promise<void> {
  try {
    await r.xgroup("CREATE", STREAM_KEY, CONSUMER_GROUP, "$", "MKSTREAM");
  } catch (err: unknown) {
    const msg = err instanceof Error ? err.message : String(err);
    if (!msg.includes("BUSYGROUP")) {
      throw err;
    }
  }
}

export async function enqueueJob(gameId: string, pgn: string): Promise<string> {
  const r = getRedis();
  const entryId = await r.xadd(
    STREAM_KEY,
    "*",
    "gameId",
    gameId,
    "pgn",
    pgn
  );
  if (entryId === null) {
    throw new Error("XADD to chess:analysis-jobs returned null");
  }
  return entryId;
}

export async function closeRedis(): Promise<void> {
  if (redis) {
    await redis.quit();
    redis = null;
  }
}

export { STREAM_KEY, CONSUMER_GROUP };
