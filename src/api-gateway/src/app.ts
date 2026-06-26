import Fastify from "fastify";
import sensible from "@fastify/sensible";
import cors from "@fastify/cors";
import { gamesRoutes } from "./routes/games";
import { jobsRoutes } from "./routes/jobs";

export function buildApp(opts: { logger: boolean | object } = { logger: true }) {
  const app = Fastify(opts);

  app.register(sensible);
  app.register(cors, {
    origin: "*",
  });

  // Health check — used by Docker Compose healthcheck and load balancers
  app.get("/health", async (_request, reply) => {
    return reply.send({ status: "ok" });
  });

  app.register(gamesRoutes);
  app.register(jobsRoutes);

  return app;
}
