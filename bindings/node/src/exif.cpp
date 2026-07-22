#include "exif.hpp"

#include <cstring>

namespace light_ocr::node {
namespace exif {

namespace {

std::uint16_t read_u16(const std::uint8_t* p, bool little_endian) {
  if (little_endian) return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
  return static_cast<std::uint16_t>((p[0] << 8) | p[1]);
}

std::uint32_t read_u32(const std::uint8_t* p, bool little_endian) {
  if (little_endian) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
  }
  return (static_cast<std::uint32_t>(p[0]) << 24) |
         (static_cast<std::uint32_t>(p[1]) << 16) |
         (static_cast<std::uint32_t>(p[2]) << 8) |
         static_cast<std::uint32_t>(p[3]);
}

}  // namespace

std::uint16_t parse_orientation(const std::vector<std::uint8_t>& encoded) noexcept {
  if (encoded.size() < 4) return 1;
  // JPEG must start with SOI marker 0xFFD8
  if (encoded[0] != 0xff || encoded[1] != 0xd8) return 1;

  std::size_t offset = 2;
  while (offset + 1 < encoded.size()) {
    if (encoded[offset] != 0xff) return 1;
    std::uint16_t marker = encoded[offset + 1];
    offset += 2;

    // SOI, EOI: no payload
    if (marker == 0xd8 || marker == 0xd9) return 1;
    // RSTn, TEM: no payload
    if (marker >= 0xd0 && marker <= 0xd7) continue;
    if (marker == 0x01) continue;
    // SOS: EXIF would be before this
    if (marker == 0xda) return 1;

    // All other markers have a 2-byte length
    if (offset + 1 >= encoded.size()) return 1;
    std::uint16_t length = read_u16(&encoded[offset], true);
    if (length < 2 || offset + length > encoded.size()) return 1;

    // APP1 marker is 0xFFE1
    if (marker == 0xe1) {
      // APP1 data starts after the 2-byte length field
      std::size_t exif_header = offset + 2;
      if (exif_header + 6 > offset + length) {
        offset += length;
        continue;
      }
      // Check "Exif\0\0" header
      static const char kExifMagic[] = {'E', 'x', 'i', 'f', '\0', '\0'};
      if (std::memcmp(&encoded[exif_header], kExifMagic, 6) != 0) {
        offset += length;
        continue;
      }

      // TIFF header starts here
      std::size_t tiff_start = exif_header + 6;
      if (tiff_start + 8 > offset + length) return 1;

      bool le = (encoded[tiff_start] == 0x49 && encoded[tiff_start + 1] == 0x49);
      bool be = (encoded[tiff_start] == 0x4d && encoded[tiff_start + 1] == 0x4d);
      if (!le && !be) return 1;

      // Magic 42
      std::uint16_t magic = read_u16(&encoded[tiff_start + 2], le);
      if (magic != 0x002a) return 1;

      // IFD0 offset from TIFF start
      std::uint32_t ifd_offset = tiff_start + read_u32(&encoded[tiff_start + 4], le);
      if (ifd_offset + 2 > offset + length) return 1;

      std::uint16_t entry_count = read_u16(&encoded[ifd_offset], le);
      for (std::uint16_t i = 0; i < entry_count; ++i) {
        std::size_t entry = ifd_offset + 2 + static_cast<std::size_t>(i) * 12;
        if (entry + 12 > offset + length) break;
        std::uint16_t tag = read_u16(&encoded[entry], le);
        if (tag == 0x0112) {  // Orientation
          std::uint16_t type = read_u16(&encoded[entry + 2], le);
          std::uint32_t count = read_u32(&encoded[entry + 4], le);
          if (type == 3 && count == 1) {  // SHORT
            std::uint16_t value = read_u16(&encoded[entry + 8], le);
            if (value >= 1 && value <= 8) return value;
          }
          return 1;
        }
      }
      return 1;
    }

    offset += length;
  }
  return 1;
}

DecodedImage apply_orientation(DecodedImage img, std::uint16_t orientation) noexcept {
  if (orientation < 1 || orientation > 8 || orientation == 1) return img;

  const std::uint32_t w = img.width;
  const std::uint32_t h = img.height;
  const std::size_t channels = 3;  // rgb8
  const std::size_t src_stride = static_cast<std::size_t>(w) * channels;

  auto make = [](std::uint32_t nw, std::uint32_t nh) {
    DecodedImage out;
    out.width = nw;
    out.height = nh;
    out.stride = static_cast<std::size_t>(nw) * 3;
    out.pixel_format = PixelFormat::rgb8;
    out.bytes.resize(static_cast<std::size_t>(nw) * static_cast<std::size_t>(nh) * 3);
    return out;
  };

  auto get = [&](std::uint32_t x, std::uint32_t y, std::size_t c) -> std::uint8_t {
    return img.bytes[static_cast<std::size_t>(y) * src_stride + x * channels + c];
  };
  auto set = [&](DecodedImage& out, std::uint32_t x, std::uint32_t y, std::size_t c, std::uint8_t v) {
    out.bytes[static_cast<std::size_t>(y) * out.stride + x * channels + c] = v;
  };

  // For orientations 5,6,7,8 width and height swap
  bool swap_dims = (orientation >= 5 && orientation <= 8);
  DecodedImage out = make(swap_dims ? h : w, swap_dims ? w : h);

  for (std::uint32_t y = 0; y < h; ++y) {
    for (std::uint32_t x = 0; x < w; ++x) {
      std::uint32_t nx = x, ny = y;
      switch (orientation) {
        case 2: nx = w - 1 - x; break;                           // flip horizontal
        case 3: nx = w - 1 - x; ny = h - 1 - y; break;            // rotate 180
        case 4: ny = h - 1 - y; break;                            // flip vertical
        case 5: nx = y; ny = x; break;                            // transpose
        case 6: nx = h - 1 - y; ny = x; break;                    // rotate 90 CW
        case 7: nx = h - 1 - y; ny = w - 1 - x; break;            // transverse
        case 8: nx = y; ny = w - 1 - x; break;                    // rotate 90 CCW
      }
      for (std::size_t c = 0; c < channels; ++c) {
        set(out, nx, ny, c, get(x, y, c));
      }
    }
  }
  return out;
}

}  // namespace exif
}  // namespace light_ocr::node
