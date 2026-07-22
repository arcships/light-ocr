'use strict';

const assert = require('node:assert/strict');
const { spawnSync } = require('node:child_process');
const fs = require('node:fs');
const { createRequire } = require('node:module');
const path = require('node:path');

async function main() {
  const [packageName, commandName, expectedBundleId] = process.argv.slice(2);
  assert.ok(packageName && commandName && expectedBundleId, 'package, command, and bundle are required');
  const fixtureDirectory = process.env.LIGHT_OCR_SMOKE_FIXTURE;
  assert.ok(fixtureDirectory, 'LIGHT_OCR_SMOKE_FIXTURE is required');
  const metadata = JSON.parse(
    fs.readFileSync(path.join(fixtureDirectory, 'fixture.json'), 'utf8'),
  );
  const pixels = fs.readFileSync(path.join(fixtureDirectory, 'pixels.bin'));
  const consumerRequire = createRequire(path.join(process.cwd(), 'package.json'));
  const facade = consumerRequire(packageName);
  const packageRoot = path.dirname(path.dirname(consumerRequire.resolve(packageName)));
  let cli = path.resolve(
    packageRoot,
    '..',
    '..',
    '.bin',
    process.platform === 'win32' ? `${commandName}.cmd` : commandName,
  );
  if (!fs.existsSync(cli)) {
    cli = path.resolve(
      process.cwd(),
      'node_modules',
      '.bin',
      process.platform === 'win32' ? `${commandName}.cmd` : commandName,
    );
  }
  const version = spawnSync(cli, ['info', '--version'], {
    encoding: 'utf8',
    shell: process.platform === 'win32',
  });
  assert.equal(version.status, 0, version.stderr || version.error?.message);
  const versionInfo = JSON.parse(version.stdout);
  assert.equal(versionInfo.model, expectedBundleId);
  assert.equal(versionInfo.tier, facade.modelProfile.tier);
  assert.equal(versionInfo.maturity, 'preview');

  const engine = await facade.createEngine({ execution: { provider: 'cpu' } });
  try {
    assert.equal(engine.info.modelBundleId, expectedBundleId);
    const result = await engine.recognize({
      data: pixels,
      width: metadata.width,
      height: metadata.height,
      stride: metadata.stride,
      pixelFormat: metadata.pixelFormat,
    });
    assert.deepEqual(result.lines.map((line) => line.text), ['HELLO 123']);
  } finally {
    await engine.close();
  }
  process.stdout.write(`${JSON.stringify({ ok: true, package: packageName })}\n`);
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
