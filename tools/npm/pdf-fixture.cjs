'use strict';

function buildPdf(objects) {
  let body = '%PDF-1.4\n';
  const offsets = [0];
  for (let index = 0; index < objects.length; index++) {
    offsets.push(Buffer.byteLength(body));
    body += `${index + 1} 0 obj\n${objects[index]}\nendobj\n`;
  }
  const xrefOffset = Buffer.byteLength(body);
  body += `xref\n0 ${objects.length + 1}\n`;
  body += '0000000000 65535 f \n';
  for (const offset of offsets.slice(1)) {
    body += `${String(offset).padStart(10, '0')} 00000 n \n`;
  }
  body += `trailer\n<< /Size ${objects.length + 1} /Root 1 0 R >>\n`;
  body += `startxref\n${xrefOffset}\n%%EOF\n`;
  return Buffer.from(body);
}

function pageObjects(content, font, width, height) {
  return [
    '<< /Type /Catalog /Pages 2 0 R >>',
    '<< /Type /Pages /Kids [3 0 R] /Count 1 >>',
    `<< /Type /Page /Parent 2 0 R /MediaBox [0 0 ${width} ${height}] `
      + '/Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>',
    `<< /Length ${Buffer.byteLength(content)} >>\nstream\n${content}\nendstream`,
    font,
  ];
}

function createTextPdf(text = 'HELLO 123', width = 300, height = 100) {
  const escapedText = String(text)
    .replaceAll('\\', '\\\\')
    .replaceAll('(', '\\(')
    .replaceAll(')', '\\)');
  const content = `BT /F1 24 Tf 20 45 Td (${escapedText}) Tj ET`;
  return buildPdf(pageObjects(
    content,
    '<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>',
    width,
    height,
  ));
}

function createNonEmbeddedCjkPdf({
  fontName = 'STSong-Light',
  textHex = '4E2D65876D4B8BD5',
  width = 300,
  height = 100,
} = {}) {
  if (!/^[A-Za-z0-9-]+$/.test(fontName)) {
    throw new TypeError('fontName must be a PDF name');
  }
  if (!/^(?:[0-9A-Fa-f]{4})+$/.test(textHex)) {
    throw new TypeError('textHex must contain UTF-16BE code units');
  }
  const descriptor = '<< /Type /FontDescriptor '
    + `/FontName /${fontName} /Flags 6 /FontBBox [-25 -254 1000 880] `
    + '/ItalicAngle 0 /Ascent 752 /Descent -271 /CapHeight 737 '
    + '/StemV 58 /MissingWidth 500 >>';
  const descendant = '<< /Type /Font /Subtype /CIDFontType0 '
    + `/BaseFont /${fontName} `
    + '/CIDSystemInfo << /Registry (Adobe) /Ordering (GB1) /Supplement 0 >> '
    + `/DW 1000 /FontDescriptor ${descriptor} >>`;
  const font = '<< /Type /Font /Subtype /Type0 '
    + `/BaseFont /${fontName} /Encoding /UniGB-UCS2-H `
    + `/DescendantFonts [${descendant}] >>`;
  const content = `BT /F1 36 Tf 30 35 Td <${textHex.toUpperCase()}> Tj ET`;
  return buildPdf(pageObjects(content, font, width, height));
}

module.exports = { createNonEmbeddedCjkPdf, createTextPdf };
