import { buildApp } from './app';

const PORT = parseInt(process.env.PORT || '18080', 10);

async function start() {
  try {
    const app = await buildApp();
    await app.listen({ port: PORT, host: '0.0.0.0' });
    app.log.info(`API Gateway listening on port ${PORT}`);
  } catch (err) {
    console.error(err);
    process.exit(1);
  }
}

start();
