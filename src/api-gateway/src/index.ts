import { buildApp } from "./app";
import { connectDb } from "./services/db";
import { connectRedis, closeRedis } from "./services/redis";

const PORT = parseInt(process.env["PORT"] ?? "8080", 10);
const MONGO_URL = process.env["MONGO_URL"] ?? "mongodb://localhost:27017";
const MONGO_DB = process.env["MONGO_DB"] ?? "chess_analyzer";
const REDIS_URL = process.env["REDIS_URL"] ?? "redis://localhost:6379";
const LOG_LEVEL = process.env["LOG_LEVEL"] ?? "info";

async function main() {
  await connectDb(MONGO_URL, MONGO_DB);
  await connectRedis(REDIS_URL);

  const app = buildApp({
    logger: { level: LOG_LEVEL },
  });

  try {
    await app.listen({ port: PORT, host: "0.0.0.0" });
    app.log.info({ port: PORT }, "API gateway listening");
  } catch (err) {
    app.log.error(err);
    process.exit(1);
  }

  const shutdown = async () => {
    await closeRedis();
    process.exit(0);
  };
  process.on("SIGTERM", shutdown);
  process.on("SIGINT", shutdown);
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
