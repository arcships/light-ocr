'use strict';

const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const { performance } = require('node:perf_hooks');

function parseArguments(argv) {
  const values = {};
  for (let index = 0; index < argv.length; index += 2) {
    const name = argv[index];
    if (!name.startsWith('--') || index + 1 >= argv.length) {
      throw new Error(`invalid argument: ${name}`);
    }
    values[name.slice(2)] = argv[index + 1];
  }
  for (const required of ['binary', 'descriptor', 'bundle', 'fixture', 'mode', 'report']) {
    if (!values[required]) throw new Error(`missing --${required}`);
  }
  if (!['auto', 'cpu', 'fp32', 'allow', 'strict'].includes(values.mode)) {
    throw new Error('--mode must be auto, cpu, fp32, allow, or strict');
  }
  values.iterations = Number(values.iterations || 5);
  values.cycles = Number(values.cycles || 1);
  values.warmup = Number(values.warmup || 0);
  if (!Number.isSafeInteger(values.iterations) || values.iterations < 1 ||
      !Number.isSafeInteger(values.cycles) || values.cycles < 1 ||
      !Number.isSafeInteger(values.warmup) || values.warmup < 0) {
    throw new Error('--iterations and --cycles must be positive integers; --warmup must be non-negative');
  }
  return values;
}

function percentile(values, fraction) {
  const sorted = [...values].sort((left, right) => left - right);
  return sorted[Math.min(sorted.length - 1, Math.ceil(sorted.length * fraction) - 1)];
}

function normalizedLines(result) {
  return result.lines.map((line) => ({
    text: line.text,
    confidence: line.confidence,
    box: line.box.map((point) => [point.x, point.y]),
  }));
}

function digest(value) {
  return crypto.createHash('sha256').update(JSON.stringify(value)).digest('hex');
}

function serializeError(error) {
  return {
    name: error?.name || 'Error',
    code: error?.code || 'unknown',
    message: error?.message || String(error),
    detail: error?.detail || '',
    creationTrace: error?.creationTrace,
  };
}

