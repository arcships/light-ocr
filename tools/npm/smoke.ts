import { createEngine, type RawImage } from '@arcships/light-ocr';

declare const image: RawImage;

async function recognize(): Promise<string[]> {
  const engine = await createEngine({ model: 'ppocrv6-small' });
  try {
    const result = await engine.recognize(image, {
      includeDiagnostics: true,
      detectionMaxSide: 960,
    });
    return result.lines.map((line) => line.text);
  } finally {
    await engine.close();
  }
}

void recognize();
