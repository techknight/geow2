#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace geow2::json {

// A JSON value, enough for the score server's messages: whole numbers
// stay exact (scores), others are doubles.
struct Value;
using Array = std::vector<Value>;
using Object = std::map<std::string, Value, std::less<>>;

struct Value {
  std::variant<std::nullptr_t, bool, int64_t, double, std::string, Array, Object> v;

  Value() : v(nullptr) {}
  Value(std::nullptr_t) : v(nullptr) {}
  Value(bool b) : v(b) {}
  Value(int i) : v(int64_t(i)) {}
  Value(int64_t i) : v(i) {}
  Value(uint32_t i) : v(int64_t(i)) {}
  Value(double d) : v(d) {}
  Value(const char* s) : v(std::string(s)) {}
  Value(std::string s) : v(std::move(s)) {}
  Value(Array a) : v(std::move(a)) {}
  Value(Object o) : v(std::move(o)) {}

  const Value& operator[](std::string_view key) const;  // null when absent or not an object
  const Value& operator[](size_t i) const;              // null when out of range or not an array
  int64_t integer(int64_t fallback = 0) const;          // a whole number (a double is truncated)
  std::string string(std::string fallback = {}) const;
  bool boolean(bool fallback = false) const;
  const Array& array() const;  // empty when not an array
  bool is_null() const { return std::holds_alternative<std::nullptr_t>(v); }
  bool is_object() const { return std::holds_alternative<Object>(v); }
};

std::optional<Value> parse(std::string_view text);
std::string dump(const Value& value);

}  // namespace geow2::json