async function main() {
  const arguments_ = parseArguments(process.argv.slice(2));
  process.env.LIGHT_OCR_NODE_BINARY = path.resolve(arguments_.binary);
  process.env.LIGHT_OCR_RUNTIME_DESCRIPTOR = path.resolve(arguments_.descriptor);
  const { createEngine } = require('../js/index.cjs');
  const fixturePath = path.resolve(arguments_.fixture);
  const fixture = JSON.parse(fs.readFileSync(fixturePath, 'utf8'));
  const pixels = fs.readFileSync(path.join(path.dirname(fixturePath), 'pixels.bin'));
  const image = {
    data: pixels,
    width: fixture.width,
    height: fixture.height,
    stride: fixture.stride,
    pixelFormat: fixture.pixelFormat,
  };
  const execution = {
    auto: { provider: 'auto' },
    cpu: { provider: 'cpu', precision: 'fp32' },
    allow: { provider: 'webgpu', cpuPartition: 'allow', precision: 'fp32' },
    strict: { provider: 'webgpu', cpuPartition: 'forbid', precision: 'fp32' },
  }[arguments_.mode];
  const reportPath = path.resolve(arguments_.report);
  const writeReport = (report) => {
    fs.mkdirSync(path.dirname(reportPath), { recursive: true });
    const temporary = `${reportPath}.${process.pid}.tmp`;
    fs.writeFileSync(temporary, `${JSON.stringify(report, null, 2)}\n`);
    fs.renameSync(temporary, reportPath);
    process.stdout.write(`${JSON.stringify(report)}\n`);
  };
  if (arguments_.mode === 'strict') {
    const started = new Date().toISOString();
    let report;
    try {
      const engine = await createEngine({
        bundlePath: path.resolve(arguments_.bundle),
        execution,
      });
      try {
        report = {
          schemaVersion: '1.1',
          ok: false,
          started,
          finished: new Date().toISOString(),
          fixture: fixture.id,
          mode: arguments_.mode,
          expectedRejection: false,
          error: { code: 'strict_profile_unexpectedly_accepted' },
          engine: engine.info,
        };
      } finally {
        await engine.close();
      }
    } catch (error) {
      const serialized = serializeError(error);
      const accepted = (
        serialized.code === 'unsupported_capability' &&
        serialized.message === 'The WebGPU model requires a bounded CPU operator partition' &&
        serialized.detail === 'required operators: Concat, Gather, Slice'
      ) || (
        serialized.code === 'runtime_initialization_failed' &&
        typeof serialized.message === 'string' &&
        serialized.message.includes('fallback to CPU EP has been explicitly disabled')
      );
      report = {
        schemaVersion: '1.1',
        ok: accepted,
        started,
        finished: new Date().toISOString(),
        fixture: fixture.id,
        mode: arguments_.mode,
        expectedRejection: accepted,
        error: serialized,
      };
    }
    writeReport(report);
    if (!report.ok) process.exitCode = 1;
    return;
  }
  const times = [];
  const initializationTimes = [];
  const firstPredictionTimes = [];
  const hashes = [];
  const rss = [];
  let firstLines;
  let firstPredictionUs;
  let engineInfo;
  const started = new Date().toISOString();
  const cpuStart = process.cpuUsage();
  const wallStart = performance.now();
  for (let cycle = 0; cycle < arguments_.cycles; ++cycle) {
    let cycleFirstPredictionUs;
    const initializationBegin = performance.now();
    const engine = await createEngine({
      bundlePath: path.resolve(arguments_.bundle),
      execution,
    });
    initializationTimes.push(Math.round((performance.now() - initializationBegin) * 1000));
    try {
      engineInfo = engine.info;
      for (let warmup = 0; warmup < arguments_.warmup; ++warmup) {
        const begin = performance.now();
        const result = await engine.recognize(image, { includeDiagnostics: true });
        const elapsed = Math.round((performance.now() - begin) * 1000);
        firstPredictionUs ??= elapsed;
        cycleFirstPredictionUs ??= elapsed;
        const lines = normalizedLines(result);
        firstLines ??= lines;
        hashes.push(digest(lines));
        rss.push(process.memoryUsage().rss);
      }
      for (let iteration = 0; iteration < arguments_.iterations; ++iteration) {
        const begin = performance.now();
        const result = await engine.recognize(image, { includeDiagnostics: true });
        const elapsed = Math.round((performance.now() - begin) * 1000);
        firstPredictionUs ??= elapsed;
        cycleFirstPredictionUs ??= elapsed;
        times.push(elapsed);
        const lines = normalizedLines(result);
        firstLines ??= lines;
        hashes.push(digest(lines));
        rss.push(process.memoryUsage().rss);
      }
    } finally {
      await engine.close();
    }
    firstPredictionTimes.push(cycleFirstPredictionUs);
    rss.push(process.memoryUsage().rss);
  }
  const wallUs = Math.round((performance.now() - wallStart) * 1000);
  const cpu = process.cpuUsage(cpuStart);
  const processCpuUs = cpu.user + cpu.system;
  const uniqueHashes = [...new Set(hashes)];
  const processHeader = process.report?.getReport?.().header || {};
  const report = {
    schemaVersion: '1.1',
    ok: uniqueHashes.length === 1,
    started,
    finished: new Date().toISOString(),
    host: {
      platform: process.platform,
      architecture: process.arch,
      node: process.versions.node,
      osName: processHeader.osName,
      osRelease: processHeader.osRelease,
      osVersion: processHeader.osVersion,
      glibcVersionRuntime: processHeader.glibcVersionRuntime,
    },
    fixture: fixture.id,
    mode: arguments_.mode,
    warmup: arguments_.warmup,
    iterations: arguments_.iterations,
    cycles: arguments_.cycles,
    result: {
      sha256: uniqueHashes[0],
      deterministic: uniqueHashes.length === 1,
      lines: firstLines,
    },
    latencyUs: {
      minimum: Math.min(...times),
      p50: percentile(times, 0.5),
      p95: percentile(times, 0.95),
      maximum: Math.max(...times),
    },
    engineInitializationUs: {
      minimum: Math.min(...initializationTimes),
      p50: percentile(initializationTimes, 0.5),
      maximum: Math.max(...initializationTimes),
      values: initializationTimes,
    },
    firstPredictionUs,
    firstPredictionUsByCycle: firstPredictionTimes,
    processCpuUs,
    measuredWallUs: wallUs,
    averageProcessCpuCores: wallUs > 0 ? processCpuUs / wallUs : 0,
    lifecycle: {
      rssBytes: rss,
      residentMinimumBytes: Math.min(...rss),
      residentMaximumBytes: Math.max(...rss),
      residentFinalBytes: rss.at(-1),
      retainedGrowthBytes: rss.length > 1 ? rss.at(-1) - rss[0] : 0,
    },
    engine: engineInfo,
  };
  writeReport(report);
  if (!report.ok) process.exitCode = 1;
}

main().catch((error) => {
  const reportPathIndex = process.argv.indexOf('--report');
  const reportPath = reportPathIndex >= 0 ? path.resolve(process.argv[reportPathIndex + 1]) : undefined;
  const report = {
    schemaVersion: '1.1',
    ok: false,
    error: serializeError(error),
  };
  if (reportPath) {
    fs.mkdirSync(path.dirname(reportPath), { recursive: true });
    fs.writeFileSync(reportPath, `${JSON.stringify(report, null, 2)}\n`);
  }
  process.stderr.write(`${JSON.stringify(report)}\n`);
  process.exitCode = 1;
});
