#include <exception>
#include <iostream>

#include "test_framework.hpp"

int main() {
  int failed = 0;
  for (const auto& test_case : camtrak_test::registry()) {
    try {
      test_case.fn();
      std::cout << "[ PASS ] " << test_case.name << "\n";
    } catch (const std::exception& error) {
      std::cout << "[ FAIL ] " << test_case.name << ": " << error.what()
                << "\n";
      ++failed;
    }
  }
  std::cout << camtrak_test::registry().size() - failed << "/"
            << camtrak_test::registry().size() << " test cases passed\n";
  return failed == 0 ? 0 : 1;
}
