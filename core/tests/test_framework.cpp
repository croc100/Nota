#include "test_framework.hpp"

#include <cstdio>

namespace nota_test {

std::vector<TestCase>& Registry() {
  static std::vector<TestCase> registry;
  return registry;
}

void Fail(const char* file, int line, const std::string& message) {
  std::ostringstream os;
  os << file << ":" << line << ": " << message;
  throw Failure{os.str()};
}

int RunAll() {
  int failed = 0;
  int passed = 0;
  for (const TestCase& test : Registry()) {
    try {
      test.fn();
      std::printf("  [ ok ] %s\n", test.name);
      ++passed;
    } catch (const Failure& f) {
      std::printf("  [FAIL] %s\n         %s\n", test.name, f.message.c_str());
      ++failed;
    } catch (const std::exception& e) {
      std::printf("  [FAIL] %s\n         unexpected exception: %s\n", test.name,
                  e.what());
      ++failed;
    }
  }
  std::printf("\n%d passed, %d failed\n", passed, failed);
  return failed == 0 ? 0 : 1;
}

}  // namespace nota_test

int main() { return nota_test::RunAll(); }
