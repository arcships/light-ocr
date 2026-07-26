#!/usr/bin/env node
/**
 * CPU Performance Benchmark for light-ocr
 * 
 * Measures cold start, hot start, per-call latency (P50/P95), memory (RSS), and CPU usage
 * across different workload types using the CPU execution provider.
 * 
 * Usage: node tools/cpu-benchmark.mjs [--iterations N] [--warmup N]
 */

import { createEngine } from '@arcships/light-ocr';
import { readFileSync, writeFileSync, mkdirSync, existsSync } from 'node:fs';
import { resolve, join } from 'node:path';
import { cpus, totalmem, platform, arch } from 'node:os';
import { createDeflate } from 'node:zlib';
import { performance } from 'node:perf_hooks';
import { execSync } from 'node:child_process';

// ── CLI args ─────────────────────────────────────────────────────────────────
const args = process.argv.slice(2);
function getArg(name, fallback) {
  const idx = args.indexOf(`--${name}`);
  return idx >= 0 && args[idx + 1] ? Number(args[idx + 1]) : fallback;
}
const ITERATIONS = getArg('iterations', 20);
const WARMUP = getArg('warmup', 3);

// ── Environment info ─────────────────────────────────────────────────────────
function getEnvironment() {
  const cpuInfo = cpus();
  const env = {
    os: `${platform()} ${arch()}`,
    cpuModel: cpuInfo[0]?.model || 'unknown',
    cpuCores: cpuInfo.length,
    cpuPhysicalCores: Math.ceil(cpuInfo.length / 2), // approximate for HT
    totalMemoryMB: Math.round(totalmem() / 1024 / 1024),
    nodeVersion: process.version,
    timestamp: new Date().toISOString(),
  };
  // Try to get more accurate physical core count on Windows
  try {
    const wmic = execSync(
      'powershell -Command "(Get-CimInstance Win32_Processor).NumberOfCores"',
      { encoding: 'utf-8', timeout: 5000 }
    ).trim();
    env.cpuPhysicalCores = parseInt(wmic, 10) || env.cpuPhysicalCores;
  } catch { /* fallback */ }
  return env;
}

// ── Minimal PNG generator (pure Node.js, no deps) ────────────────────────────
function crc32(buf) {
  let crc = 0xffffffff;
  const table = new Int32Array(256);
  for (let i = 0; i < 256; i++) {
    let c = i;
    for (let j = 0; j < 8; j++) c = (c & 1) ? (0xedb88320 ^ (c >>> 1)) : (c >>> 1);
    table[i] = c;
  }
  for (let i = 0; i < buf.length; i++) crc = table[(crc ^ buf[i]) & 0xff] ^ (crc >>> 8);
  return (crc ^ 0xffffffff) >>> 0;
}

function pngChunk(type, data) {
  const len = Buffer.alloc(4);
  len.writeUInt32BE(data.length);
  const typeAndData = Buffer.concat([Buffer.from(type), data]);
  const crc = Buffer.alloc(4);
  crc.writeUInt32BE(crc32(typeAndData));
  return Buffer.concat([len, typeAndData, crc]);
}

async function zlibDeflate(data) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    const stream = createDeflate({ level: 9 });
    stream.on('data', (c) => chunks.push(c));
    stream.on('end', () => resolve(Buffer.concat(chunks)));
    stream.on('error', reject);
    stream.end(data);
  });
}

function createPNG(width, height, rgba) {
  // IHDR
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(width, 0);
  ihdr.writeUInt32BE(height, 4);
  ihdr[8] = 8; // bit depth
  ihdr[9] = 6; // RGBA
  ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;

  // IDAT (filter each row with filter byte 0)
  const rawLen = height * (1 + width * 4);
  const raw = Buffer.alloc(rawLen);
  for (let y = 0; y < height; y++) {
    const rowOffset = y * (1 + width * 4);
    raw[rowOffset] = 0; // filter: none
    rgba.copy(raw, rowOffset + 1, y * width * 4, (y + 1) * width * 4);
  }

  return { ihdr, raw, width, height };
}

async function buildPNGBuffer(width, height, rgba) {
  const { ihdr, raw } = createPNG(width, height, rgba);
  const deflated = await zlibDeflate(raw);
  const signature = Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]);
  const iend = pngChunk('IEND', Buffer.alloc(0));
  return Buffer.concat([
    signature,
    pngChunk('IHDR', ihdr),
    pngChunk('IDAT', deflated),
    iend,
  ]);
}

