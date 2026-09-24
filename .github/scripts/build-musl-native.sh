#!/bin/sh
# Executed inside an alpine:3.22 container by the npm release workflow.
# GitHub-hosted arm64 runners cannot run JavaScript Actions inside Alpine
# job containers, so the musl builds run via `docker run` with the checkout
# and artifact steps on the host runner.
#
# Usage: sh build-musl-native.sh <platform-id>
set -eu
platform_id="$1"
export PIP_BREAK_SYSTEM_PACKAGES=1

echo "=== [1] musl toolchain ==="
apk add --no-cache nodejs npm python3 py3-pip bash git gcc g++ make cmake \
  ninja patch tar xz unzip linux-headers
git config --global --add safe.directory /src
gcc --version | head -1
node --version

echo "=== [2] bootstrap ($platform_id) ==="
cd /src
python3 tools/bootstrap_dependencies.py \
  --cache-dir .cache/dependencies \
  --platform-id "$platform_id"

echo "=== [3] pdfium-native ==="
mkdir -p .cache/pdfium-package
cd .cache/pdfium-package
npm init --yes
npm install --ignore-scripts --no-audit --no-fund --package-lock=false \
  pdfium-native@0.6.1
python3 /src/tools/pdfium/prepare_renderer.py \
  --pdfium-dir node_modules/pdfium-native \
  --font-cache /src/.cache/pdf-fonts \
  --download \
  --build

echo "=== [4] node headers + cmake configure ==="
cd /src
node_version="$(node -p process.versions.node)"
npx --yes node-gyp@11.4.2 install "$node_version" --devdir "$PWD/.cache/node-gyp"
NODE_INCLUDE_DIR="$PWD/.cache/node-gyp/$node_version/include/node"
export NODE_INCLUDE_DIR

cmake -S /src -B build-npm -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DLIGHT_OCR_DEPENDENCY_CACHE_DIR=/src/.cache/dependencies \
  -DLIGHT_OCR_ONNXRUNTIME_FLAVOR=cpu \
  -DLIGHT_OCR_TARGET_LIBC=musl \
  -DLIGHT_OCR_BUILD_NODE=ON \
  -DLIGHT_OCR_BUILD_TESTS=OFF \
  -DLIGHT_OCR_BUILD_TOOLS=OFF \
  -DLIGHT_OCR_NODE_INCLUDE_DIR="$NODE_INCLUDE_DIR" \
  -DLIGHT_OCR_NODE_EXECUTABLE="$(command -v node)"

echo "=== [5] build + metadata + stage ==="
cmake --build build-npm --config Release --parallel "$(nproc)"
python3 tools/generate_release_metadata.py \
  --build-dir build-npm \
  --output-dir "reports/npm/${platform_id}" \
  --platform-id "$platform_id" \
  --configuration Release \
  --model-free
python3 tools/npm_release.py stage-native \
  --platform-id "$platform_id" \
  --build-dir build-npm \
  --configuration Release \
  --metadata-dir "reports/npm/${platform_id}" \
  --pdfium-dir .cache/pdfium-package/node_modules/pdfium-native \
  --output-dir "dist/native-input/${platform_id}" \
  --runtime-flavor cpu

echo "=== musl native build staged for ${platform_id} ==="
