#include <sstream>

#include "lumen/ast.hpp"

namespace lumen {

namespace {

// Binding powers, lowest first. Assignment is handled separately (it is
// right-associative and needs an assignable target).
enum Prec {
  kLowest = 0,
  kOr,
  kAnd,
  kNot,
  kCompare,
  kSum,
  kProduct,
  kUnary,
  kPostfix,
};

Prec infix_prec(Tok t) {
  switch (t) {
    case Tok::Or: return kOr;
    case Tok::And: return kAnd;
    case Tok::Eq: case Tok::NotEq: case Tok::Less: case Tok::LessEq:
    case Tok::Greater: case Tok::GreaterEq: return kCompare;
    case Tok::Plus: case Tok::Minus: return kSum;
    case Tok::Star: case Tok::Slash: case Tok::Percent: return kProduct;
    case Tok::LParen: case Tok::LBracket: return kPostfix;
    default: return kLowest;
  }
}

bool is_comparison(Tok t) { return infix_prec(t) == kCompare; }

class Parser {
 public:
  explicit Parser(std::vector<Token> toks) : t_(std::move(toks)) {}

  Program program() {
    Program p;
    while (!check(Tok::Eof)) p.stmts.push_back(statement());
    return p;
  }

 private:
  const Token& cur() const { return t_[i_]; }
  bool check(Tok k) const { return cur().kind == k; }
  const Token& advance() { return t_[i_ == t_.size() - 1 ? i_ : i_++]; }
  bool match(Tok k) {
    if (!check(k)) return false;
    advance();
    return true;
  }
  [[noreturn]] void error_here(const std::string& what) const {
    throw CompileError(what + ", found " + describe(cur()), cur().span);
  }
  static std::string describe(const Token& t) {
    if (t.kind == Tok::Ident) return "identifier '" + t.text + "'";
    if (t.kind == Tok::Int || t.kind == Tok::Float) return "number";
    if (t.kind == Tok::String) return "string";
    return tok_name(t.kind);
  }
  const Token& expect(Tok k, const char* context) {
    if (!check(k)) error_here(std::string("expected ") + tok_name(k) + " " + context);
    return advance();
  }

  // ------------------------------------------------------------ statements

  StmtPtr statement() {
    const Span at = cur().span;
    auto s = std::make_unique<Stmt>();
    s->span = at;
    if (match(Tok::Let)) {
      s->kind = Stmt::Kind::Let;
      s->name = expect(Tok::Ident, "after 'let'").text;
      expect(Tok::Assign, "after the variable name (every 'let' needs a value)");
      s->expr = expression(true);
      expect(Tok::Semicolon, "after the 'let' value");
      return s;
    }
    if (check(Tok::Fn) && t_[i_ + 1].kind == Tok::Ident) {
      advance();
      s->kind = Stmt::Kind::Fn;
      s->fn = function_rest(at, advance().text);
      return s;
    }
    if (match(Tok::If)) return if_rest(std::move(s));
    if (match(Tok::While)) {
      s->kind = Stmt::Kind::While;
      s->expr = expression(false);
      s->body = block("after the 'while' condition");
      return s;
    }
    if (match(Tok::For)) {
      s->kind = Stmt::Kind::For;
      s->name = expect(Tok::Ident, "after 'for'").text;
      expect(Tok::In, "after the loop variable");
      s->expr = expression(false);
      s->body = block("after the 'for' iterable");
      return s;
    }
    if (match(Tok::Return)) {
      s->kind = Stmt::Kind::Return;
      if (!check(Tok::Semicolon)) s->expr = expression(true);
      expect(Tok::Semicolon, "after 'return'");
      return s;
    }
    if (match(Tok::Break)) {
      s->kind = Stmt::Kind::Break;
      expect(Tok::Semicolon, "after 'break'");
      return s;
    }
    if (match(Tok::Continue)) {
      s->kind = Stmt::Kind::Continue;
      expect(Tok::Semicolon, "after 'continue'");
      return s;
    }
    if (check(Tok::LBrace)) {
      s->kind = Stmt::Kind::Block;
      s->body = block("");
      return s;
    }
    s->kind = Stmt::Kind::Expr;
    s->expr = expression(true);
    expect(Tok::Semicolon, "after the expression");
    return s;
  }

