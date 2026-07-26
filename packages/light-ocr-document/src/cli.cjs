#!/usr/bin/env node
'use strict';

const { createDocumentEngine, getVersion, hasPdfSupport, OcrError } = require('./index.cjs');

const USAGE = `
light-ocr-document - PDF and multi-page image OCR

Usage:
  light-ocr-document <command> [options]

Commands:
  recognize <source>    Process a PDF or image file(s)
  info                  Show engine and PDF support info
  help                  Show this help

Options:
  --format <type>       Output format: json, jsonl, text, markdown (default: json)
  --pages <range>       Page range, e.g., 1-5 or 3 (PDF only)
  --dpi <n>             PDF raster DPI (default: 150)
  --max-pages <n>       Maximum pages to process (default: 100)
  --max-file-bytes <n>  Maximum file size in bytes (default: 104857600)
  --quiet               Suppress progress output
  --version             Show version

Examples:
  light-ocr-document recognize document.pdf
  light-ocr-document recognize document.pdf --format jsonl --pages 1-10
  light-ocr-document recognize image1.png image2.jpg --format text
  light-ocr-document info
`.trim();

async function main() {
  const args = process.argv.slice(2);
  
  if (args.length === 0 || args.includes('--help') || args.includes('-h') || args.includes('help')) {
    console.log(USAGE);
    process.exit(0);
  }
  
  if (args.includes('--version') || args.includes('-v')) {
    console.log(getVersion());
    process.exit(0);
  }
  
  const command = args[0];
  
  if (command === 'info') {
    console.log(`light-ocr-document v${getVersion()}`);
    console.log(`PDF support: ${hasPdfSupport() ? 'yes' : 'no (install pdfium-native)'}`);
    process.exit(0);
  }
  
  if (command !== 'recognize') {
    console.error(`Unknown command: ${command}`);
    console.error('Run "light-ocr-document help" for usage');
    process.exit(64);
  }
  
  // Parse options
  const options = {};
  const sources = [];
  
  for (let i = 1; i < args.length; i++) {
    const arg = args[i];
    
    if (arg === '--format' && i + 1 < args.length) {
      options.format = args[++i];
    } else if (arg === '--pages' && i + 1 < args.length) {
      const range = args[++i];
      const match = range.match(/^(\d+)(?:-(\d+))?$/);
      if (match) {
        options.pageRange = {
          start: parseInt(match[1]),
          end: match[2] ? parseInt(match[2]) : parseInt(match[1])
        };
      } else {
        console.error(`Invalid page range: ${range}`);
        process.exit(65);
      }
    } else if (arg === '--dpi' && i + 1 < args.length) {
      options.dpi = parseInt(args[++i]);
    } else if (arg === '--max-pages' && i + 1 < args.length) {
      options.maxPages = parseInt(args[++i]);
    } else if (arg === '--max-file-bytes' && i + 1 < args.length) {
      options.maxFileBytes = parseInt(args[++i]);
    } else if (arg === '--quiet') {
      options.quiet = true;
    } else if (!arg.startsWith('-')) {
      sources.push(arg);
    }
  }
  
  if (sources.length === 0) {
    console.error('No source files specified');
    process.exit(65);
  }
  
  // Create engine
  let engine;
  try {
    engine = await createDocumentEngine();
  } catch (err) {
    console.error(`Failed to create engine: ${err.message}`);
    process.exit(70);
  }
  
  try {
    const format = options.format || 'json';
    const pages = [];
    
    // Process documents
    const source = sources.length === 1 ? sources[0] : sources;
    
    for await (const page of engine.recognizeDocument(source, options)) {
      pages.push(page);
      
      // Output JSONL as we go
      if (format === 'jsonl') {
        console.log(JSON.stringify(page));
      }
      
      // Progress output
      if (!options.quiet) {
        process.stderr.write(`\rProcessed page ${pages.length}...`);
      }
    }
    
    if (!options.quiet) {
      process.stderr.write('\n');
    }
    
    // Output final result for non-JSONL formats
    if (format === 'json') {
      const result = {
        schemaVersion: 1,
        source: {
          kind: sources.some(s => s.endsWith('.pdf')) ? 'pdf' : 'page-images',
          mediaType: sources.some(s => s.endsWith('.pdf')) ? 'application/pdf' : 'image/*',
          identity: { files: sources },
          pageCount: pages.length
        },
        pages
      };
      console.log(JSON.stringify(result, null, 2));
    } else if (format === 'text') {
      for (const page of pages) {
        console.log(page.lines.map(l => l.text).join('\n'));
      }
    } else if (format === 'markdown') {
      console.log('# Document OCR Result\n');
      for (const page of pages) {
        console.log(`## Page ${page.index + 1}\n`);
        console.log(page.lines.map(l => l.text).join('\n\n'));
        console.log('');
      }
    }
    
    process.exit(0);
  } catch (err) {
    if (err instanceof OcrError) {
      console.error(`OCR Error: ${err.code} - ${err.message}`);
      if (err.detail) {
        console.error(`Detail: ${err.detail}`);
      }
      
      // Map error codes to exit codes
      const exitCodeMap = {
        'invalid_argument': 65,
        'invalid_image': 66,
        'unsupported_capability': 67,
        'invalid_model_bundle': 68,
        'resource_limit_exceeded': 69,
        'package_load_failed': 70,
        'inference_failed': 71,
        'internal_error': 72
      };
      
      process.exit(exitCodeMap[err.code] || 72);
    }
    
    console.error(`Error: ${err.message}`);
    process.exit(72);
  } finally {
    await engine.close();
  }
}

main().catch(err => {
  console.error(err);
  process.exit(72);
});
