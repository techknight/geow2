#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "extract/bytes.h"

namespace geow2 {

// Reader for XtLib "Flask" databases (Data.bin, magic FLSK; format in
// docs/formats/flask.md). Decodes records with the file's own schema, as
// the game's merge does (sub_8212D5A0 and the converters it calls), into a
// tree of values. Typed views (Model, Unit, ...) are built on top of this.
struct FlaskValue;
using FlaskList = std::vector<FlaskValue>;

struct FlaskStruct {
  std::string type;
  std::vector<std::pair<std::string, FlaskValue>> fields;  // bases' fields first, then own, in file order
  const FlaskValue* find(const std::string& name) const;
};

struct FlaskRef {
  uint32_t record;  // index into Flask::records()
};

struct FlaskValue {
  std::variant<std::monostate, bool, int64_t, uint64_t, double, std::string, FlaskRef,
               std::shared_ptr<FlaskStruct>, std::shared_ptr<FlaskList>>
      v;
  bool is_null() const { return std::holds_alternative<std::monostate>(v); }
};

class Flask {
 public:
  struct Field {
    std::string name;
    uint16_t type;    // index into types(), 0xFFFF for enum values
    uint16_t offset;  // or enum value
    uint8_t kind;     // 0 atom, 1 enum, 2 flags, 3 struct, 4 pointer, 5 record ref, 6 enum value
    uint8_t array;    // nonzero: 8-byte (chunk, first, count) array
  };
  struct Type {
    std::string name;
    uint16_t kind;  // 0 atom, 1 enum, 2 flags, 3 struct
    uint16_t atom;  // atom code (sub_8212B980) for kind 0
    uint16_t size;
    std::vector<std::pair<uint16_t, uint16_t>> bases;  // (type, offset)
    std::vector<Field> fields;
  };
  struct Record {
    uint32_t id;
    uint16_t type;
    uint16_t object;  // 0xFFFF: none
  };

  explicit Flask(Bytes file);  // throws FormatError where the game's validator fails

  const std::vector<Type>& types() const { return types_; }
  const std::vector<Record>& records() const { return records_; }
  // Decodes a record's object; null when the record has none.
  FlaskValue decode(uint32_t record) const;

 private:
  struct Chunk {
    uint16_t type, count, kind;
    size_t offset;  // file offset of the first element
  };
  struct Object {
    std::vector<Chunk> chunks;
    std::string strings;  // de-obfuscated extra bytes
  };

  uint32_t u32(size_t off) const;
  size_t elem_size(const Chunk& c) const;
  FlaskValue value(uint16_t type, uint8_t kind, size_t off, const Object& obj, int depth) const;
  FlaskValue structure(uint16_t type, size_t off, const Object& obj, int depth) const;
  FlaskValue atom(const Type& t, size_t off, const Object& obj) const;

  Bytes data_;
  std::vector<Type> types_;
  std::vector<Record> records_;
  std::vector<Object> objects_;
};

// sub_8212E0A8: the name-pool and string obfuscation of version-2 flasks.
std::string flask_deobfuscate(std::span<const uint8_t> src);

}  // namespace geow2
