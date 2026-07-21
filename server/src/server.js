'use strict';

const { initEngine } = require('./engine');
const { createApp } = require('./app');

const PORT = Number(process.env.PORT ?? 3000);

function createShutdownHandler(server, engine, exit = process.exit) {
  let shuttingDown = false;
  return async function shutdown() {
    if (shuttingDown) return;
    shuttingDown = true;
    server.close();
    await engine.close();
    exit(0);
  };
}

async function main() {
  const engine = await initEngine();
  const app = createApp(engine);
  const server = app.listen(PORT, () => {
    console.log(`light-ocr-api listening on port ${PORT}`);
  });

  const shutdown = createShutdownHandler(server, engine);
  process.on('SIGTERM', shutdown);
  process.on('SIGINT', shutdown);
}

if (require.main === module) {
  main().catch((error) => {
    console.error(error);
    process.exit(1);
  });
}

module.exports = { createShutdownHandler, main };