// ── Test image generators ────────────────────────────────────────────────────

/** Simple image: "Hello 123" on white background (small, ~300x100) */
async function generateSimpleImage() {
  const w = 300, h = 100;
  const rgba = Buffer.alloc(w * h * 4, 255); // white bg
  // Draw "HELLO 123" as simple block letters
  drawText(rgba, w, h, 'HELLO 123', 20, 35);
  return buildPNGBuffer(w, h, rgba);
}

/** Dense text image: many lines of text (800x600) */
async function generateDenseTextImage() {
  const w = 800, h = 600;
  const rgba = Buffer.alloc(w * h * 4, 255); // white bg
  const lines = [
    'The quick brown fox jumps over the lazy dog.',
    'ABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789',
    'Invoice #2024-001 Date: 2024-01-15',
    'Item          Qty    Price   Total',
    'Widget A       10    $2.50   $25.00',
    'Widget B        5    $4.00   $20.00',
    'Service Fee     1   $15.00   $15.00',
    '─────────────────────────────────────',
    'Subtotal                          $60.00',
    'Tax (8%)                           $4.80',
    'Total                             $64.80',
    'Payment: Credit Card **** 4242',
    'Thank you for your purchase!',
    'Reference: TXN-20240115-ABC123',
    'Lorem ipsum dolor sit amet consectetur',
  ];
  let y = 15;
  for (const line of lines) {
    drawText(rgba, w, h, line, 10, y);
    y += 30;
  }
  return buildPNGBuffer(w, h, rgba);
}

/** Large image: 2048x2048 with scattered text */
async function generateLargeImage() {
  const w = 2048, h = 2048;
  const rgba = Buffer.alloc(w * h * 4, 255); // white bg
  // Place text blocks at various positions
  const texts = [
    'DOCUMENT TITLE 2024', 'Section 1: Introduction',
    'This is a comprehensive document.', 'Page 1 of 4',
    'Section 2: Methodology', 'The results show significant improvement.',
    'Section 3: Results', 'P50: 12ms  P95: 45ms',
    'Section 4: Conclusion', 'Further analysis is recommended.',
    'Appendix A: Raw Data', 'Table 1: Performance Metrics',
  ];
  let x = 50, y = 50;
  for (const text of texts) {
    drawText(rgba, w, h, text, x, y);
    y += 80;
    if (y > h - 100) { y = 50; x += 500; }
  }
  return buildPNGBuffer(w, h, rgba);
}

/** Multi-page PDF: uses existing hello-123.png as base (multi-page not directly supported in benchmark) */
function generateMultiPagePDF() {
  // Use existing hello-123.png as a proxy for PDF page testing
  const fixturePath = resolve(
    process.cwd(),
    'packages/light-ocr-server/test/fixtures/hello-123.png'
  );
  return readFileSync(fixturePath);
}

