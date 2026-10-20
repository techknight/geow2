#include "net/json.h"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace geow2::json {
namespace {

const Value kNull;
const Array kNoArray;

struct Parser {
  std::string_view s;
  size_t i = 0;
  int depth = 0;

  void space() {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
  }
  bool eat(char c) {
    space();
    if (i < s.size() && s[i] == c) {
      ++i;
      return true;
    }
    return false;
  }
  bool word(std::string_view w) {
    if (s.substr(i, w.size()) != w) return false;
    i += w.size();
    return true;
  }

  static void utf8(std::string& out, uint32_t c) {
    if (c < 0x80) {
      out += char(c);
    } else if (c < 0x800) {
      out += char(0xC0 | (c >> 6));
      out += char(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
      out += char(0xE0 | (c >> 12));
      out += char(0x80 | ((c >> 6) & 0x3F));
      out += char(0x80 | (c & 0x3F));
    } else {
      out += char(0xF0 | (c >> 18));
      out += char(0x80 | ((c >> 12) & 0x3F));
      out += char(0x80 | ((c >> 6) & 0x3F));
      out += char(0x80 | (c & 0x3F));
    }
  }

  bool hex4(uint32_t& c) {
    if (i + 4 > s.size()) return false;
    c = 0;
    for (int k = 0; k < 4; ++k) {
      const char h = s[i++];
      c <<= 4;
      if (h >= '0' && h <= '9') c |= uint32_t(h - '0');
      else if (h >= 'a' && h <= 'f') c |= uint32_t(h - 'a' + 10);
      else if (h >= 'A' && h <= 'F') c |= uint32_t(h - 'A' + 10);
      else return false;
    }
    return true;
  }

  std::optional<std::string> string() {
    if (!eat('"')) return std::nullopt;
    std::string out;
    while (i < s.size()) {
      const char c = s[i++];
      if (c == '"') return out;
      if (uint8_t(c) < 0x20) return std::nullopt;
      if (c != '\\') {
        out += c;
        continue;
      }
      if (i >= s.size()) return std::nullopt;
      switch (s[i++]) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
          uint32_t c1;
          if (!hex4(c1)) return std::nullopt;
          if (c1 >= 0xD800 && c1 < 0xDC00) {  // a surrogate pair
            uint32_t c2;
            if (!word("\\u") || !hex4(c2) || c2 < 0xDC00 || c2 >= 0xE000) return std::nullopt;
            c1 = 0x10000 + ((c1 - 0xD800) << 10) + (c2 - 0xDC00);
          }
          utf8(out, c1);
          break;
        }
        default: return std::nullopt;
      }
    }
    return std::nullopt;
  }

  std::optional<Value> number() {
    const size_t start = i;
    if (i < s.size() && s[i] == '-') ++i;
    bool fraction = false;
    while (i < s.size() && ((s[i] >= '0' && s[i] <= '9') || s[i] == '.' || s[i] == 'e' || s[i] == 'E' || s[i] == '+' ||
                            s[i] == '-')) {
      fraction |= s[i] == '.' || s[i] == 'e' || s[i] == 'E';
      ++i;
    }
    const char* b = s.data() + start;
    const char* e = s.data() + i;
    if (!fraction) {
      int64_t n = 0;
      if (auto r = std::from_chars(b, e, n); r.ec == std::errc() && r.ptr == e) return Value(n);
    }
    // (strtod rather than from_chars: macOS has the floating-point
    // from_chars only from 13.3, and the game runs on 11. The C locale is
    // never changed, so the decimal point is '.'.)
    const std::string text(b, e);
    char* end = nullptr;
    const double d = std::strtod(text.c_str(), &end);
    if (!text.empty() && end == text.c_str() + text.size()) return Value(d);
    return std::nullopt;
  }

