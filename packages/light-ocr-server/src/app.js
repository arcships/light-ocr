'use strict';

const express = require('express');

const healthRouter = require('./routes/health');
const infoRouter = require('./routes/info');
const ocrRouter = require('./routes/ocr');
const { errorHandler } = require('./errors');

function createApp(engine, options = {}) {
  const app = express();
  app.use('/api/v1', healthRouter());
  app.use('/api/v1', infoRouter(engine));
  app.use('/api/v1', ocrRouter(engine, options));
  app.use(errorHandler);
  return app;
}

module.exports = { createApp };
