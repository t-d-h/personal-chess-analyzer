import fastify from 'fastify';
import cors from '@fastify/cors';
import sensible from '@fastify/sensible';
import gamesRoutes from './routes/games';
import { connectDB } from './services/db';
import { connectRedis } from './services/redis';

export async function buildApp() {
  const app = fastify({ logger: true });

  await app.register(cors);
  await app.register(sensible);

  await connectDB();
  await connectRedis();

  await app.register(gamesRoutes);

  // Healthcheck
  app.get('/health', async () => {
    return { status: 'ok' };
  });

  return app;
}
