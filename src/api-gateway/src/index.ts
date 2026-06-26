import { buildApp } from "./app";
import { connectDb } from "./services/db";

const PORT = parseInt(process.env["PORT"] ?? "8080", 10);
const MONGO_URL = process.env["MONGO_URL"] ?? "mongodb://localhost:27017";
const MONGO_DB = process.env["MONGO_DB"] ?? "chess_analyzer";
const LOG_LEVEL = process.env["LOG_LEVEL"] ?? "info";

async function main() {
  await connectDb(MONGO_URL, MONGO_DB);

  const app = buildApp({
    logger: { level: LOG_LEVEL },
  });

  try {
    await app.listen({ port: PORT, host: "0.0.0.0" });
    app.log.info(`API gateway listening on :${PORT}`);
  } catch (err) {
    app.log.error(err);
    process.exit(1);
  }
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
