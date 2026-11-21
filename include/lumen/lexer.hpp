#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace lumen {

// A position in the source: 1-based line and column.
struct Span {
  int line = 1;
  int col = 1;
};

// A compile-time (lex or parse) error, reported as "line:col: message".
struct CompileError : std::runtime_error {
  CompileError(const std::string& msg, Span at)
      : std::runtime_error(msg), span(at) {}
  Span span;
};

enum class Tok {
  // literals and names
  Int, Float, String, Ident,
  // keywords
  Let, Fn, Return, If, Else, While, For, In, Break, Continue, True, False, Nil, And, Or, Not,
  // punctuation
  LParen, RParen, LBrace, RBrace, LBracket, RBracket, Comma, Semicolon, Colon, Dot,
  Plus, Minus, Star, Slash, Percent,
  Assign, Eq, NotEq, Less, LessEq, Greater, GreaterEq,
  Eof,
};

const char* tok_name(Tok t);

struct Token {
  Tok kind = Tok::Eof;
  std::string text;  // identifier name, or the decoded string literal
  std::int64_t int_value = 0;
  double float_value = 0;
  Span span;
};

// Turns source text into tokens. Throws CompileError on a bad character,
// an unterminated string or comment, or an integer literal that overflows.
std::vector<Token> lex(const std::string& source);

}  // namespace lumen
