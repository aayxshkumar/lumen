#include <gtest/gtest.h>

#include "lumen/lexer.hpp"

using namespace lumen;

namespace {

std::vector<Tok> kinds(const std::string& s) {
  std::vector<Tok> out;
  for (const auto& t : lex(s)) out.push_back(t.kind);
  return out;
}

}  // namespace

TEST(Lexer, KeywordsOperatorsAndPunctuation) {
  EXPECT_EQ(kinds("let x = fn(a) { return a <= 2 and not b != c; }"),
            (std::vector<Tok>{Tok::Let, Tok::Ident, Tok::Assign, Tok::Fn, Tok::LParen, Tok::Ident, Tok::RParen,
                              Tok::LBrace, Tok::Return, Tok::Ident, Tok::LessEq, Tok::Int, Tok::And, Tok::Not,
                              Tok::Ident, Tok::NotEq, Tok::Ident, Tok::Semicolon, Tok::RBrace, Tok::Eof}));
}

TEST(Lexer, Numbers) {
  auto t = lex("42 1_000_000 3.25 1e3 2.5e-2 7.method");
  EXPECT_EQ(t[0].int_value, 42);
  EXPECT_EQ(t[1].int_value, 1000000);
  EXPECT_EQ(t[2].kind, Tok::Float);
  EXPECT_DOUBLE_EQ(t[2].float_value, 3.25);
  EXPECT_DOUBLE_EQ(t[3].float_value, 1000.0);
  EXPECT_DOUBLE_EQ(t[4].float_value, 0.025);
  EXPECT_EQ(t[5].kind, Tok::Int);  // "7." followed by a name is int then dot
  EXPECT_EQ(t[6].kind, Tok::Dot);
  EXPECT_THROW(lex("99999999999999999999"), CompileError);
}

TEST(Lexer, StringsAndEscapes) {
  auto t = lex(R"("a\n\"b\"\\")");
  ASSERT_EQ(t[0].kind, Tok::String);
  EXPECT_EQ(t[0].text, "a\n\"b\"\\");
  EXPECT_THROW(lex("\"abc"), CompileError);
  EXPECT_THROW(lex(R"("\q")"), CompileError);
}

TEST(Lexer, CommentsAndPositions) {
  auto t = lex("// line\n  /* block\n comment */ x");
  ASSERT_EQ(t[0].kind, Tok::Ident);
  EXPECT_EQ(t[0].span.line, 3);
  EXPECT_EQ(t[0].span.col, 13);
  EXPECT_THROW(lex("/* never closed"), CompileError);
}

TEST(Lexer, BangAloneIsAnError) {
  try {
    lex("x = !y");
    FAIL();
  } catch (const CompileError& e) {
    EXPECT_EQ(e.span.col, 5);
    EXPECT_NE(std::string(e.what()).find("not"), std::string::npos);
  }
}
