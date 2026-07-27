import {
  createDocumentEngine,
  recognizeDocument,
  type DocumentPage,
  type RecognizeDocumentOptions,
} from '@arcships/light-ocr-document';

const options: RecognizeDocumentOptions = {
  dpi: 150,
  pageRange: { start: 1, end: 2 },
  maxPages: 2,
  engineOptions: { execution: { provider: 'cpu' } },
};

async function consume(): Promise<void> {
  for await (const page of recognizeDocument('report.pdf', options)) {
    const typed: DocumentPage = page;
    void typed.lines;
  }

  const engine = await createDocumentEngine({
    engineOptions: { execution: { provider: 'auto' } },
  });
  await engine.close();
}

void consume;
