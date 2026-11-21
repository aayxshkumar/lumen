#include "lumen/interpreter.hpp"

#include <cmath>
#include <limits>

namespace lumen {

Interpreter::Interpreter(std::ostream& o) : out(o), globals(std::make_shared<Env>(nullptr)) {
  install_builtins(*this);
}

void Interpreter::fail(Span at, const std::string& msg) const {
  std::vector<std::string> trace;
  for (auto it = stack_.rbegin(); it != stack_.rend(); ++it) {
    trace.push_back("in " + (it->name.empty() ? std::string("<fn>") : it->name) + " called at line " +
                    std::to_string(it->call_site.line) + ":" + std::to_string(it->call_site.col));
  }
  throw RuntimeError(msg, at, std::move(trace));
}

void Interpreter::run(const Program& p) {
  stack_.clear();
  for (const auto& s : p.stmts) {
    const Flow f = exec(*s, globals);
    if (f == Flow::Return) fail(s->span, "'return' outside a function");
    if (f == Flow::Break || f == Flow::Continue) fail(s->span, "'break' or 'continue' outside a loop");
  }
}

Value Interpreter::run_repl(const Program& p) {
  stack_.clear();
  Value last;
  for (const auto& s : p.stmts) {
    if (s->kind == Stmt::Kind::Expr) {
      last = eval(*s->expr, globals);
      continue;
    }
    last = Value();
    const Flow f = exec(*s, globals);
    if (f != Flow::Normal) fail(s->span, "'return', 'break' or 'continue' outside a function or loop");
  }
  return last;
}

Value* Interpreter::lookup(const std::shared_ptr<Env>& env, const std::string& name) {
  for (Env* e = env.get(); e; e = e->parent.get()) {
    auto it = e->vars.find(name);
    if (it != e->vars.end()) return &it->second;
  }
  return nullptr;
}

bool Interpreter::condition(const Expr& e, const std::shared_ptr<Env>& env, const char* what) {
  const Value v = eval(e, env);
  if (!v.is_bool())
    fail(e.span, std::string(what) + " must be a bool, got " + v.type_name() + " " + to_repr(v));
  return v.as_bool();
}

// ------------------------------------------------------------------ statements

Interpreter::Flow Interpreter::exec_block(const std::vector<StmtPtr>& body,
                                          const std::shared_ptr<Env>& env) {
  for (const auto& s : body) {
    const Flow f = exec(*s, env);
    if (f != Flow::Normal) return f;
  }
  return Flow::Normal;
}

Interpreter::Flow Interpreter::exec(const Stmt& s, const std::shared_ptr<Env>& env) {
  switch (s.kind) {
    case Stmt::Kind::Let: {
      Value v = eval(*s.expr, env);
      if (env->vars.count(s.name)) fail(s.span, "'" + s.name + "' is already declared in this scope");
      env->vars.emplace(s.name, std::move(v));
      return Flow::Normal;
    }
    case Stmt::Kind::Fn: {
      if (env->vars.count(s.fn->name))
        fail(s.span, "'" + s.fn->name + "' is already declared in this scope");
      // Declared before the closure is created so the function can call
      // itself. This makes env -> closure -> env a reference cycle.
      Value v;
      v.v = std::make_shared<Closure>(s.fn, env);
      env->vars.emplace(s.fn->name, std::move(v));
      return Flow::Normal;
    }
    case Stmt::Kind::Expr:
      eval(*s.expr, env);
      return Flow::Normal;
    case Stmt::Kind::Block:
      return exec_block(s.body, std::make_shared<Env>(env));
    case Stmt::Kind::If:
      if (condition(*s.expr, env, "an 'if' condition")) return exec_block(s.body, std::make_shared<Env>(env));
      if (s.has_else) return exec_block(s.else_body, std::make_shared<Env>(env));
      return Flow::Normal;
    case Stmt::Kind::While:
      while (condition(*s.expr, env, "a 'while' condition")) {
        const Flow f = exec_block(s.body, std::make_shared<Env>(env));
        if (f == Flow::Break) break;
        if (f == Flow::Return) return f;
      }
      return Flow::Normal;
    case Stmt::Kind::For: {
      const Value it = eval(*s.expr, env);
      std::vector<Value> items;  // a snapshot, so the body may modify the collection
      if (it.is_list()) {
        items = it.as_list().items;
      } else if (it.is_map()) {
        for (const auto& [k, v] : it.as_map().entries) items.push_back(k);
      } else if (it.is_string()) {
        for (char c : it.as_string()) items.push_back(Value::string(std::string(1, c)));
      } else {
        fail(s.expr->span, std::string("can't iterate over ") + it.type_name());
      }
      for (auto& item : items) {
        // A fresh environment per iteration, so closures capture this
        // iteration's variable.
        auto body_env = std::make_shared<Env>(env);
        body_env->vars.emplace(s.name, std::move(item));
        const Flow f = exec_block(s.body, body_env);
        if (f == Flow::Break) break;
        if (f == Flow::Return) return f;
      }
      return Flow::Normal;
    }
    case Stmt::Kind::Return:
      return_value_ = s.expr ? eval(*s.expr, env) : Value();
      return Flow::Return;
    case Stmt::Kind::Break:
      return Flow::Break;
    case Stmt::Kind::Continue:
      return Flow::Continue;
  }
  return Flow::Normal;
}

// ------------------------------------------------------------------ expressions

Value Interpreter::eval(const Expr& e, const std::shared_ptr<Env>& env) {
  switch (e.kind) {
    case Expr::Kind::Int: return Value::integer(e.int_value);
    case Expr::Kind::Float: return Value::real(e.float_value);
    case Expr::Kind::String: return Value::string(e.text);
    case Expr::Kind::Bool: return Value::boolean(e.bool_value);
    case Expr::Kind::Nil: return Value();
    case Expr::Kind::Var: {
      Value* v = lookup(env, e.text);
      if (!v) fail(e.span, "undefined variable '" + e.text + "'");
      return *v;
    }
    case Expr::Kind::Unary: {
      const Value v = eval(*e.a, env);
      if (e.op == Tok::Not) {
        if (!v.is_bool()) fail(e.span, std::string("'not' needs a bool, got ") + v.type_name());
        return Value::boolean(!v.as_bool());
      }
      if (v.is_int()) {
        if (v.as_int() == std::numeric_limits<std::int64_t>::min()) fail(e.span, "integer overflow");
        return Value::integer(-v.as_int());
      }
      if (v.is_float()) return Value::real(-v.as_float());
      fail(e.span, std::string("can't negate ") + v.type_name());
    }
    case Expr::Kind::Binary: {
      const Value a = eval(*e.a, env);
      const Value b = eval(*e.b, env);
      return binary(e, a, b);
    }
    case Expr::Kind::Logical: {
      const char* what = e.op == Tok::And ? "'and'" : "'or'";
      const Value a = eval(*e.a, env);
      if (!a.is_bool()) fail(e.a->span, std::string(what) + " needs bools, got " + a.type_name());
      if (e.op == Tok::And ? !a.as_bool() : a.as_bool()) return a;
      const Value b = eval(*e.b, env);
      if (!b.is_bool()) fail(e.b->span, std::string(what) + " needs bools, got " + b.type_name());
      return b;
    }
    case Expr::Kind::Assign: {
      if (e.a->kind == Expr::Kind::Var) {
        Value v = eval(*e.b, env);
        Value* slot = lookup(env, e.a->text);
        if (!slot) fail(e.a->span, "assignment to undeclared variable '" + e.a->text + "' (use 'let')");
        *slot = v;
        return v;
      }
      const Value obj = eval(*e.a->a, env);
      const Value key = eval(*e.a->b, env);
      Value v = eval(*e.b, env);
      assign_index(*e.a, obj, key, v);
      return v;
    }
    case Expr::Kind::Call: {
      const Value callee = eval(*e.a, env);
      std::vector<Value> args;
      args.reserve(e.args.size());
      for (const auto& a : e.args) args.push_back(eval(*a, env));
      return call(callee, args, e.span);
    }
    case Expr::Kind::Index: {
      const Value obj = eval(*e.a, env);
      const Value key = eval(*e.b, env);
      return index(e, obj, key);
    }
    case Expr::Kind::List: {
      auto l = std::make_shared<List>();
      for (const auto& a : e.args) l->items.push_back(eval(*a, env));
      Value v;
      v.v = l;
      return v;
    }
    case Expr::Kind::Map: {
      auto m = std::make_shared<Map>();
      for (std::size_t i = 0; i < e.args.size(); i += 2) {
        Value k = eval(*e.args[i], env);
        if (!Map::hashable(k)) fail(e.args[i]->span, std::string("a ") + k.type_name() + " can't be a map key");
        m->set(k, eval(*e.args[i + 1], env));
      }
      Value v;
      v.v = m;
      return v;
    }
    case Expr::Kind::Fn: {
      Value v;
      v.v = std::make_shared<Closure>(e.fn, env);
      return v;
    }
  }
  return Value();
}

Value Interpreter::call(const Value& callee, std::vector<Value>& args, Span at) {
  ++calls;
  if (callee.v.index() == 8) {
    const auto& b = *std::get<std::shared_ptr<Builtin>>(callee.v);
    const int n = static_cast<int>(args.size());
    if (n < b.min_args || (b.max_args >= 0 && n > b.max_args)) {
      std::string want = b.min_args == b.max_args ? std::to_string(b.min_args)
                         : b.max_args < 0         ? "at least " + std::to_string(b.min_args)
                                                  : std::to_string(b.min_args) + " to " + std::to_string(b.max_args);
      fail(at, b.name + "() takes " + want + " argument" + (want == "1" ? "" : "s") + ", got " +
                   std::to_string(n));
    }
    return b.fn(*this, args, at);
  }
  if (callee.v.index() != 7) fail(at, std::string("can't call a ") + callee.type_name());
  const auto closure = std::get<std::shared_ptr<Closure>>(callee.v);
  const FnDef& fn = *closure->fn;
  if (args.size() != fn.params.size()) {
    fail(at, (fn.name.empty() ? std::string("<fn>") : fn.name) + " takes " +
                 std::to_string(fn.params.size()) + " argument" + (fn.params.size() == 1 ? "" : "s") +
                 ", got " + std::to_string(args.size()));
  }
  if (static_cast<int>(stack_.size()) >= max_depth)
    fail(at, "stack overflow (more than " + std::to_string(max_depth) + " nested calls)");
  auto env = std::make_shared<Env>(closure->env);
  for (std::size_t i = 0; i < args.size(); ++i) env->vars.emplace(fn.params[i], std::move(args[i]));
  stack_.push_back(Frame{fn.name, at});
  const Flow f = exec_block(fn.body, env);
  stack_.pop_back();
  if (f == Flow::Break || f == Flow::Continue) fail(at, "'break' or 'continue' outside a loop");
  if (f == Flow::Return) {
    Value v = std::move(return_value_);
    return_value_ = Value();
    return v;
  }
  return Value();
}

// ------------------------------------------------------------------ operators

Value Interpreter::binary(const Expr& e, const Value& a, const Value& b) {
  const Tok op = e.op;
  if (op == Tok::Eq) return Value::boolean(values_equal(a, b));
  if (op == Tok::NotEq) return Value::boolean(!values_equal(a, b));

  const auto type_message = [&](const char* verb) {
    return std::string("can't ") + verb + " " + a.type_name() + " and " + b.type_name();
  };

  if (op == Tok::Less || op == Tok::LessEq || op == Tok::Greater || op == Tok::GreaterEq) {
    int c = 0;
    if (a.is_int() && b.is_int()) {
      c = a.as_int() < b.as_int() ? -1 : a.as_int() > b.as_int() ? 1 : 0;
    } else if (a.is_number() && b.is_number()) {
      const double x = a.as_number(), y = b.as_number();
      if (std::isnan(x) || std::isnan(y)) return Value::boolean(false);
      c = x < y ? -1 : x > y ? 1 : 0;
    } else if (a.is_string() && b.is_string()) {
      const int r = a.as_string().compare(b.as_string());
      c = r < 0 ? -1 : r > 0 ? 1 : 0;
    } else {
      fail(e.span, type_message("compare"));
    }
    switch (op) {
      case Tok::Less: return Value::boolean(c < 0);
      case Tok::LessEq: return Value::boolean(c <= 0);
      case Tok::Greater: return Value::boolean(c > 0);
      default: return Value::boolean(c >= 0);
    }
  }

  if (op == Tok::Plus) {
    if (a.is_string() && b.is_string()) return Value::string(a.as_string() + b.as_string());
    if (a.is_list() && b.is_list()) {
      auto l = std::make_shared<List>();
      l->items = a.as_list().items;
      l->items.insert(l->items.end(), b.as_list().items.begin(), b.as_list().items.end());
      Value v;
      v.v = l;
      return v;
    }
  }
  if (!a.is_number() || !b.is_number()) {
    const char* verb = op == Tok::Plus    ? "add"
                       : op == Tok::Minus ? "subtract"
                       : op == Tok::Star  ? "multiply"
                       : op == Tok::Slash ? "divide"
                                          : "take the remainder of";
    fail(e.span, type_message(verb));
  }

  if (a.is_int() && b.is_int()) {
    const std::int64_t x = a.as_int(), y = b.as_int();
    std::int64_t r = 0;
    switch (op) {
      case Tok::Plus:
        if (__builtin_add_overflow(x, y, &r)) fail(e.span, "integer overflow");
        return Value::integer(r);
      case Tok::Minus:
        if (__builtin_sub_overflow(x, y, &r)) fail(e.span, "integer overflow");
        return Value::integer(r);
      case Tok::Star:
        if (__builtin_mul_overflow(x, y, &r)) fail(e.span, "integer overflow");
        return Value::integer(r);
      case Tok::Slash:
        if (y == 0) fail(e.span, "division by zero");
        if (x == std::numeric_limits<std::int64_t>::min() && y == -1) fail(e.span, "integer overflow");
        return Value::integer(x / y);  // truncates toward zero
      default:
        if (y == 0) fail(e.span, "division by zero");
        if (y == -1) return Value::integer(0);
        return Value::integer(x % y);  // sign follows the dividend
    }
  }
  const double x = a.as_number(), y = b.as_number();
  switch (op) {
    case Tok::Plus: return Value::real(x + y);
    case Tok::Minus: return Value::real(x - y);
    case Tok::Star: return Value::real(x * y);
    case Tok::Slash: return Value::real(x / y);  // IEEE: 1.0 / 0 is inf
    default: return Value::real(std::fmod(x, y));
  }
}

Value Interpreter::index(const Expr& e, const Value& obj, const Value& key) {
  if (obj.is_list() || obj.is_string()) {
    if (!key.is_int()) fail(e.span, std::string("index must be an int, got ") + key.type_name());
    const std::int64_t i = key.as_int();
    const std::size_t n = obj.is_list() ? obj.as_list().items.size() : obj.as_string().size();
    if (i < 0 || static_cast<std::uint64_t>(i) >= n)
      fail(e.span, "index " + std::to_string(i) + " out of range (length " + std::to_string(n) + ")");
    if (obj.is_list()) return obj.as_list().items[static_cast<std::size_t>(i)];
    return Value::string(std::string(1, obj.as_string()[static_cast<std::size_t>(i)]));
  }
  if (obj.is_map()) {
    if (!Map::hashable(key)) fail(e.span, std::string("a ") + key.type_name() + " can't be a map key");
    const Value* v = obj.as_map().find(key);
    if (!v) fail(e.span, "key " + to_repr(key) + " not in map");
    return *v;
  }
  fail(e.span, std::string("can't index a ") + obj.type_name());
}

void Interpreter::assign_index(const Expr& e, const Value& obj, const Value& key, Value v) {
  if (obj.is_list()) {
    if (!key.is_int()) fail(e.span, std::string("index must be an int, got ") + key.type_name());
    auto& items = obj.as_list().items;
    const std::int64_t i = key.as_int();
    if (i < 0 || static_cast<std::uint64_t>(i) >= items.size())
      fail(e.span, "index " + std::to_string(i) + " out of range (length " + std::to_string(items.size()) + ")");
    items[static_cast<std::size_t>(i)] = std::move(v);
    return;
  }
  if (obj.is_map()) {
    if (!Map::hashable(key)) fail(e.span, std::string("a ") + key.type_name() + " can't be a map key");
    obj.as_map().set(key, std::move(v));
    return;
  }
  if (obj.is_string()) fail(e.span, "strings are immutable");
  fail(e.span, std::string("can't index a ") + obj.type_name());
}

}  // namespace lumen
