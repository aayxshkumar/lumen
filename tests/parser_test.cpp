#include <gtest/gtest.h>

#include "lumen/ast.hpp"

using namespace lumen;

namespace {

std::string ast(const std::string& src) { return dump(parse(src)); }

std::string error_of(const std::string& src) {
  try {
    parse(src);
  } catch (const CompileError& e) {
    return std::to_string(e.span.line) + ":" + std::to_string(e.span.col) + " " + e.what();
  }
  return "no error";
}

}  // namespace

TEST(Parser, Precedence) {
  EXPECT_EQ(ast("1 + 2 * 3;"), "(+ 1 (* 2 3))\n");
  EXPECT_EQ(ast("(1 + 2) * 3;"), "(* (+ 1 2) 3)\n");
  EXPECT_EQ(ast("1 - 2 - 3;"), "(- (- 1 2) 3)\n");
  EXPECT_EQ(ast("-a * b;"), "(* (neg a) b)\n");
  EXPECT_EQ(ast("not a == b;"), "(not (== a b))\n");
  EXPECT_EQ(ast("a or b and c;"), "(or a (and b c))\n");
  EXPECT_EQ(ast("a < b + 1 and c;"), "(and (< a (+ b 1)) c)\n");
}

TEST(Parser, AssignmentIsRightAssociative) {
  EXPECT_EQ(ast("a = b = 3;"), "(= a (= b 3))\n");
  EXPECT_EQ(ast("xs[i + 1] = 2;"), "(= (index xs (+ i 1)) 2)\n");
}

TEST(Parser, CallsIndexesAndLiterals) {
  EXPECT_EQ(ast("f(1, g(2))[0];"), "(index (call f 1 (call g 2)) 0)\n");
  EXPECT_EQ(ast("let m = {\"a\": [1, 2,], 3: nil};"), "(let m (map \"a\" (list 1 2) 3 nil))\n");
  EXPECT_EQ(ast("let f = fn(x, y) { return x; };"), "(let f (fn (x y) {(return x)}))\n");
}

TEST(Parser, Statements) {
  EXPECT_EQ(ast("fn f(n) { if n < 2 { return n; } else if n == 2 { return 1; } else { return 0; } }"),
            "(fndecl f (n) {(if (< n 2) {(return n)} {(if (== n 2) {(return 1)} {(return 0)})})})\n");
  EXPECT_EQ(ast("for x in xs { continue; } while true { break; }"),
            "(for x xs {(continue)})\n(while true {(break)})\n");
}

TEST(Parser, BraceAfterConditionIsTheBody) {
  // `if x {` must not try to parse `{` as a map literal.
  EXPECT_EQ(ast("if x { y; }"), "(if x {y})\n");
  EXPECT_EQ(ast("if x == ({}) { y; }"), "(if (== x (map)) {y})\n");
}

TEST(Parser, ErrorsPointAtTheProblem) {
  EXPECT_EQ(error_of("let x = 1"), "1:10 expected ';' after the 'let' value, found end of input");
  EXPECT_EQ(error_of("let = 3;"), "1:5 expected identifier after 'let', found '='");
  EXPECT_EQ(error_of("1 + ;"), "1:5 expected an expression, found ';'");
  EXPECT_EQ(error_of("1 = 2;"), "1:3 can only assign to a variable or an index");
  EXPECT_EQ(error_of("a < b < c;"), "1:7 comparisons can't be chained; use 'and'");
  EXPECT_EQ(error_of("fn f(a, a) {}"), "1:9 duplicate parameter 'a'");
  EXPECT_EQ(error_of("if x {\n  y;\n"), "3:1 expected '}' to close the block, found end of input");
}
