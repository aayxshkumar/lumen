#pragma once

#include <memory>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "lumen/ast.hpp"
#include "lumen/value.hpp"

namespace lumen {

// A run-time error, with the lumen call stack at the point it happened
// (innermost call first).
struct RuntimeError : std::runtime_error {
  RuntimeError(const std::string& msg, Span at, std::vector<std::string> stack)
      : std::runtime_error(msg), span(at), trace(std::move(stack)) {}
  Span span;
  std::vector<std::string> trace;
};

// Tree-walking interpreter. It is deliberately simple: environments are
// hash maps chained by pointer, and every variable lookup walks the chain.
// It is the reference implementation that faster back ends (see README
// roadmap) must agree with, program for program.
class Interpreter {
 public:
  explicit Interpreter(std::ostream& out);

  // Runs a whole program in the global environment. Throws RuntimeError.
  void run(const Program& p);
  // For the REPL: runs `p` and returns the value of its last statement if
  // that is an expression statement (otherwise nil).
  Value run_repl(const Program& p);

  // Calls a function value with arguments (used by builtins such as sort).
  Value call(const Value& callee, std::vector<Value>& args, Span at);
  [[noreturn]] void fail(Span at, const std::string& msg) const;

  std::ostream& out;
  std::shared_ptr<Env> globals;
  // Deepest lumen call nesting before "stack overflow" is reported. Each
  // lumen call uses several C++ frames, so the CLI runs the interpreter on a
  // thread with a large stack.
  int max_depth = 10000;
  std::uint64_t calls = 0;  // for --stats

 private:
  enum class Flow { Normal, Return, Break, Continue };

  Flow exec(const Stmt& s, const std::shared_ptr<Env>& env);
  Flow exec_block(const std::vector<StmtPtr>& body, const std::shared_ptr<Env>& env);
  Value eval(const Expr& e, const std::shared_ptr<Env>& env);
  Value binary(const Expr& e, const Value& a, const Value& b);
  Value index(const Expr& e, const Value& obj, const Value& key);
  void assign_index(const Expr& e, const Value& obj, const Value& key, Value v);
  bool condition(const Expr& e, const std::shared_ptr<Env>& env, const char* what);
  Value* lookup(const std::shared_ptr<Env>& env, const std::string& name);

  struct Frame {
    std::string name;
    Span call_site;
  };
  std::vector<Frame> stack_;
  Value return_value_;
};

void install_builtins(Interpreter& in);

}  // namespace lumen
