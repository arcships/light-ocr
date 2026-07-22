# @arcships/light-ocr-server

Preview HTTP API for [`@arcships/light-ocr`](https://github.com/arcships/light-ocr). It runs the default PP-OCRv6 Small engine behind a bounded multipart upload endpoint and can be started directly or as a Docker container.

This package is private while its HTTP contract is evaluated. It is not part of the `0.3.x` npm release line yet.

## Run locally

From the monorepo root:

```bash
npm install
npm start --workspace @arcships/light-ocr-server
```

The server listens on `PORT` (default `3000`).

## Run with Docker

From the monorepo root:

```bash
docker build -f packages/light-ocr-server/Dockerfile -t light-ocr-server .
docker run --rm -p 3000:3000 light-ocr-server
```

Or run `docker compose up --build`. Native packages are available for both Linux x64 and Linux arm64; no Docker platform override is required.

## API

All endpoints use the `/api/v1` prefix.

- `GET /api/v1/health` returns `{ "status": "ok" }`.
- `GET /api/v1/info` returns the server version and engine execution information.
- `POST /api/v1/ocr` accepts `multipart/form-data` with an `image` file field (JPEG or PNG, at most 20 MiB).

```bash
curl -F "image=@sample.jpg" http://localhost:3000/api/v1/ocr
```

Successful OCR returns `{ "lines": [...] }`. Stable engine failures retain their `OcrError.code` in the JSON `error` field and are mapped to an HTTP status by category.

## Configuration

| Variable | Default | Description |
| --- | --- | --- |
| `PORT` | `3000` | TCP port, from 1 through 65535 |
| `EXECUTION_MODE` | `cpu` | `auto`, `cpu`, `apple`, or `webgpu` |
| `QUEUE_CAPACITY` | `4` | Engine queue and concurrent upload limit, from 1 through 64 |

The HTTP concurrency check rejects excess uploads with `429` before Multer buffers their bodies. The native engine remains the final authority for image and resource validation.

## Test

```bash
npm test --workspace @arcships/light-ocr-server
```

The suite covers routing, upload backpressure, error mapping, shutdown ordering, configuration, and one real OCR fixture.

## Provenance

The server originated in [arcships/light-ocr#24](https://github.com/arcships/light-ocr/pull/24) by [chatre7](https://github.com/chatre7), then continued in [`chatre7/light-ocr-server`](https://github.com/chatre7/light-ocr-server) while the main repository prepared its N2 monorepo. It was migrated back when that prerequisite was met, retaining the later upload-bound and graceful-shutdown fixes.