/** Simple block letter drawer (no external deps) */
function drawText(rgba, imgW, imgH, text, startX, startY) {
  // 5x7 bitmap font for ASCII 32-90
  const FONT = {
    ' ': [0x00,0x00,0x00,0x00,0x00,0x00,0x00],
    '!': [0x04,0x04,0x04,0x04,0x04,0x00,0x04],
    '#': [0x0a,0x1f,0x0a,0x0a,0x1f,0x0a,0x00],
    '$': [0x04,0x0f,0x14,0x0e,0x05,0x1e,0x04],
    '%': [0x18,0x19,0x02,0x04,0x08,0x13,0x03],
    '&': [0x0c,0x12,0x14,0x08,0x15,0x12,0x0d],
    '(': [0x02,0x04,0x08,0x08,0x08,0x04,0x02],
    ')': [0x08,0x04,0x02,0x02,0x02,0x04,0x08],
    '*': [0x00,0x04,0x15,0x0e,0x15,0x04,0x00],
    '+': [0x00,0x04,0x04,0x1f,0x04,0x04,0x00],
    ',': [0x00,0x00,0x00,0x00,0x00,0x04,0x08],
    '-': [0x00,0x00,0x00,0x1f,0x00,0x00,0x00],
    '.': [0x00,0x00,0x00,0x00,0x00,0x00,0x04],
    '/': [0x00,0x01,0x02,0x04,0x08,0x10,0x00],
    '0': [0x0e,0x11,0x13,0x15,0x19,0x11,0x0e],
    '1': [0x04,0x0c,0x04,0x04,0x04,0x04,0x0e],
    '2': [0x0e,0x11,0x01,0x02,0x04,0x08,0x1f],
    '3': [0x1f,0x02,0x04,0x02,0x01,0x11,0x0e],
    '4': [0x02,0x06,0x0a,0x12,0x1f,0x02,0x02],
    '5': [0x1f,0x10,0x1e,0x01,0x01,0x11,0x0e],
    '6': [0x06,0x08,0x10,0x1e,0x11,0x11,0x0e],
    '7': [0x1f,0x01,0x02,0x04,0x08,0x08,0x08],
    '8': [0x0e,0x11,0x11,0x0e,0x11,0x11,0x0e],
    '9': [0x0e,0x11,0x11,0x0f,0x01,0x02,0x0c],
    ':': [0x00,0x00,0x04,0x00,0x04,0x00,0x00],
    ';': [0x00,0x00,0x04,0x00,0x04,0x08,0x00],
    '<': [0x02,0x04,0x08,0x10,0x08,0x04,0x02],
    '=': [0x00,0x00,0x1f,0x00,0x1f,0x00,0x00],
    '>': [0x08,0x04,0x02,0x01,0x02,0x04,0x08],
    '?': [0x0e,0x11,0x01,0x02,0x04,0x00,0x04],
    '@': [0x0e,0x11,0x01,0x0d,0x15,0x15,0x0e],
    'A': [0x0e,0x11,0x11,0x1f,0x11,0x11,0x11],
    'B': [0x1e,0x11,0x11,0x1e,0x11,0x11,0x1e],
    'C': [0x0e,0x11,0x10,0x10,0x10,0x11,0x0e],
    'D': [0x1c,0x12,0x11,0x11,0x11,0x12,0x1c],
    'E': [0x1f,0x10,0x10,0x1e,0x10,0x10,0x1f],
    'F': [0x1f,0x10,0x10,0x1e,0x10,0x10,0x10],
    'G': [0x0e,0x11,0x10,0x17,0x11,0x11,0x0f],
    'H': [0x11,0x11,0x11,0x1f,0x11,0x11,0x11],
    'I': [0x0e,0x04,0x04,0x04,0x04,0x04,0x0e],
    'J': [0x07,0x02,0x02,0x02,0x02,0x12,0x0c],
    'K': [0x11,0x12,0x14,0x18,0x14,0x12,0x11],
    'L': [0x10,0x10,0x10,0x10,0x10,0x10,0x1f],
    'M': [0x11,0x1b,0x15,0x15,0x11,0x11,0x11],
    'N': [0x11,0x11,0x19,0x15,0x13,0x11,0x11],
    'O': [0x0e,0x11,0x11,0x11,0x11,0x11,0x0e],
    'P': [0x1e,0x11,0x11,0x1e,0x10,0x10,0x10],
    'Q': [0x0e,0x11,0x11,0x11,0x15,0x12,0x0d],
    'R': [0x1e,0x11,0x11,0x1e,0x14,0x12,0x11],
    'S': [0x0f,0x10,0x10,0x0e,0x01,0x01,0x1e],
    'T': [0x1f,0x04,0x04,0x04,0x04,0x04,0x04],
    'U': [0x11,0x11,0x11,0x11,0x11,0x11,0x0e],
    'V': [0x11,0x11,0x11,0x11,0x0a,0x0a,0x04],
    'W': [0x11,0x11,0x11,0x15,0x15,0x1b,0x11],
    'X': [0x11,0x11,0x0a,0x04,0x0a,0x11,0x11],
    'Y': [0x11,0x11,0x0a,0x04,0x04,0x04,0x04],
    'Z': [0x1f,0x01,0x02,0x04,0x08,0x10,0x1f],
    'a': [0x00,0x00,0x0e,0x01,0x0f,0x11,0x0f],
    'b': [0x10,0x10,0x16,0x19,0x11,0x11,0x1e],
    'c': [0x00,0x00,0x0e,0x10,0x10,0x11,0x0e],
    'd': [0x01,0x01,0x0d,0x13,0x11,0x11,0x0f],
    'e': [0x00,0x00,0x0e,0x11,0x1f,0x10,0x0e],
    'f': [0x06,0x09,0x08,0x1c,0x08,0x08,0x08],
    'g': [0x00,0x0f,0x11,0x11,0x0f,0x01,0x0e],
    'h': [0x10,0x10,0x16,0x19,0x11,0x11,0x11],
    'i': [0x04,0x00,0x0c,0x04,0x04,0x04,0x0e],
    'j': [0x02,0x00,0x06,0x02,0x02,0x12,0x0c],
    'k': [0x10,0x10,0x12,0x14,0x18,0x14,0x12],
    'l': [0x0c,0x04,0x04,0x04,0x04,0x04,0x0e],
    'm': [0x00,0x00,0x1a,0x15,0x15,0x11,0x11],
    'n': [0x00,0x00,0x16,0x19,0x11,0x11,0x11],
    'o': [0x00,0x00,0x0e,0x11,0x11,0x11,0x0e],
    'p': [0x00,0x00,0x1e,0x11,0x1e,0x10,0x10],
    'q': [0x00,0x00,0x0d,0x13,0x0f,0x01,0x01],
    'r': [0x00,0x00,0x16,0x19,0x10,0x10,0x10],
    's': [0x00,0x00,0x0e,0x10,0x0e,0x01,0x1e],
    't': [0x08,0x08,0x1c,0x08,0x08,0x09,0x06],
    'u': [0x00,0x00,0x11,0x11,0x11,0x13,0x0d],
    'v': [0x00,0x00,0x11,0x11,0x11,0x0a,0x04],
    'w': [0x00,0x00,0x11,0x11,0x15,0x15,0x0a],
    'x': [0x00,0x00,0x11,0x0a,0x04,0x0a,0x11],
    'y': [0x00,0x00,0x11,0x11,0x0f,0x01,0x0e],
    'z': [0x00,0x00,0x1f,0x02,0x04,0x08,0x1f],
  };
  const scale = 2;
  let cx = startX;
  for (const ch of text) {
    const glyph = FONT[ch] || FONT['?'];
    for (let row = 0; row < 7; row++) {
      const bits = glyph[row];
      for (let col = 0; col < 5; col++) {
        if (bits & (1 << (4 - col))) {
          for (let dy = 0; dy < scale; dy++) {
            for (let dx = 0; dx < scale; dx++) {
              const px = cx + col * scale + dx;
              const py = startY + row * scale + dy;
              if (px >= 0 && px < imgW && py >= 0 && py < imgH) {
                const off = (py * imgW + px) * 4;
                rgba[off] = 0;
                rgba[off + 1] = 0;
                rgba[off + 2] = 0;
              }
            }
          }
        }
      }
    }
    cx += 6 * scale;
  }
}

