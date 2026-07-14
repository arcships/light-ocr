import { createEngine, type RawImage } from '@arcships/light-ocr';

declare const image: RawImage;
declare const encodedImage: Uint8Array;

async function recognize(): Promise<string[]> {
  const engine = await createEngine({
    model: 'ppocrv6-small',
    detection: { strategy: 'tiled' },
  });
  try {
    const result = await engine.recognize(image, {
      includeDiagnostics: true,
    });
    await engine.recognizeEncoded(encodedImage);
    return result.lines.map((line) => line.text);
  } finally {
    await engine.close();
  }
}

void recognize();
