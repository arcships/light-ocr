'use strict';

const express = require('express');
const packageJson = require('../../package.json');

function infoRouter(engine) {
  const router = express.Router();
  router.get('/info', (request, response) => {
    response.status(200).json({
      execution: engine.info.execution,
      version: packageJson.version,
    });
  });
  return router;
}

module.exports = infoRouter;
