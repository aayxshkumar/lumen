#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>

#include "lumen/interpreter.hpp"

namespace lumen {

namespace {

using Args = std::vector<Value>;

Value list_value(std::shared_ptr<List> l) {
  Value v;
  v.v = std::move(l);
  return v;
}

void need(Interpreter& in, bool ok, Span at, const std::string& msg) {
  if (!ok) in.fail(at, msg);
}

std::int64_t need_int(Interpreter& in, const Value& v, Span at, const char* fn, int pos) {
  if (!v.is_int())
    in.fail(at, std::string(fn) + "() argument " + std::to_string(pos) + " must be an int, got " + v.type_name());
  return v.as_int();
}

void add(Interpreter& in, std::string name, int min_args, int max_args,
         std::function<Value(Interpreter&, Args&, Span)> fn) {
  auto b = std::make_shared<Builtin>();
  b->name = name;
  b->min_args = min_args;
  b->max_args = max_args;
  b->fn = std::move(fn);
  Value v;
  v.v = std::move(b);
  in.globals->vars[name] = std::move(v);
}

}  // namespace

void install_builtins(Interpreter& interp) {
  add(interp, "print", 0, -1, [](Interpreter& in, Args& a, Span) {
    for (std::size_t i = 0; i < a.size(); ++i) {
      if (i) in.out << ' ';
      in.out << to_display(a[i]);
    }
    in.out << '\n';
    return Value();
  });

  add(interp, "len", 1, 1, [](Interpreter& in, Args& a, Span at) {
    const Value& v = a[0];
    if (v.is_list()) return Value::integer(static_cast<std::int64_t>(v.as_list().items.size()));
    if (v.is_map()) return Value::integer(static_cast<std::int64_t>(v.as_map().entries.size()));
    if (v.is_string()) return Value::integer(static_cast<std::int64_t>(v.as_string().size()));
    in.fail(at, std::string("len() of a ") + v.type_name());
  });

  add(interp, "push", 2, 2, [](Interpreter& in, Args& a, Span at) {
    need(in, a[0].is_list(), at, std::string("push() needs a list, got ") + a[0].type_name());
    a[0].as_list().items.push_back(std::move(a[1]));
    return Value();
  });

  add(interp, "pop", 1, 1, [](Interpreter& in, Args& a, Span at) {
    need(in, a[0].is_list(), at, std::string("pop() needs a list, got ") + a[0].type_name());
    auto& items = a[0].as_list().items;
    need(in, !items.empty(), at, "pop() from an empty list");
    Value v = std::move(items.back());
    items.pop_back();
    return v;
  });

  add(interp, "range", 1, 3, [](Interpreter& in, Args& a, Span at) {
    std::int64_t lo = 0, hi = 0, step = 1;
    if (a.size() == 1) {
      hi = need_int(in, a[0], at, "range", 1);
    } else {
      lo = need_int(in, a[0], at, "range", 1);
      hi = need_int(in, a[1], at, "range", 2);
      if (a.size() == 3) step = need_int(in, a[2], at, "range", 3);
    }
    need(in, step != 0, at, "range() step can't be 0");
    auto l = std::make_shared<List>();
    const double count = std::ceil((static_cast<double>(hi) - static_cast<double>(lo)) / static_cast<double>(step));
    need(in, count < 50'000'000, at, "range() would make more than 50 million items");
    for (std::int64_t i = lo; step > 0 ? i < hi : i > hi; i += step) {
      l->items.push_back(Value::integer(i));
      if ((step > 0 && i > std::numeric_limits<std::int64_t>::max() - step) ||
          (step < 0 && i < std::numeric_limits<std::int64_t>::min() - step))
        break;
    }
    return list_value(std::move(l));
  });

  add(interp, "str", 1, 1, [](Interpreter&, Args& a, Span) { return Value::string(to_display(a[0])); });
  add(interp, "repr", 1, 1, [](Interpreter&, Args& a, Span) { return Value::string(to_repr(a[0])); });
  add(interp, "type", 1, 1, [](Interpreter&, Args& a, Span) { return Value::string(a[0].type_name()); });

  add(interp, "int", 1, 1, [](Interpreter& in, Args& a, Span at) {
    const Value& v = a[0];
    if (v.is_int()) return v;
    if (v.is_float()) {
      const double d = std::trunc(v.as_float());
      need(in, std::isfinite(d) && d >= -9223372036854775808.0 && d < 9223372036854775808.0, at,
           "int() of " + to_repr(v) + " is out of range");
      return Value::integer(static_cast<std::int64_t>(d));
    }
    if (v.is_string()) {
      const std::string& s = v.as_string();
      errno = 0;
      char* end = nullptr;
      const long long r = std::strtoll(s.c_str(), &end, 10);
      need(in, !s.empty() && end == s.c_str() + s.size() && errno != ERANGE, at,
           "int() can't parse " + to_repr(v));
      return Value::integer(r);
    }
    in.fail(at, std::string("int() of a ") + v.type_name());
  });

  add(interp, "float", 1, 1, [](Interpreter& in, Args& a, Span at) {
    const Value& v = a[0];
    if (v.is_number()) return Value::real(v.as_number());
    if (v.is_string()) {
      const std::string& s = v.as_string();
      char* end = nullptr;
      const double d = std::strtod(s.c_str(), &end);
      need(in, !s.empty() && end == s.c_str() + s.size(), at, "float() can't parse " + to_repr(v));
      return Value::real(d);
    }
    in.fail(at, std::string("float() of a ") + v.type_name());
  });

  add(interp, "keys", 1, 1, [](Interpreter& in, Args& a, Span at) {
    need(in, a[0].is_map(), at, std::string("keys() needs a map, got ") + a[0].type_name());
    auto l = std::make_shared<List>();
    for (const auto& [k, v] : a[0].as_map().entries) l->items.push_back(k);
    return list_value(std::move(l));
  });

  add(interp, "has", 2, 2, [](Interpreter& in, Args& a, Span at) {
    need(in, a[0].is_map(), at, std::string("has() needs a map, got ") + a[0].type_name());
    need(in, Map::hashable(a[1]), at, std::string("a ") + a[1].type_name() + " can't be a map key");
    return Value::boolean(a[0].as_map().find(a[1]) != nullptr);
  });

  add(interp, "del", 2, 2, [](Interpreter& in, Args& a, Span at) {
    need(in, a[0].is_map(), at, std::string("del() needs a map, got ") + a[0].type_name());
    need(in, Map::hashable(a[1]), at, std::string("a ") + a[1].type_name() + " can't be a map key");
    return Value::boolean(a[0].as_map().erase(a[1]));
  });

  add(interp, "join", 2, 2, [](Interpreter& in, Args& a, Span at) {
    need(in, a[0].is_list(), at, std::string("join() needs a list, got ") + a[0].type_name());
    need(in, a[1].is_string(), at, std::string("join() separator must be a string, got ") + a[1].type_name());
    std::string out;
    const auto& items = a[0].as_list().items;
    for (std::size_t i = 0; i < items.size(); ++i) {
      if (i) out += a[1].as_string();
      out += to_display(items[i]);
    }
    return Value::string(std::move(out));
  });

  add(interp, "split", 2, 2, [](Interpreter& in, Args& a, Span at) {
    need(in, a[0].is_string() && a[1].is_string(), at, "split() needs two strings");
    const std::string& s = a[0].as_string();
    const std::string& sep = a[1].as_string();
    need(in, !sep.empty(), at, "split() separator can't be empty");
    auto l = std::make_shared<List>();
    std::size_t start = 0;
    while (true) {
      const std::size_t p = s.find(sep, start);
      if (p == std::string::npos) {
        l->items.push_back(Value::string(s.substr(start)));
        break;
      }
      l->items.push_back(Value::string(s.substr(start, p - start)));
      start = p + sep.size();
    }
    return list_value(std::move(l));
  });

  add(interp, "sort", 1, 2, [](Interpreter& in, Args& a, Span at) {
    need(in, a[0].is_list(), at, std::string("sort() needs a list, got ") + a[0].type_name());
    auto& items = a[0].as_list().items;
    if (a.size() == 2) {
      // sort(list, less) with a comparison function returning a bool.
      need(in, a[1].is_callable(), at, "sort() comparator must be a function");
      Value cmp = a[1];
      std::stable_sort(items.begin(), items.end(), [&](const Value& x, const Value& y) {
        std::vector<Value> args{x, y};
        const Value r = in.call(cmp, args, at);
        if (!r.is_bool()) in.fail(at, "sort() comparator must return a bool");
        return r.as_bool();
      });
      return Value();
    }
    const bool numbers = std::all_of(items.begin(), items.end(), [](const Value& v) { return v.is_number(); });
    const bool strings = std::all_of(items.begin(), items.end(), [](const Value& v) { return v.is_string(); });
    need(in, numbers || strings, at, "sort() without a comparator needs all numbers or all strings");
    std::stable_sort(items.begin(), items.end(), [&](const Value& x, const Value& y) {
      if (strings) return x.as_string() < y.as_string();
      if (x.is_int() && y.is_int()) return x.as_int() < y.as_int();
      return x.as_number() < y.as_number();
    });
    return Value();
  });

  add(interp, "abs", 1, 1, [](Interpreter& in, Args& a, Span at) {
    if (a[0].is_int()) {
      need(in, a[0].as_int() != std::numeric_limits<std::int64_t>::min(), at, "integer overflow");
      return Value::integer(a[0].as_int() < 0 ? -a[0].as_int() : a[0].as_int());
    }
    need(in, a[0].is_float(), at, std::string("abs() of a ") + a[0].type_name());
    return Value::real(std::fabs(a[0].as_float()));
  });

  add(interp, "sqrt", 1, 1, [](Interpreter& in, Args& a, Span at) {
    need(in, a[0].is_number(), at, std::string("sqrt() of a ") + a[0].type_name());
    return Value::real(std::sqrt(a[0].as_number()));
  });

  add(interp, "clock", 0, 0, [](Interpreter&, Args&, Span) {
    using namespace std::chrono;
    return Value::real(duration<double>(steady_clock::now().time_since_epoch()).count());
  });

  add(interp, "assert", 1, 2, [](Interpreter& in, Args& a, Span at) {
    need(in, a[0].is_bool(), at, std::string("assert() needs a bool, got ") + a[0].type_name());
    if (!a[0].as_bool()) in.fail(at, a.size() == 2 ? "assertion failed: " + to_display(a[1]) : "assertion failed");
    return Value();
  });

  add(interp, "error", 1, 1, [](Interpreter& in, Args& a, Span at) -> Value {
    in.fail(at, to_display(a[0]));
  });
}

}  // namespace lumen
