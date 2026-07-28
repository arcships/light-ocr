#pragma once

#include <cstdlib>
#include <string>

#include "fpdfview.h"

namespace light_ocr_pdfium {

// PDFium's default font discovery is host-dependent. The release loader sets
// this process-local value to the immutable fonts directory beside pdfium.node
// before the addon is loaded.
inline std::string bundled_font_directory;
inline const char* bundled_font_paths[2] = {nullptr, nullptr};

inline void InitializeWithBundledFonts() {
  const char* directory = std::getenv("LIGHT_OCR_PDFIUM_FONT_DIR");
  if (directory == nullptr || directory[0] == '\0') {
    FPDF_InitLibrary();
    return;
  }

  bundled_font_directory = directory;
  bundled_font_paths[0] = bundled_font_directory.c_str();

  FPDF_LIBRARY_CONFIG config{};
  config.version = 2;
  config.m_pUserFontPaths = bundled_font_paths;
  config.m_pIsolate = nullptr;
  config.m_v8EmbedderSlot = 0;
  FPDF_InitLibraryWithConfig(&config);
}

}  // namespace light_ocr_pdfium
