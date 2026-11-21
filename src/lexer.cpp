#include "lumen/lexer.hpp"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <unordered_map>

namespace lumen {

const char* tok_name(Tok t) {
  switch (t) {
    case Tok::Int: return "integer";
    case Tok::Float: return "float";
    case Tok::String: return "string";
    case Tok::Ident: return "identifier";
    case Tok::Let: return "'let'";
    case Tok::Fn: return "'fn'";
    case Tok::Return: return "'return'";
    case Tok::If: return "'if'";
    case Tok::Else: return "'else'";
    case Tok::While: return "'while'";
    case Tok::For: return "'for'";
    case Tok::In: return "'in'";
    case Tok::Break: return "'break'";
    case Tok::Continue: return "'continue'";
    case Tok::True: return "'true'";
    case Tok::False: return "'false'";
    case Tok::Nil: return "'nil'";
    case Tok::And: return "'and'";
    case Tok::Or: return "'or'";
    case Tok::Not: return "'not'";
    case Tok::LParen: return "'('";
    case Tok::RParen: return "')'";
    case Tok::LBrace: return "'{'";
    case Tok::RBrace: return "'}'";
    case Tok::LBracket: return "'['";
    case Tok::RBracket: return "']'";
    case Tok::Comma: return "','";
    case Tok::Semicolon: return "';'";
    case Tok::Colon: return "':'";
    case Tok::Dot: return "'.'";
    case Tok::Plus: return "'+'";
    case Tok::Minus: return "'-'";
    case Tok::Star: return "'*'";
    case Tok::Slash: return "'/'";
    case Tok::Percent: return "'%'";
    case Tok::Assign: return "'='";
    case Tok::Eq: return "'=='";
    case Tok::NotEq: return "'!='";
    case Tok::Less: return "'<'";
    case Tok::LessEq: return "'<='";
    case Tok::Greater: return "'>'";
    case Tok::GreaterEq: return "'>='";
    case Tok::Eof: return "end of input";
  }
  return "?";
}

namespace {

const std::unordered_map<std::string, Tok>& keywords() {
  static const std::unordered_map<std::string, Tok> k = {
      {"let", Tok::Let},         {"fn", Tok::Fn},       {"return", Tok::Return},
      {"if", Tok::If},           {"else", Tok::Else},   {"while", Tok::While},
      {"for", Tok::For},         {"in", Tok::In},       {"break", Tok::Break},
      {"continue", Tok::Continue}, {"true", Tok::True}, {"false", Tok::False},
      {"nil", Tok::Nil},         {"and", Tok::And},     {"or", Tok::Or},
      {"not", Tok::Not},
  };
  return k;
}

class Lexer {
 public:
  explicit Lexer(const std::string& s) : s_(s) {}

  std::vector<Token> run() {
    std::vector<Token> out;
    while (true) {
      skip_space_and_comments();
      Token t;
      t.span = here();
      if (at_end()) {
        t.kind = Tok::Eof;
        out.push_back(t);
        return out;
      }
      const char c = advance();
      if (std::isdigit(static_cast<unsigned char>(c))) {
        number(t, c);
      } else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
        std::string name(1, c);
        while (!at_end() && (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_'))
          name += advance();
        auto it = keywords().find(name);
        t.kind = it == keywords().end() ? Tok::Ident : it->second;
        t.text = std::move(name);
      } else if (c == '"') {
        string_literal(t);
      } else {
        t.kind = punct(c, t.span);
      }
      out.push_back(std::move(t));
    }
  }

 private:
  bool at_end() const { return i_ >= s_.size(); }
  char peek(std::size_t ahead = 0) const { return i_ + ahead < s_.size() ? s_[i_ + ahead] : '\0'; }
  Span here() const { return Span{line_, col_}; }
  char advance() {
    const char c = s_[i_++];
    if (c == '\n') {
      ++line_;
      col_ = 1;
    } else {
      ++col_;
    }
    return c;
  }
  bool match(char c) {
    if (peek() != c) return false;
    advance();
    return true;
  }

  void skip_space_and_comments() {
    while (!at_end()) {
      const char c = peek();
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
        advance();
      } else if (c == '/' && peek(1) == '/') {
        while (!at_end() && peek() != '\n') advance();
      } else if (c == '/' && peek(1) == '*') {
        const Span start = here();
        advance();
        advance();
        while (!(peek() == '*' && peek(1) == '/')) {
          if (at_end()) throw CompileError("unterminated block comment", start);
          advance();
        }
        advance();
        advance();
      } else {
        return;
      }
    }
  }