  StmtPtr if_rest(StmtPtr s) {
    s->kind = Stmt::Kind::If;
    s->expr = expression(false);
    s->body = block("after the 'if' condition");
    if (match(Tok::Else)) {
      s->has_else = true;
      if (check(Tok::If)) {
        auto nested = std::make_unique<Stmt>();
        nested->span = cur().span;
        advance();
        s->else_body.push_back(if_rest(std::move(nested)));
      } else {
        s->else_body = block("after 'else'");
      }
    }
    return s;
  }

  std::vector<StmtPtr> block(const char* context) {
    std::string ctx = context;
    expect(Tok::LBrace, ctx.empty() ? "to start a block" : ctx.c_str());
    std::vector<StmtPtr> body;
    while (!check(Tok::RBrace)) {
      if (check(Tok::Eof)) error_here("expected '}' to close the block");
      body.push_back(statement());
    }
    advance();
    return body;
  }

  std::shared_ptr<FnDef> function_rest(Span at, std::string name) {
    auto fn = std::make_shared<FnDef>();
    fn->name = std::move(name);
    fn->span = at;
    expect(Tok::LParen, "to start the parameter list");
    if (!check(Tok::RParen)) {
      do {
        const Token& p = expect(Tok::Ident, "as a parameter name");
        for (const auto& existing : fn->params)
          if (existing == p.text) throw CompileError("duplicate parameter '" + p.text + "'", p.span);
        fn->params.push_back(p.text);
      } while (match(Tok::Comma));
    }
    expect(Tok::RParen, "after the parameters");
    fn->body = block("to start the function body");
    return fn;
  }

  // ------------------------------------------------------------ expressions

  // allow_map: false in `if`/`while`/`for` heads, where '{' starts the body
  // (the same rule Rust uses). Parentheses turn it back on.
  ExprPtr expression(bool allow_map) {
    const bool saved = allow_map_;
    allow_map_ = allow_map;
    auto e = assignment();
    allow_map_ = saved;
    return e;
  }

  ExprPtr assignment() {
    auto target = binary(kLowest);
    if (!check(Tok::Assign)) return target;
    const Span at = cur().span;
    advance();
    if (target->kind != Expr::Kind::Var && target->kind != Expr::Kind::Index)
      throw CompileError("can only assign to a variable or an index", at);
    auto e = std::make_unique<Expr>();
    e->kind = Expr::Kind::Assign;
    e->span = at;
    e->a = std::move(target);
    e->b = assignment();
    return e;
  }

  ExprPtr binary(Prec min_prec) {
    auto left = prefix();
    while (true) {
      const Tok op = cur().kind;
      const Prec p = infix_prec(op);
      if (p == kLowest || p <= min_prec) break;
      const Span at = cur().span;
      advance();
      if (op == Tok::LParen) {
        left = call(std::move(left), at);
        continue;
      }
      if (op == Tok::LBracket) {
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Kind::Index;
        e->span = at;
        e->a = std::move(left);
        e->b = expression(true);
        expect(Tok::RBracket, "after the index");
        left = std::move(e);
        continue;
      }
      auto right = binary(p);
      if (is_comparison(op) && is_comparison(cur().kind))
        throw CompileError("comparisons can't be chained; use 'and'", cur().span);
      auto e = std::make_unique<Expr>();
      e->kind = (op == Tok::And || op == Tok::Or) ? Expr::Kind::Logical : Expr::Kind::Binary;
      e->op = op;
      e->span = at;
      e->a = std::move(left);
      e->b = std::move(right);
      left = std::move(e);
    }
    return left;
  }

