#pragma once

#include <cstddef>
#include <limits>
#include <type_traits>

namespace light_ocr::internal {

template <class T>
bool checked_add(T a, T b, T* out) noexcept {
  static_assert(std::is_unsigned<T>::value, "checked arithmetic requires unsigned types");
  if (b > std::numeric_limits<T>::max() - a) return false;
  *out = static_cast<T>(a + b);
  return true;
}

template <class T>
bool checked_mul(T a, T b, T* out) noexcept {
  static_assert(std::is_unsigned<T>::value, "checked arithmetic requires unsigned types");
  if (a != 0 && b > std::numeric_limits<T>::max() / a) return false;
  *out = static_cast<T>(a * b);
  return true;
}

inline bool checked_image_bytes(std::size_t height, std::size_t stride,
                                std::size_t row_bytes, std::size_t* out) noexcept {
  if (height == 0) return false;
  std::size_t preceding = 0;
  return checked_mul(height - 1, stride, &preceding) &&
         checked_add(preceding, row_bytes, out);
}

}  // namespace light_ocr::internal
