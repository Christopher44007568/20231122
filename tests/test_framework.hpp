#pragma once

#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace camtrak_test {

struct Case {
  std::string name;
  std::function<void()> fn;
};

inline std::vector<Case>& registry() {
  static std::vector<Case> cases;
  return cases;
}

struct Registrar {
  Registrar(const std::string& name, std::function<void()> fn) {
    registry().push_back(Case{name, std::move(fn)});
  }
};

inline void check(bool condition, const std::string& message,
                  const char* file, int line) {
  if (!condition) {
    throw std::runtime_error(std::string(file) + ":" + std::to_string(line) +
                             " assertion failed: " + message);
  }
}

inline bool near(double a, double b, double epsilon = 1e-9) {
  return std::abs(a - b) <= epsilon;
}

}  // namespace camtrak_test

#define TEST_CASE(name)                                                  \
  static void name();                                                    \
  static camtrak_test::Registrar registrar_##name(#name, name);          \
  static void name()

#define CHECK(condition) \
  camtrak_test::check((condition), #condition, __FILE__, __LINE__)
#define CHECK_NEAR(a, b) \
  camtrak_test::check(camtrak_test::near((a), (b)), #a " ~= " #b, \
                      __FILE__, __LINE__)
