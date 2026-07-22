FROM node:22-trixie-slim

WORKDIR /app

COPY package.json package-lock.json ./
COPY packages/runtime/package.json ./packages/runtime/package.json
COPY packages/light-ocr/package.json ./packages/light-ocr/package.json
COPY packages/light-ocr-server/package.json ./packages/light-ocr-server/package.json

RUN npm ci --omit=dev --ignore-scripts --no-audit --no-fund

COPY packages/runtime/src ./packages/runtime/src
COPY packages/light-ocr/src ./packages/light-ocr/src
COPY packages/light-ocr-server/src ./packages/light-ocr-server/src

RUN groupadd -r ocr && useradd -r -g ocr ocr
USER ocr

EXPOSE 3000
ENV EXECUTION_MODE=cpu
ENV QUEUE_CAPACITY=4

CMD ["node", "packages/light-ocr-server/src/server.js"]