  ExprPtr call(ExprPtr callee, Span at) {
    auto e = std::make_unique<Expr>();
    e->kind = Expr::Kind::Call;
    e->span = at;
    e->a = std::move(callee);
    if (!check(Tok::RParen)) {
      do e->args.push_back(expression(true));
      while (match(Tok::Comma));
    }
    expect(Tok::RParen, "after the arguments");
    return e;
  }

  ExprPtr prefix() {
    const Token& t = cur();
    auto e = std::make_unique<Expr>();
    e->span = t.span;
    switch (t.kind) {
      case Tok::Int:
        e->kind = Expr::Kind::Int;
        e->int_value = t.int_value;
        advance();
        return e;
      case Tok::Float:
        e->kind = Expr::Kind::Float;
        e->float_value = t.float_value;
        advance();
        return e;
      case Tok::String:
        e->kind = Expr::Kind::String;
        e->text = t.text;
        advance();
        return e;
      case Tok::True:
      case Tok::False:
        e->kind = Expr::Kind::Bool;
        e->bool_value = t.kind == Tok::True;
        advance();
        return e;
      case Tok::Nil:
        e->kind = Expr::Kind::Nil;
        advance();
        return e;
      case Tok::Ident:
        e->kind = Expr::Kind::Var;
        e->text = t.text;
        advance();
        return e;
      case Tok::Minus:
        advance();
        e->kind = Expr::Kind::Unary;
        e->op = Tok::Minus;
        e->a = binary(kUnary);
        return e;
      case Tok::Not:
        advance();
        e->kind = Expr::Kind::Unary;
        e->op = Tok::Not;
        e->a = binary(kNot);
        return e;
      case Tok::LParen: {
        advance();
        auto inner = expression(true);
        expect(Tok::RParen, "to close the parenthesis");
        return inner;
      }
      case Tok::LBracket:
        advance();
        e->kind = Expr::Kind::List;
        if (!check(Tok::RBracket)) {
          do {
            if (check(Tok::RBracket)) break;  // trailing comma
            e->args.push_back(expression(true));
          } while (match(Tok::Comma));
        }
        expect(Tok::RBracket, "to close the list");
        return e;
      case Tok::LBrace:
        if (!allow_map_) error_here("expected an expression (put a map literal in parentheses here)");
        advance();
        e->kind = Expr::Kind::Map;
        if (!check(Tok::RBrace)) {
          do {
            if (check(Tok::RBrace)) break;
            e->args.push_back(expression(true));
            expect(Tok::Colon, "between a map key and its value");
            e->args.push_back(expression(true));
          } while (match(Tok::Comma));
        }
        expect(Tok::RBrace, "to close the map");
        return e;
      case Tok::Fn:
        advance();
        e->kind = Expr::Kind::Fn;
        e->fn = function_rest(t.span, "");
        return e;
      default:
        error_here("expected an expression");
    }
  }

