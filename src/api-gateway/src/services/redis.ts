import Redis from 'ioredis';

const REDIS_URL = process.env.REDIS_URL || 'redis://localhost:6379';

let redis: Redis;

export async function connectRedis() {
  if (!redis) {
    redis = new Redis(REDIS_URL);
    
    redis.on('error', (err) => {
      console.error('Redis error', err);
    });

    await new Promise<void>((resolve, reject) => {
      const onReady = () => {
        console.log('Connected to Redis');
        redis.off('error', onError);
        resolve();
      };
      const onError = (err: any) => {
        redis.off('ready', onReady);
        reject(err);
      };
      redis.once('ready', onReady);
      redis.once('error', onError);
    });

    // Initialize consumer group
    try {
      await redis.xgroup('CREATE', 'chess:analysis-jobs', 'workers', '$', 'MKSTREAM');
      console.log('Created consumer group workers for stream chess:analysis-jobs');
    } catch (err: any) {
      if (err.message && err.message.includes('BUSYGROUP')) {
        console.log('Consumer group workers already exists');
      } else {
        console.error('Failed to create consumer group', err);
        throw err;
      }
    }
  }
}

export function getRedis(): Redis {
  if (!redis) {
    throw new Error('Redis not connected. Call connectRedis() first.');
  }
  return redis;
}

export async function disconnectRedis() {
  if (redis) {
    await redis.quit();
  }
}
