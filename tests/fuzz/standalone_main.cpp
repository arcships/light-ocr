#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

namespace {

std::uint64_t next_random(std::uint64_t& state) {
  state ^= state << 13;
  state ^= state >> 7;
  state ^= state << 17;
  return state;
}

std::size_t parse_option(std::string_view argument, std::string_view prefix,
                         std::size_t fallback) {
  if (argument.substr(0, prefix.size()) != prefix) return fallback;
  std::size_t value = 0;
  const auto text = argument.substr(prefix.size());
  const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
  if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) return fallback;
  return value;
}

}  // namespace

int main(int argc, char** argv) {
  std::size_t runs = 1000;
  std::size_t max_length = 4096;
  std::size_t seed = 0x4c4f4352u;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    runs = parse_option(argument, "-runs=", runs);
    max_length = parse_option(argument, "-max_len=", max_length);
    seed = parse_option(argument, "-seed=", seed);
  }
  max_length = std::max<std::size_t>(max_length, 1);

  std::uint64_t state = seed == 0 ? 1 : seed;
  std::vector<std::uint8_t> input;
  for (std::size_t run = 0; run < runs; ++run) {
    const auto size = static_cast<std::size_t>(next_random(state) % max_length) + 1;
    input.resize(size);
    for (auto& byte : input) byte = static_cast<std::uint8_t>(next_random(state));
    (void)LLVMFuzzerTestOneInput(input.data(), input.size());
  }
  std::cout << "standalone fuzz smoke completed: runs=" << runs
            << " seed=" << seed << " max_len=" << max_length << '\n';
  return 0;
}
