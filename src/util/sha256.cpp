#include "util/sha256.hpp"

#include <array>
#include <iomanip>
#include <sstream>

namespace light_ocr::internal {
namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
    0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
    0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
    0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

constexpr std::uint32_t rotate_right(std::uint32_t value, std::uint32_t bits) noexcept {
  return (value >> bits) | (value << (32u - bits));
}

class Sha256 {
 public:
  void update(const std::uint8_t* data, std::size_t size) {
    total_bytes_ += size;
    while (size > 0) {
      const std::size_t available = block_.size() - block_size_;
      const std::size_t take = size < available ? size : available;
      for (std::size_t i = 0; i < take; ++i) block_[block_size_ + i] = data[i];
      block_size_ += take;
      data += take;
      size -= take;
      if (block_size_ == block_.size()) {
        transform(block_.data());
        block_size_ = 0;
      }
    }
  }

  std::array<std::uint8_t, 32> finish() {
    const std::uint64_t total_bits = static_cast<std::uint64_t>(total_bytes_) * 8u;
    block_[block_size_++] = 0x80u;
    if (block_size_ > 56) {
      while (block_size_ < 64) block_[block_size_++] = 0;
      transform(block_.data());
      block_size_ = 0;
    }
    while (block_size_ < 56) block_[block_size_++] = 0;
    for (int i = 7; i >= 0; --i) {
      block_[block_size_++] = static_cast<std::uint8_t>(total_bits >> (i * 8));
    }
    transform(block_.data());

    std::array<std::uint8_t, 32> result{};
    for (std::size_t i = 0; i < state_.size(); ++i) {
      result[i * 4] = static_cast<std::uint8_t>(state_[i] >> 24);
      result[i * 4 + 1] = static_cast<std::uint8_t>(state_[i] >> 16);
      result[i * 4 + 2] = static_cast<std::uint8_t>(state_[i] >> 8);
      result[i * 4 + 3] = static_cast<std::uint8_t>(state_[i]);
    }
    return result;
  }

 private:
  void transform(const std::uint8_t* block) {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t i = 0; i < 16; ++i) {
      words[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
                 (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
                 (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
                 static_cast<std::uint32_t>(block[i * 4 + 3]);
    }
    for (std::size_t i = 16; i < words.size(); ++i) {
      const auto s0 = rotate_right(words[i - 15], 7) ^ rotate_right(words[i - 15], 18) ^
                      (words[i - 15] >> 3);
      const auto s1 = rotate_right(words[i - 2], 17) ^ rotate_right(words[i - 2], 19) ^
                      (words[i - 2] >> 10);
      words[i] = words[i - 16] + s0 + words[i - 7] + s1;
    }

    auto a = state_[0];
    auto b = state_[1];
    auto c = state_[2];
    auto d = state_[3];
    auto e = state_[4];
    auto f = state_[5];
    auto g = state_[6];
    auto h = state_[7];
    for (std::size_t i = 0; i < 64; ++i) {
      const auto s1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
      const auto choice = (e & f) ^ ((~e) & g);
      const auto temp1 = h + s1 + choice + kRoundConstants[i] + words[i];
      const auto s0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
      const auto majority = (a & b) ^ (a & c) ^ (b & c);
      const auto temp2 = s0 + majority;
      h = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
  }

  std::array<std::uint32_t, 8> state_ = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u,
                                         0xa54ff53au, 0x510e527fu, 0x9b05688cu,
                                         0x1f83d9abu, 0x5be0cd19u};
  std::array<std::uint8_t, 64> block_{};
  std::size_t block_size_ = 0;
  std::size_t total_bytes_ = 0;
};

}  // namespace

std::string sha256_hex(const std::uint8_t* data, std::size_t size) {
  Sha256 hasher;
  hasher.update(data, size);
  const auto digest = hasher.finish();
  std::ostringstream output;
  output << std::hex << std::setfill('0');
  for (const auto byte : digest) output << std::setw(2) << static_cast<unsigned>(byte);
  return output.str();
}

}  // namespace light_ocr::internal
