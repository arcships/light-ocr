'use strict';

const { createApp } = require('./app');
const { readConfig } = require('./config');
const { initEngine } = require('./engine');

function createShutdownHandler(server, engine, exit = process.exit) {
  let shuttingDown = false;
  return async function shutdown() {
    if (shuttingDown) return;
    shuttingDown = true;
    await new Promise((resolve) => server.close(resolve));
    await engine.close();
    exit(0);
  };
}

async function main() {
  const config = readConfig();
  const engine = await initEngine(config);
  const app = createApp(engine, { maxConcurrentUploads: config.queueCapacity });
  const server = app.listen(config.port, () => {
    console.log(`light-ocr-server listening on port ${config.port}`);
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