  std::optional<Value> value() {
    if (++depth > 64) return std::nullopt;
    space();
    if (i >= s.size()) return std::nullopt;
    std::optional<Value> out;
    const char c = s[i];
    if (c == '{') {
      ++i;
      Object o;
      if (!eat('}')) {
        do {
          auto key = string();
          if (!key || !eat(':')) return std::nullopt;
          auto v = value();
          if (!v) return std::nullopt;
          o.insert_or_assign(std::move(*key), std::move(*v));
        } while (eat(','));
        if (!eat('}')) return std::nullopt;
      }
      out = Value(std::move(o));
    } else if (c == '[') {
      ++i;
      Array a;
      if (!eat(']')) {
        do {
          auto v = value();
          if (!v) return std::nullopt;
          a.push_back(std::move(*v));
        } while (eat(','));
        if (!eat(']')) return std::nullopt;
      }
      out = Value(std::move(a));
    } else if (c == '"') {
      if (auto str = string()) out = Value(std::move(*str));
    } else if (word("true")) {
      out = Value(true);
    } else if (word("false")) {
      out = Value(false);
    } else if (word("null")) {
      out = Value(nullptr);
    } else {
      out = number();
    }
    --depth;
    return out;
  }
};

void dump_string(std::string& out, const std::string& s) {
  out += '"';
  for (const char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (uint8_t(c) < 0x20) {
          char u[8];
          std::snprintf(u, sizeof(u), "\\u%04x", unsigned(uint8_t(c)));
          out += u;
        } else {
          out += c;
        }
    }
  }
  out += '"';
}

void dump_to(std::string& out, const Value& v) {
  if (std::holds_alternative<std::nullptr_t>(v.v)) {
    out += "null";
  } else if (const bool* b = std::get_if<bool>(&v.v)) {
    out += *b ? "true" : "false";
  } else if (const int64_t* n = std::get_if<int64_t>(&v.v)) {
    out += std::to_string(*n);
  } else if (const double* d = std::get_if<double>(&v.v)) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.17g", std::isfinite(*d) ? *d : 0.0);
    out += buf;
  } else if (const std::string* s = std::get_if<std::string>(&v.v)) {
    dump_string(out, *s);
  } else if (const Array* a = std::get_if<Array>(&v.v)) {
    out += '[';
    for (size_t k = 0; k < a->size(); ++k) {
      if (k) out += ',';
      dump_to(out, (*a)[k]);
    }
    out += ']';
  } else if (const Object* o = std::get_if<Object>(&v.v)) {
    out += '{';
    bool first = true;
    for (const auto& [key, value] : *o) {
      if (!first) out += ',';
      first = false;
      dump_string(out, key);
      out += ':';
      dump_to(out, value);
    }
    out += '}';
  }
}

}  // namespace

const Value& Value::operator[](std::string_view key) const {
  if (const Object* o = std::get_if<Object>(&v))
    if (auto it = o->find(key); it != o->end()) return it->second;
  return kNull;
}

const Value& Value::operator[](size_t i) const {
  if (const Array* a = std::get_if<Array>(&v))
    if (i < a->size()) return (*a)[i];
  return kNull;
}

int64_t Value::integer(int64_t fallback) const {
  if (const int64_t* n = std::get_if<int64_t>(&v)) return *n;
  // (Converting a double outside int64's range is undefined behaviour.)
  if (const double* d = std::get_if<double>(&v)) return std::fabs(*d) < 9.2e18 ? int64_t(*d) : fallback;
  return fallback;
}

std::string Value::string(std::string fallback) const {
  if (const std::string* s = std::get_if<std::string>(&v)) return *s;
  return fallback;
}

bool Value::boolean(bool fallback) const {
  if (const bool* b = std::get_if<bool>(&v)) return *b;
  return fallback;
}

const Array& Value::array() const {
  if (const Array* a = std::get_if<Array>(&v)) return *a;
  return kNoArray;
}

std::optional<Value> parse(std::string_view text) {
  Parser p{text};
  auto v = p.value();
  p.space();
  if (!v || p.i != text.size()) return std::nullopt;
  return v;
}

std::string dump(const Value& value) {
  std::string out;
  dump_to(out, value);
  return out;
}

}  // namespace geow2::json
