import Fastify from "fastify";
import sensible from "@fastify/sensible";
import cors from "@fastify/cors";
import rateLimit from "@fastify/rate-limit";
import { gamesRoutes } from "./routes/games";
import { jobsRoutes } from "./routes/jobs";
import { RATE_LIMIT_MAX, RATE_LIMIT_WINDOW } from "./config";

const BODY_LIMIT = 600 * 1024;

export function buildApp(opts: { logger: boolean | object; trustProxy?: boolean } = { logger: true }) {
  const app = Fastify({ ...opts, bodyLimit: BODY_LIMIT, trustProxy: opts.trustProxy ?? false });

  app.register(sensible);
  app.register(cors, {
    origin: "*",
  });

  app.register(rateLimit, {
    global: true,
    max: RATE_LIMIT_MAX,
    timeWindow: RATE_LIMIT_WINDOW,
  });

  app.get("/health", async (_request, reply) => {
    return reply.send({ status: "ok" });
  });

  app.register(gamesRoutes);
  app.register(jobsRoutes);

  return app;
}

export { RATE_LIMIT_MAX, RATE_LIMIT_WINDOW };
