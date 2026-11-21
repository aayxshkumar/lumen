#include "lumen/value.hpp"

#include <charconv>
#include <cmath>
#include <cstring>

namespace lumen {

const char* Value::type_name() const {
  switch (v.index()) {
    case 0: return "nil";
    case 1: return "bool";
    case 2: return "int";
    case 3: return "float";
    case 4: return "string";
    case 5: return "list";
    case 6: return "map";
    case 7: return "function";
    case 8: return "builtin";
  }
  return "?";
}

bool Map::hashable(const Value& k) { return k.v.index() <= 4; }

std::string Map::encode(const Value& k) {
  // Ints and floats that are equal (1 and 1.0) must be the same key, since
  // 1 == 1.0 is true. Integral floats are encoded as ints.
  std::string out;
  switch (k.v.index()) {
    case 0: return "n";
    case 1: return k.as_bool() ? "t" : "f";
    case 2: return "i" + std::to_string(k.as_int());
    case 3: {
      const double d = k.as_float();
      if (std::isfinite(d) && d == std::trunc(d) && std::fabs(d) < 9.2e18)
        return "i" + std::to_string(static_cast<std::int64_t>(d));
      out = "d";
      out.append(reinterpret_cast<const char*>(&d), sizeof d);
      return out;
    }
    default: return "s" + k.as_string();
  }
}

Value* Map::find(const Value& k) {
  auto it = index.find(encode(k));
  return it == index.end() ? nullptr : &entries[it->second].second;
}

void Map::set(const Value& k, Value v) {
  const std::string key = encode(k);
  auto it = index.find(key);
  if (it != index.end()) {
    entries[it->second].second = std::move(v);
    return;
  }
  index.emplace(key, entries.size());
  entries.emplace_back(k, std::move(v));
}

bool Map::erase(const Value& k) {
  auto it = index.find(encode(k));
  if (it == index.end()) return false;
  const std::size_t pos = it->second;
  index.erase(it);
  entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(pos));
  for (auto& [key, p] : index)
    if (p > pos) --p;
  return true;
}

namespace {

std::string format_float(double d) {
  if (std::isnan(d)) return "nan";
  if (std::isinf(d)) return d > 0 ? "inf" : "-inf";
  char buf[64];
  auto r = std::to_chars(buf, buf + sizeof buf, d);  // shortest round-trip form
  std::string s(buf, r.ptr);
  if (s.find_first_of(".en") == std::string::npos) s += ".0";
  return s;
}

void write(std::string& out, const Value& v, bool quote, int depth) {
  if (depth > 64) {
    out += "...";
    return;
  }
  switch (v.v.index()) {
    case 0: out += "nil"; return;
    case 1: out += v.as_bool() ? "true" : "false"; return;
    case 2: out += std::to_string(v.as_int()); return;
    case 3: out += format_float(v.as_float()); return;
    case 4:
      if (!quote) {
        out += v.as_string();
        return;
      }
      out += '"';
      for (char c : v.as_string()) {
        if (c == '"' || c == '\\') out += '\\';
        if (c == '\n') {
          out += "\\n";
          continue;
        }
        out += c;
      }
      out += '"';
      return;
    case 5: {
      out += '[';
      const auto& items = v.as_list().items;
      for (std::size_t i = 0; i < items.size(); ++i) {
        if (i) out += ", ";
        write(out, items[i], true, depth + 1);
      }
      out += ']';
      return;
    }
    case 6: {
      out += '{';
      bool first = true;
      for (const auto& [k, val] : v.as_map().entries) {
        if (!first) out += ", ";
        first = false;
        write(out, k, true, depth + 1);
        out += ": ";
        write(out, val, true, depth + 1);
      }
      out += '}';
      return;
    }
    case 7: {
      const auto& name = std::get<std::shared_ptr<Closure>>(v.v)->fn->name;
      out += name.empty() ? "<fn>" : "<fn " + name + ">";
      return;
    }
    case 8: out += "<builtin " + std::get<std::shared_ptr<Builtin>>(v.v)->name + ">"; return;
  }
}

bool equal(const Value& a, const Value& b, int depth) {
  if (depth > 64) return false;
  if (a.is_number() && b.is_number()) {
    if (a.is_int() && b.is_int()) return a.as_int() == b.as_int();
    return a.as_number() == b.as_number();
  }
  if (a.v.index() != b.v.index()) return false;
  switch (a.v.index()) {
    case 0: return true;
    case 1: return a.as_bool() == b.as_bool();
    case 4: return a.as_string() == b.as_string();
    case 5: {
      const auto& x = a.as_list().items;
      const auto& y = b.as_list().items;
      if (&x == &y) return true;
      if (x.size() != y.size()) return false;
      for (std::size_t i = 0; i < x.size(); ++i)
        if (!equal(x[i], y[i], depth + 1)) return false;
      return true;
    }
    case 6: {
      auto& x = a.as_map();
      auto& y = b.as_map();
      if (&x == &y) return true;
      if (x.entries.size() != y.entries.size()) return false;
      for (const auto& [k, val] : x.entries) {
        const Value* other = y.find(k);
        if (!other || !equal(val, *other, depth + 1)) return false;
      }
      return true;
    }
    case 7: return std::get<std::shared_ptr<Closure>>(a.v) == std::get<std::shared_ptr<Closure>>(b.v);
    case 8: return std::get<std::shared_ptr<Builtin>>(a.v) == std::get<std::shared_ptr<Builtin>>(b.v);
  }
  return false;
}

}  // namespace

std::string to_display(const Value& v) {
  std::string s;
  write(s, v, false, 0);
  return s;
}

std::string to_repr(const Value& v) {
  std::string s;
  write(s, v, true, 0);
  return s;
}

bool values_equal(const Value& a, const Value& b) { return equal(a, b, 0); }

}  // namespace lumen