  void number(Token& t, char first) {
    std::string digits(1, first);
    while (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '_') {
      const char c = advance();
      if (c != '_') digits += c;
    }
    bool is_float = false;
    if (peek() == '.' && std::isdigit(static_cast<unsigned char>(peek(1)))) {
      is_float = true;
      digits += advance();
      while (std::isdigit(static_cast<unsigned char>(peek()))) digits += advance();
    }
    if (peek() == 'e' || peek() == 'E') {
      const char sign = peek(1);
      const bool ok = std::isdigit(static_cast<unsigned char>(sign)) ||
                      ((sign == '+' || sign == '-') && std::isdigit(static_cast<unsigned char>(peek(2))));
      if (ok) {
        is_float = true;
        digits += advance();
        if (peek() == '+' || peek() == '-') digits += advance();
        while (std::isdigit(static_cast<unsigned char>(peek()))) digits += advance();
      }
    }
    if (is_float) {
      t.kind = Tok::Float;
      t.float_value = std::strtod(digits.c_str(), nullptr);
      return;
    }
    errno = 0;
    const long long v = std::strtoll(digits.c_str(), nullptr, 10);
    if (errno == ERANGE) throw CompileError("integer literal " + digits + " is too large", t.span);
    t.kind = Tok::Int;
    t.int_value = v;
  }

  void string_literal(Token& t) {
    t.kind = Tok::String;
    while (true) {
      if (at_end() || peek() == '\n') throw CompileError("unterminated string", t.span);
      const char c = advance();
      if (c == '"') return;
      if (c != '\\') {
        t.text += c;
        continue;
      }
      const Span esc = here();
      if (at_end()) throw CompileError("unterminated string", t.span);
      const char e = advance();
      switch (e) {
        case 'n': t.text += '\n'; break;
        case 't': t.text += '\t'; break;
        case 'r': t.text += '\r'; break;
        case '0': t.text += '\0'; break;
        case '\\': t.text += '\\'; break;
        case '"': t.text += '"'; break;
        default:
          throw CompileError(std::string("unknown escape \\") + e, Span{esc.line, esc.col - 1});
      }
    }
  }

  Tok punct(char c, Span at) {
    switch (c) {
      case '(': return Tok::LParen;
      case ')': return Tok::RParen;
      case '{': return Tok::LBrace;
      case '}': return Tok::RBrace;
      case '[': return Tok::LBracket;
      case ']': return Tok::RBracket;
      case ',': return Tok::Comma;
      case ';': return Tok::Semicolon;
      case ':': return Tok::Colon;
      case '.': return Tok::Dot;
      case '+': return Tok::Plus;
      case '-': return Tok::Minus;
      case '*': return Tok::Star;
      case '/': return Tok::Slash;
      case '%': return Tok::Percent;
      case '=': return match('=') ? Tok::Eq : Tok::Assign;
      case '!':
        if (match('=')) return Tok::NotEq;
        throw CompileError("unexpected '!' (use 'not' for negation)", at);
      case '<': return match('=') ? Tok::LessEq : Tok::Less;
      case '>': return match('=') ? Tok::GreaterEq : Tok::Greater;
      default: {
        std::string shown = std::isprint(static_cast<unsigned char>(c)) ? std::string(1, c)
                                                                         : "byte " + std::to_string(static_cast<unsigned char>(c));
        throw CompileError("unexpected character '" + shown + "'", at);
      }
    }
  }

  const std::string& s_;
  std::size_t i_ = 0;
  int line_ = 1, col_ = 1;
};

}  // namespace

std::vector<Token> lex(const std::string& source) { return Lexer(source).run(); }

}  // namespace lumen
