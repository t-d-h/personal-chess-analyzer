import Fastify from "fastify";
import sensible from "@fastify/sensible";
import { gamesRoutes } from "./routes/games";

export function buildApp(opts: { logger: boolean | object } = { logger: true }) {
  const app = Fastify(opts);

  app.register(sensible);

  // Health check — used by Docker Compose healthcheck and load balancers
  app.get("/health", async (_request, reply) => {
    return reply.send({ status: "ok" });
  });

  app.register(gamesRoutes);

  return app;
}
