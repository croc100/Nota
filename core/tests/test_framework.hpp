// 최소 테스트 하네스.
//
// GoogleTest를 쓰지 않는 이유: 이 저장소의 핵심 이점은 코어를 네트워크·IDE 없이
// 어디서나 빌드·검증할 수 있다는 것이다(docs/DESIGN.md 9.3). FetchContent로 외부
// 의존성을 끌어오면 그 성질이 약해진다. 필요한 기능이 CHECK 몇 개뿐이므로
// 60줄짜리 하네스로 충분하다. 테스트 규모가 커지면 GoogleTest로 교체한다.
#pragma once

#include <cmath>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace nota_test {

struct TestCase {
  const char* name;
  std::function<void()> fn;
};

std::vector<TestCase>& Registry();
int RunAll();

// 실패는 예외로 전달한다. 하나가 실패해도 나머지 테스트는 계속 돌린다.
struct Failure {
  std::string message;
};

[[noreturn]] void Fail(const char* file, int line, const std::string& message);

struct Registrar {
  Registrar(const char* name, std::function<void()> fn) {
    Registry().push_back({name, std::move(fn)});
  }
};

}  // namespace nota_test

#define NOTA_TEST(test_name)                                             \
  static void test_name();                                               \
  static ::nota_test::Registrar registrar_##test_name(#test_name,        \
                                                      test_name);        \
  static void test_name()

#define CHECK(cond)                                                      \
  do {                                                                   \
    if (!(cond)) ::nota_test::Fail(__FILE__, __LINE__, "CHECK failed: " #cond); \
  } while (0)

#define CHECK_EQ(a, b)                                                   \
  do {                                                                   \
    const auto nota_lhs = (a);                                           \
    const auto nota_rhs = (b);                                           \
    if (!(nota_lhs == nota_rhs)) {                                       \
      std::ostringstream os;                                             \
      os << "CHECK_EQ failed: " #a " == " #b " (" << nota_lhs << " vs "  \
         << nota_rhs << ")";                                             \
      ::nota_test::Fail(__FILE__, __LINE__, os.str());                   \
    }                                                                    \
  } while (0)

#define CHECK_NEAR(a, b, tol)                                            \
  do {                                                                   \
    const double nota_lhs = static_cast<double>(a);                      \
    const double nota_rhs = static_cast<double>(b);                      \
    if (std::fabs(nota_lhs - nota_rhs) > static_cast<double>(tol)) {     \
      std::ostringstream os;                                             \
      os << "CHECK_NEAR failed: " #a " ~= " #b " (" << nota_lhs          \
         << " vs " << nota_rhs << ", tol " << (tol) << ")";              \
      ::nota_test::Fail(__FILE__, __LINE__, os.str());                   \
    }                                                                    \
  } while (0)
