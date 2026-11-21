#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "lumen/ast.hpp"

namespace lumen {

struct Value;
struct List;
struct Map;
struct Closure;
struct Builtin;
struct Env;

// Live-object counters, shown by `lumen --stats`. Memory is reference
// counted, so a cycle (a closure stored in the environment it captured, a
// list that contains itself) is never freed. These counters make that leak
// visible; see README "Memory".
struct HeapStats {
  static inline std::int64_t lists = 0, maps = 0, closures = 0, envs = 0;
};

struct Value {
  using Str = std::shared_ptr<const std::string>;
  std::variant<std::monostate, bool, std::int64_t, double, Str, std::shared_ptr<List>,
               std::shared_ptr<Map>, std::shared_ptr<Closure>, std::shared_ptr<Builtin>>
      v;

  Value() = default;
  static Value nil() { return Value(); }
  static Value boolean(bool b) { Value x; x.v = b; return x; }
  static Value integer(std::int64_t i) { Value x; x.v = i; return x; }
  static Value real(double d) { Value x; x.v = d; return x; }
  static Value string(std::string s) { Value x; x.v = std::make_shared<const std::string>(std::move(s)); return x; }

  bool is_nil() const { return v.index() == 0; }
  bool is_bool() const { return v.index() == 1; }
  bool is_int() const { return v.index() == 2; }
  bool is_float() const { return v.index() == 3; }
  bool is_number() const { return is_int() || is_float(); }
  bool is_string() const { return v.index() == 4; }
  bool is_list() const { return v.index() == 5; }
  bool is_map() const { return v.index() == 6; }
  bool is_callable() const { return v.index() == 7 || v.index() == 8; }

  bool as_bool() const { return std::get<bool>(v); }
  std::int64_t as_int() const { return std::get<std::int64_t>(v); }
  double as_float() const { return std::get<double>(v); }
  double as_number() const { return is_int() ? static_cast<double>(as_int()) : as_float(); }
  const std::string& as_string() const { return *std::get<Str>(v); }
  List& as_list() const { return *std::get<std::shared_ptr<List>>(v); }
  Map& as_map() const { return *std::get<std::shared_ptr<Map>>(v); }

  // "int", "float", "string", ...
  const char* type_name() const;
};

struct List {
  List() { ++HeapStats::lists; }
  ~List() { --HeapStats::lists; }
  std::vector<Value> items;
};

// Insertion-ordered hash map. Keys must be nil, bool, int, float or string.
struct Map {
  Map() { ++HeapStats::maps; }
  ~Map() { --HeapStats::maps; }
  std::vector<std::pair<Value, Value>> entries;
  std::unordered_map<std::string, std::size_t> index;  // key encoding -> position

  static bool hashable(const Value& k);
  static std::string encode(const Value& k);  // precondition: hashable(k)
  Value* find(const Value& k);
  void set(const Value& k, Value v);
  bool erase(const Value& k);
};

struct Env {
  explicit Env(std::shared_ptr<Env> p) : parent(std::move(p)) { ++HeapStats::envs; }
  ~Env() { --HeapStats::envs; }
  std::unordered_map<std::string, Value> vars;
  std::shared_ptr<Env> parent;
};

struct Closure {
  Closure(std::shared_ptr<FnDef> f, std::shared_ptr<Env> e) : fn(std::move(f)), env(std::move(e)) {
    ++HeapStats::closures;
  }
  ~Closure() { --HeapStats::closures; }
  std::shared_ptr<FnDef> fn;
  std::shared_ptr<Env> env;
};

class Interpreter;
struct Builtin {
  std::string name;
  int min_args = 0, max_args = 0;  // max_args = -1 for variadic
  std::function<Value(Interpreter&, std::vector<Value>&, Span)> fn;
};

// Values print the way a lumen programmer reads them: strings bare at the
// top level, quoted inside lists and maps; floats always show a '.' or an
// exponent (2.0, 1e+30) so they can't be mistaken for ints.
std::string to_display(const Value& v);
std::string to_repr(const Value& v);

bool values_equal(const Value& a, const Value& b);

}  // namespace lumen
