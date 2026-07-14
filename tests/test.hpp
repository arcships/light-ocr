#pragma once

#include <cmath>
#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace light_ocr::test {

struct Case {
  std::string name;
  std::function<void()> function;
};

inline std::vector<Case>& registry() {
  static std::vector<Case> value;
  return value;
}

struct Registrar {
  Registrar(std::string name, std::function<void()> function) {
    registry().push_back(Case{std::move(name), std::move(function)});
  }
};

inline void fail(const char* expression, const char* file, int line,
                 const std::string& detail = {}) {
  std::ostringstream message;
  message << file << ':' << line << ": expectation failed: " << expression;
  if (!detail.empty()) message << " (" << detail << ')';
  throw std::runtime_error(message.str());
}

inline int run_all() {
  std::size_t failed = 0;
  for (const auto& test : registry()) {
    try {
      test.function();
      std::cout << "PASS " << test.name << '\n';
    } catch (const std::exception& exception) {
      ++failed;
      std::cerr << "FAIL " << test.name << ": " << exception.what() << '\n';
    } catch (...) {
      ++failed;
      std::cerr << "FAIL " << test.name << ": unknown exception\n";
    }
  }
  std::cout << (registry().size() - failed) << '/' << registry().size() << " tests passed\n";
  return failed == 0 ? 0 : 1;
}

}  // namespace light_ocr::test

#define LIGHT_OCR_TEST(name)                                                            \
  static void name();                                                                  \
  static ::light_ocr::test::Registrar name##_registrar(#name, &name);                  \
  static void name()

#define EXPECT_TRUE(expression)                                                         \
  do {                                                                                  \
    if (!(expression)) ::light_ocr::test::fail(#expression, __FILE__, __LINE__);        \
  } while (false)

#define EXPECT_FALSE(expression) EXPECT_TRUE(!(expression))

#define EXPECT_EQ(left, right)                                                          \
  do {                                                                                  \
    const auto& light_ocr_left = (left);                                                \
    const auto& light_ocr_right = (right);                                              \
    if (!(light_ocr_left == light_ocr_right)) {                                         \
      ::light_ocr::test::fail(#left " == " #right, __FILE__, __LINE__);                \
    }                                                                                   \
  } while (false)

#define EXPECT_NEAR(left, right, tolerance)                                             \
  do {                                                                                  \
    const auto light_ocr_left = static_cast<double>(left);                              \
    const auto light_ocr_right = static_cast<double>(right);                            \
    if (std::abs(light_ocr_left - light_ocr_right) > static_cast<double>(tolerance)) {   \
      ::light_ocr::test::fail(#left " ~= " #right, __FILE__, __LINE__);                \
    }                                                                                   \
  } while (false)