// ── Percentile helper ────────────────────────────────────────────────────────
function percentile(sorted, p) {
  const idx = Math.min(sorted.length - 1, Math.ceil(p * sorted.length) - 1);
  return sorted[idx];
}

function distribution(values) {
  const sorted = [...values].sort((a, b) => a - b);
  return {
    min: sorted[0],
    p50: percentile(sorted, 0.50),
    p95: percentile(sorted, 0.95),
    max: sorted[sorted.length - 1],
    mean: Math.round(sorted.reduce((a, b) => a + b, 0) / sorted.length),
  };
}

// ── Memory snapshot ──────────────────────────────────────────────────────────
function memorySnapshot() {
  const usage = process.memoryUsage();
  return {
    rssMB: Math.round(usage.rss / 1024 / 1024),
    heapUsedMB: Math.round(usage.heapUsed / 1024 / 1024),
    heapTotalMB: Math.round(usage.heapTotal / 1024 / 1024),
    externalMB: Math.round(usage.external / 1024 / 1024),
    arrayBuffersMB: Math.round(usage.arrayBuffers / 1024 / 1024),
  };
}

// ── Main benchmark ───────────────────────────────────────────────────────────
async function main() {
  const env = getEnvironment();
  console.log('═══════════════════════════════════════════════════════════════');
  console.log('  light-ocr CPU Performance Benchmark');
  console.log('═══════════════════════════════════════════════════════════════');
  console.log(`  OS:            ${env.os}`);
  console.log(`  CPU:           ${env.cpuModel}`);
  console.log(`  Cores:         ${env.cpuPhysicalCores} physical / ${env.cpuCores} logical`);
  console.log(`  Memory:        ${env.totalMemoryMB} MB`);
  console.log(`  Node.js:       ${env.nodeVersion}`);
  console.log(`  Iterations:    ${ITERATIONS} (warmup: ${WARMUP})`);
  console.log('═══════════════════════════════════════════════════════════════\n');

  // ── Prepare test images ──────────────────────────────────────────────────
  console.log('Preparing test images...');
  const testCases = [
    { name: 'simple', desc: 'Simple image (300x100, "HELLO 123")', image: await generateSimpleImage() },
    { name: 'dense', desc: 'Dense text (800x600, invoice-like)', image: await generateDenseTextImage() },
    { name: 'large', desc: 'Large image (2048x2048, scattered text)', image: await generateLargeImage() },
    { name: 'real', desc: 'Real fixture (hello-123.png)', image: generateMultiPagePDF() },
  ];

  for (const tc of testCases) {
    console.log(`  ✓ ${tc.name}: ${tc.desc} (${(tc.image.length / 1024).toFixed(1)} KB)`);
  }
  console.log();

  // ── Cold start measurement ────────────────────────────────────────────────
  console.log('─── Cold Start (first engine creation + first OCR) ───');
  const memBefore = memorySnapshot();
  const coldStartBegin = performance.now();
  
  const engine = await createEngine({
    execution: { provider: 'cpu', performanceHint: 'latency' },
  });

  const engineReadyTime = performance.now();
  const coldEngineMs = engineReadyTime - coldStartBegin;

  // First OCR call (cold)
  const firstOcr = await engine.recognizeEncoded(testCases[0].image);
  const coldEnd = performance.now();
  const coldFirstOcrMs = coldEnd - engineReadyTime;
  const coldTotalMs = coldEnd - coldStartBegin;

  const memAfterEngine = memorySnapshot();

  console.log(`  Engine creation:   ${coldEngineMs.toFixed(1)} ms`);
  console.log(`  First OCR call:    ${coldFirstOcrMs.toFixed(1)} ms`);
  console.log(`  Total cold start:  ${coldTotalMs.toFixed(1)} ms`);
  console.log(`  RSS after engine:  ${memAfterEngine.rssMB} MB (delta: +${memAfterEngine.rssMB - memBefore.rssMB} MB)`);
  console.log(`  Engine info: provider=${engine.info.execution.requestedProvider}, model=${engine.info.modelBundleId}`);
  console.log();

  // ── Per-workload benchmark ────────────────────────────────────────────────
  const results = {};

  for (const tc of testCases) {
    console.log(`─── ${tc.name}: ${tc.desc} ───`);
    
    // Warmup
    for (let i = 0; i < WARMUP; i++) {
      await engine.recognizeEncoded(tc.image);
    }

    // Benchmark iterations
    const latencies = [];
    const timingsList = [];
    const memSamples = [];
    const cpuUsageStart = process.cpuUsage();

    for (let i = 0; i < ITERATIONS; i++) {
      memSamples.push(memorySnapshot());
      const start = performance.now();
      const result = await engine.recognizeEncoded(tc.image);
      const elapsed = performance.now() - start;
      latencies.push(elapsed);
      timingsList.push(result.timingUs);
    }

    const cpuUsageEnd = process.cpuUsage(cpuUsageStart);
    const latencyDist = distribution(latencies);
    const avgTiming = {};
    const timingKeys = Object.keys(timingsList[0] || {});
    for (const key of timingKeys) {
      const values = timingsList.map(t => t[key]);
      avgTiming[key] = distribution(values);
    }

    const rssValues = memSamples.map(m => m.rssMB);
    const memDist = distribution(rssValues);

    const result = {
      description: tc.desc,
      imageSizeBytes: tc.image.length,
      iterations: ITERATIONS,
      warmup: WARMUP,
      latencyMs: latencyDist,
      timingUs: avgTiming,
      memory: {
        rssMB: memDist,
      },
      cpuTimeMs: {
        user: Math.round(cpuUsageEnd.user / 1000),
        system: Math.round(cpuUsageEnd.system / 1000),
      },
    };
    results[tc.name] = result;

    console.log(`  Latency (ms):  P50=${latencyDist.p50.toFixed(1)}  P95=${latencyDist.p95.toFixed(1)}  mean=${latencyDist.mean.toFixed(1)}  min=${latencyDist.min.toFixed(1)}  max=${latencyDist.max.toFixed(1)}`);
    console.log(`  Timing (μs):   total P50=${avgTiming.total?.p50}  detect_inf=${avgTiming.detectionInference?.p50}  recog_inf=${avgTiming.recognitionInference?.p50}`);
    console.log(`  Memory RSS:    mean=${memDist.mean} MB  max=${memDist.max} MB`);
    console.log(`  CPU time:      user=${result.cpuTimeMs.user} ms  system=${result.cpuTimeMs.system} ms`);
    console.log();
  }

  // ── Warm start (post-warmup single call) ──────────────────────────────────
  console.log('─── Warm Start (engine already loaded, single call) ───');
  // engine is already warm from the benchmarks above
  const warmStart = performance.now();
  await engine.recognizeEncoded(testCases[0].image);
  const warmMs = performance.now() - warmStart;
  console.log(`  Single OCR latency (warm): ${warmMs.toFixed(1)} ms`);
  console.log();

  // ── Final memory ──────────────────────────────────────────────────────────
  const finalMem = memorySnapshot();
  console.log('─── Final Memory ───');
  console.log(`  RSS:           ${finalMem.rssMB} MB`);
  console.log(`  Heap used:     ${finalMem.heapUsedMB} MB`);
  console.log(`  Heap total:    ${finalMem.heapTotalMB} MB`);
  console.log(`  External:      ${finalMem.externalMB} MB`);
  console.log();

  // ── Cleanup ───────────────────────────────────────────────────────────────
  await engine.close();

  // ── Generate report ───────────────────────────────────────────────────────
  const report = {
    schemaVersion: '1.0',
    benchmark: 'cpu-performance-baseline',
    environment: env,
    config: {
      executionProvider: 'cpu',
      model: 'ppocrv6-small',
      iterations: ITERATIONS,
      warmup: WARMUP,
    },
    coldStart: {
      engineCreationMs: Math.round(coldEngineMs),
      firstOcrMs: Math.round(coldFirstOcrMs),
      totalColdMs: Math.round(coldTotalMs),
      rssAfterEngineMB: memAfterEngine.rssMB,
      rssDeltaMB: memAfterEngine.rssMB - memBefore.rssMB,
    },
    warmStart: {
      singleCallMs: Math.round(warmMs),
    },
    workloads: results,
    finalMemory: finalMem,
    generatedAt: new Date().toISOString(),
  };

  const reportPath = join(process.cwd(), 'reports', 'cpu-baseline.json');
  const reportDir = join(process.cwd(), 'reports');
  if (!existsSync(reportDir)) mkdirSync(reportDir, { recursive: true });
  writeFileSync(reportPath, JSON.stringify(report, null, 2), 'utf-8');
  console.log(`Report saved to: ${reportPath}`);

  // ── Print summary table ───────────────────────────────────────────────────
  console.log('\n═══════════════════════════════════════════════════════════════');
  console.log('  Summary');
  console.log('═══════════════════════════════════════════════════════════════');
  console.log(`  Cold start (engine + 1st OCR):  ${coldTotalMs.toFixed(0)} ms`);
  console.log(`  Warm start (single call):       ${warmMs.toFixed(1)} ms`);
  console.log('');
  console.log('  Workload Latency (ms):');
  console.log('  ┌────────────┬──────────┬──────────┬──────────┐');
  console.log('  │ Workload   │   P50    │   P95    │   Mean   │');
  console.log('  ├────────────┼──────────┼──────────┼──────────┤');
  for (const [name, r] of Object.entries(results)) {
    const pad = (s, n) => String(s).padStart(n);
    console.log(`  │ ${name.padEnd(10)} │ ${pad(r.latencyMs.p50.toFixed(1), 8)} │ ${pad(r.latencyMs.p95.toFixed(1), 8)} │ ${pad(r.latencyMs.mean.toFixed(1), 8)} │`);
  }
  console.log('  └────────────┴──────────┴──────────┴──────────┘');
  console.log(`\n  Peak RSS: ${finalMem.rssMB} MB`);
  console.log('═══════════════════════════════════════════════════════════════');
}

main().catch((err) => {
  console.error('Benchmark failed:', err);
  process.exit(1);
});