  std::vector<Token> t_;
  std::size_t i_ = 0;
  bool allow_map_ = true;
};

// ------------------------------------------------------------------ dump

void dump_expr(std::ostream& o, const Expr& e);
void dump_stmts(std::ostream& o, const std::vector<StmtPtr>& s);

void dump_expr(std::ostream& o, const Expr& e) {
  switch (e.kind) {
    case Expr::Kind::Int: o << e.int_value; return;
    case Expr::Kind::Float: o << e.float_value << "f"; return;
    case Expr::Kind::String: o << '"' << e.text << '"'; return;
    case Expr::Kind::Bool: o << (e.bool_value ? "true" : "false"); return;
    case Expr::Kind::Nil: o << "nil"; return;
    case Expr::Kind::Var: o << e.text; return;
    case Expr::Kind::Unary:
      o << "(" << (e.op == Tok::Not ? "not" : "neg") << " ";
      dump_expr(o, *e.a);
      o << ")";
      return;
    case Expr::Kind::Binary:
    case Expr::Kind::Logical: {
      std::string op = tok_name(e.op);
      if (op.size() > 2 && op.front() == '\'') op = op.substr(1, op.size() - 2);
      o << "(" << op << " ";
      dump_expr(o, *e.a);
      o << " ";
      dump_expr(o, *e.b);
      o << ")";
      return;
    }
    case Expr::Kind::Assign:
      o << "(= ";
      dump_expr(o, *e.a);
      o << " ";
      dump_expr(o, *e.b);
      o << ")";
      return;
    case Expr::Kind::Call:
      o << "(call ";
      dump_expr(o, *e.a);
      for (const auto& a : e.args) {
        o << " ";
        dump_expr(o, *a);
      }
      o << ")";
      return;
    case Expr::Kind::Index:
      o << "(index ";
      dump_expr(o, *e.a);
      o << " ";
      dump_expr(o, *e.b);
      o << ")";
      return;
    case Expr::Kind::List:
    case Expr::Kind::Map:
      o << (e.kind == Expr::Kind::List ? "(list" : "(map");
      for (const auto& a : e.args) {
        o << " ";
        dump_expr(o, *a);
      }
      o << ")";
      return;
    case Expr::Kind::Fn:
      o << "(fn (";
      for (std::size_t i = 0; i < e.fn->params.size(); ++i) o << (i ? " " : "") << e.fn->params[i];
      o << ") ";
      dump_stmts(o, e.fn->body);
      o << ")";
      return;
  }
}

void dump_stmt(std::ostream& o, const Stmt& s) {
  switch (s.kind) {
    case Stmt::Kind::Let:
      o << "(let " << s.name << " ";
      dump_expr(o, *s.expr);
      o << ")";
      return;
    case Stmt::Kind::Fn:
      o << "(fndecl " << s.fn->name << " (";
      for (std::size_t i = 0; i < s.fn->params.size(); ++i) o << (i ? " " : "") << s.fn->params[i];
      o << ") ";
      dump_stmts(o, s.fn->body);
      o << ")";
      return;
    case Stmt::Kind::Expr: dump_expr(o, *s.expr); return;
    case Stmt::Kind::Block: dump_stmts(o, s.body); return;
    case Stmt::Kind::If:
      o << "(if ";
      dump_expr(o, *s.expr);
      o << " ";
      dump_stmts(o, s.body);
      if (s.has_else) {
        o << " ";
        dump_stmts(o, s.else_body);
      }
      o << ")";
      return;
    case Stmt::Kind::While:
      o << "(while ";
      dump_expr(o, *s.expr);
      o << " ";
      dump_stmts(o, s.body);
      o << ")";
      return;
    case Stmt::Kind::For:
      o << "(for " << s.name << " ";
      dump_expr(o, *s.expr);
      o << " ";
      dump_stmts(o, s.body);
      o << ")";
      return;
    case Stmt::Kind::Return:
      o << "(return";
      if (s.expr) {
        o << " ";
        dump_expr(o, *s.expr);
      }
      o << ")";
      return;
    case Stmt::Kind::Break: o << "(break)"; return;
    case Stmt::Kind::Continue: o << "(continue)"; return;
  }
}

void dump_stmts(std::ostream& o, const std::vector<StmtPtr>& stmts) {
  o << "{";
  for (std::size_t i = 0; i < stmts.size(); ++i) {
    o << (i ? " " : "");
    dump_stmt(o, *stmts[i]);
  }
  o << "}";
}

}  // namespace

Program parse(const std::string& source) { return Parser(lex(source)).program(); }

std::string dump(const Program& p) {
  std::ostringstream o;
  for (const auto& s : p.stmts) {
    dump_stmt(o, *s);
    o << "\n";
  }
  return o.str();
}

}  // namespace lumen
