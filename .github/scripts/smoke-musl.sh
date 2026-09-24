#!/bin/sh
# Executed inside an alpine:3.22 container by the npm release workflow.
# See build-musl-native.sh for why the musl smoke runs via `docker run`.
#
# Usage: sh smoke-musl.sh <platform-id> <release-version> <runtime-version>
set -eu
platform_id="$1"
release_version="$2"
runtime_version="$3"

apk add --no-cache nodejs npm bash

mkdir -p /work/package-smoke
cd /work/package-smoke
npm init --yes
npm install --offline --ignore-scripts --no-audit --no-fund --package-lock=false \
  "/work/dist/release/arcships-light-ocr-model-ppocrv6-small-0.3.4.tgz" \
  "/work/dist/release/arcships-light-ocr-${platform_id}-${release_version}.tgz" \
  "/work/dist/release/arcships-light-ocr-runtime-${runtime_version}.tgz" \
  "/work/dist/release/arcships-light-ocr-${release_version}.tgz"
cp /work/tools/npm/smoke.cjs .

echo "=== stable Small OCR smoke on ${platform_id} ==="
node smoke.cjs

echo "=== built-in PDF OCR smoke on ${platform_id} ==="
node /work/tools/npm/document-smoke.cjs
