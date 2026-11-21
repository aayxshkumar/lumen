#include <gtest/gtest.h>

#include <sstream>

#include "lumen/interpreter.hpp"

using namespace lumen;

namespace {

std::string run(const std::string& src) {
  std::ostringstream out;
  Interpreter in(out);
  in.run(parse(src));
  return out.str();
}

std::string runtime_error(const std::string& src) {
  std::ostringstream out;
  Interpreter in(out);
  try {
    in.run(parse(src));
  } catch (const RuntimeError& e) {
    return std::to_string(e.span.line) + ":" + std::to_string(e.span.col) + " " + e.what();
  }
  return "no error";
}

}  // namespace

TEST(Interpreter, ArithmeticAndPrinting) {
  EXPECT_EQ(run("print(7 / 2, -7 / 2, 7 % 3, -7 % 3, 7.0 / 2, 2 * 1.5);"), "3 -3 1 -1 3.5 3.0\n");
  EXPECT_EQ(run("print(1 == 1.0, [1, 2] == [1, 2], {\"a\": 1} == {\"a\": 1.0}, \"a\" < \"b\");"),
            "true true true true\n");
  EXPECT_EQ(run("print([1, \"two\", [3.0]], {\"k\": nil});"), "[1, \"two\", [3.0]] {\"k\": nil}\n");
}

TEST(Interpreter, ClosuresCaptureByReference) {
  EXPECT_EQ(run(R"(
    fn counter() { let n = 0; return fn() { n = n + 1; return n; }; }
    let c = counter();
    c(); c();
    print(c());
  )"), "3\n");
}

TEST(Interpreter, LoopVariableIsFreshEachIteration) {
  EXPECT_EQ(run(R"(
    let fs = [];
    for i in range(3) { push(fs, fn() { return i; }); }
    print(fs[0](), fs[1](), fs[2]());
  )"), "0 1 2\n");
}

TEST(Interpreter, RuntimeErrorsHavePositions) {
  EXPECT_EQ(runtime_error("let x = 1;\nprint(x / 0);"), "2:9 division by zero");
  EXPECT_EQ(runtime_error("print(y);"), "1:7 undefined variable 'y'");
  EXPECT_EQ(runtime_error("let a = [1];\na[5];"), "2:2 index 5 out of range (length 1)");
  EXPECT_EQ(runtime_error("if 1 { }"), "1:4 an 'if' condition must be a bool, got int 1");
  EXPECT_EQ(runtime_error("print(9223372036854775807 + 1);"), "1:27 integer overflow");
}

TEST(Interpreter, DeepRecursionIsAnErrorNotACrash) {
  // Run with a small limit, so this is safe on the default test stack.
  std::ostringstream out;
  Interpreter in(out);
  in.max_depth = 200;
  try {
    in.run(parse("fn f(n) { return f(n + 1); } f(0);"));
    FAIL();
  } catch (const RuntimeError& e) {
    EXPECT_NE(std::string(e.what()).find("stack overflow"), std::string::npos);
    EXPECT_EQ(e.trace.size(), 200u);
  }
}
