#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "lumen/lexer.hpp"

namespace lumen {

struct Stmt;
struct Expr;
using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;

// A function literal or declaration. Shared, because closures created at
// run time point back at it.
struct FnDef {
  std::string name;  // "" for an anonymous fn
  std::vector<std::string> params;
  std::vector<StmtPtr> body;
  Span span;
};

struct Expr {
  enum class Kind {
    Int, Float, String, Bool, Nil,
    Var,       // name
    Unary,     // op (Minus, Not), a
    Binary,    // op, a, b  (arithmetic and comparison)
    Logical,   // op (And, Or), a, b  (short-circuit)
    Assign,    // a = target (Var or Index), b = value
    Call,      // a = callee, args
    Index,     // a[b]
    List,      // args = items
    Map,       // args = key0, value0, key1, value1, ...
    Fn,        // fn
  };
  Kind kind;
  Span span;
  Tok op = Tok::Eof;
  std::int64_t int_value = 0;
  double float_value = 0;
  bool bool_value = false;
  std::string text;  // string literal or variable name
  ExprPtr a, b;
  std::vector<ExprPtr> args;
  std::shared_ptr<FnDef> fn;
};

struct Stmt {
  enum class Kind {
    Let,       // name = expr
    Fn,        // fn declaration (fn->name is set)
    Expr,      // expr;
    Block,     // body
    If,        // cond, body, else_body (else_body may hold one nested If)
    While,     // cond, body
    For,       // name in expr, body
    Return,    // expr may be null
    Break,
    Continue,
  };
  Kind kind;
  Span span;
  std::string name;
  ExprPtr expr;   // let value, expression, condition, iterable, return value
  std::vector<StmtPtr> body;
  std::vector<StmtPtr> else_body;
  bool has_else = false;
  std::shared_ptr<FnDef> fn;
};

struct Program {
  std::vector<StmtPtr> stmts;
};

// Parses a whole program. Throws CompileError with the position of the
// first problem.
Program parse(const std::string& source);

// S-expression dump of the tree, for --dump-ast and parser tests.
std::string dump(const Program& p);

}  // namespace lumen
