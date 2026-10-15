#include "data/flask.h"

#include <cstring>

namespace geow2 {
namespace {

constexpr int kMaxDepth = 32;

}  // namespace

std::string flask_deobfuscate(std::span<const uint8_t> src) {
  // sub_8212E0A8: rolling key from 0xCC; the last byte becomes the terminator.
  std::string out;
  if (src.empty()) return out;
  uint8_t key = 0xCC;
  for (size_t i = 0; i + 1 < src.size(); ++i) {
    const uint8_t o = uint8_t(key - (src[i] ^ 0xB3));
    out += char(o);
    key = uint8_t(key - uint8_t(i) + o);
  }
  out += '\0';
  return out;
}

const FlaskValue* FlaskStruct::find(const std::string& name) const {
  for (const auto& [n, v] : fields)
    if (n == name) return &v;
  return nullptr;
}

uint32_t Flask::u32(size_t off) const {
  need(data_, off, 4, "flask value");
  return be32(&data_[off]);
}

Flask::Flask(Bytes file) : data_(std::move(file)) {
  // Header and section bounds (sub_8212C558).
  need(data_, 0, 0x48, "flask header");
  if (std::memcmp(data_.data(), "FLSK", 4)) throw FormatError("flask: bad magic");
  const uint32_t version = be32(&data_[4]);
  if (version != 1 && version != 2) throw FormatError("flask: unsupported version");
  uint32_t w[18];
  for (int i = 0; i < 18; ++i) w[i] = be32(&data_[size_t(i) * 4]);
  struct Section {
    uint32_t off, count, stride;
  };
  const Section types{w[2], w[3], 12}, bases{w[4], w[5], 4}, fields{w[6], w[7], 8}, pool{w[8], w[9], 1},
      records{w[10], w[11], 8}, objects{w[12], w[13], 16}, chunks{w[14], w[15], 6}, blob{w[16], w[17], 1};
  for (const Section& s : {types, bases, fields, pool, records, objects, chunks, blob})
    need(data_, s.off, size_t(s.count) * s.stride, "flask section");

  const std::span<const uint8_t> pool_bytes(&data_[pool.off], pool.count);
  const std::string names =
      version == 1 ? std::string(pool_bytes.begin(), pool_bytes.end()) : flask_deobfuscate(pool_bytes);
  auto name_at = [&](uint16_t off) {
    if (off >= names.size()) throw FormatError("flask: name offset out of range");
    return std::string(names.c_str() + off);
  };

  // Types with their field and base runs (consecutive, in type order).
  uint32_t next_field = 0, next_base = 0;
  for (uint32_t i = 0; i < types.count; ++i) {
    const uint8_t* p = &data_[types.off + size_t(i) * 12];
    Type t;
    t.name = name_at(be16(p));
    const uint16_t nfields = be16(p + 2), nbases = be16(p + 4);
    t.kind = be16(p + 6);
    t.atom = be16(p + 8);
    t.size = be16(p + 10);
    if (t.kind > 3) throw FormatError("flask: bad type kind");
    if (next_field + nfields > fields.count || next_base + nbases > bases.count)
      throw FormatError("flask: field or base run out of range");
    for (uint16_t j = 0; j < nbases; ++j) {
      const uint8_t* b = &data_[bases.off + size_t(next_base + j) * 4];
      t.bases.emplace_back(be16(b), be16(b + 2));
    }
    for (uint16_t j = 0; j < nfields; ++j) {
      const uint8_t* f = &data_[fields.off + size_t(next_field + j) * 8];
      t.fields.push_back({name_at(be16(f)), be16(f + 2), be16(f + 4), f[6], f[7]});
      if (f[6] > 6) throw FormatError("flask: bad field kind");
    }
    next_field += nfields;
    next_base += nbases;
    types_.push_back(std::move(t));
  }
  if (next_field != fields.count || next_base != bases.count) throw FormatError("flask: field or base count mismatch");
  for (const Type& t : types_) {
    for (const auto& [bt, bo] : t.bases)
      if (bt >= types_.size() || types_[bt].kind != 3 || bo + types_[bt].size > t.size)
        throw FormatError("flask: bad base");
    if (t.kind != 3) continue;
    for (const Field& f : t.fields) {
      if (f.type >= types_.size()) throw FormatError("flask: field type out of range");
      const size_t need_size = f.array ? 8 : (f.kind >= 4 ? 4 : types_[f.type].size);
      if (size_t(f.offset) + need_size > t.size) throw FormatError("flask: field outside its struct");
    }
  }

  for (uint32_t i = 0; i < records.count; ++i) {
    const uint8_t* p = &data_[records.off + size_t(i) * 8];
    Record r{be32(p), be16(p + 4), be16(p + 6)};
    if (r.type >= types_.size() || (r.object != 0xFFFF && r.object >= objects.count))
      throw FormatError("flask: bad record");
    if (i && r.id <= records_.back().id) throw FormatError("flask: record IDs not increasing");
    records_.push_back(r);
  }

  // Objects: chunk runs and their data/extra in the blob (sub_8212C558, sub_8212CAD0).
  uint32_t next_chunk = 0;
  size_t cursor = blob.off;
  for (uint32_t i = 0; i < objects.count; ++i) {
    const uint8_t* p = &data_[objects.off + size_t(i) * 16];
    const uint32_t nchunks = be32(p + 4), dsize = be32(p + 8), xsize = be32(p + 12);
    if (be32(p) >= records.count || next_chunk + nchunks > chunks.count) throw FormatError("flask: bad object");
    Object obj;
    size_t pos = 0;
    for (uint32_t c = 0; c < nchunks; ++c) {
      const uint8_t* q = &data_[chunks.off + size_t(next_chunk + c) * 6];
      Chunk ch{be16(q), be16(q + 2), be16(q + 4), cursor + pos};
      if (ch.type >= types_.size() || ch.kind > 2) throw FormatError("flask: bad chunk");
      pos = (pos + size_t(ch.count) * elem_size(ch) + 3) & ~size_t(3);
      obj.chunks.push_back(ch);
    }
    if (pos != dsize) throw FormatError("flask: object size mismatch");
    if (cursor + dsize + xsize > size_t(blob.off) + blob.count) throw FormatError("flask: object outside the blob");
    if (xsize) {
      const std::span<const uint8_t> x(&data_[cursor + dsize], xsize);
      obj.strings = version == 1 ? std::string(x.begin(), x.end()) : flask_deobfuscate(x);
    }
    next_chunk += nchunks;
    cursor += dsize + xsize;
    objects_.push_back(std::move(obj));
  }
  if (next_chunk != chunks.count || cursor != size_t(blob.off) + blob.count)
    throw FormatError("flask: chunk or blob total mismatch");
}

size_t Flask::elem_size(const Chunk& c) const { return c.kind == 0 ? types_[c.type].size : 4; }

FlaskValue Flask::decode(uint32_t record) const {
  if (record >= records_.size()) throw FormatError("flask: record out of range");
  const Record& r = records_[record];
  if (r.object == 0xFFFF) return {};
  const Object& obj = objects_[r.object];
  if (obj.chunks.empty()) return {};
  return structure(obj.chunks[0].type, obj.chunks[0].offset, obj, 0);
}

FlaskValue Flask::atom(const Type& t, size_t off, const Object& obj) const {
  // sub_8212B980.
  switch (t.atom) {
    case 1:
      return {u32(off) != 0};
    case 2:
      return {int64_t(int8_t(u32(off)))};
    case 3:
      return {int64_t(int16_t(u32(off)))};
    case 4:
      return {int64_t(int32_t(u32(off)))};
    case 6:
      return {uint64_t(uint8_t(u32(off)))};
    case 7:
      return {uint64_t(uint16_t(u32(off)))};
    case 8:
    case 13:
      return {uint64_t(u32(off))};
    case 5:
      return {int64_t((uint64_t(u32(off)) << 32) | u32(off + 4))};
    case 9:
      return {(uint64_t(u32(off)) << 32) | u32(off + 4)};
    case 10: {
      const uint32_t b = u32(off);
      float f;
      std::memcpy(&f, &b, 4);
      return {double(f)};
    }
    case 11: {
      const uint64_t b = (uint64_t(u32(off)) << 32) | u32(off + 4);
      double f;
      std::memcpy(&f, &b, 8);
      return {f};
    }
    case 12: {
      const uint32_t s = u32(off);
      if (s >= obj.strings.size()) return {};
      return {std::string(obj.strings.c_str() + s)};
    }
  }
  return {};
}

FlaskValue Flask::value(uint16_t type, uint8_t kind, size_t off, const Object& obj, int depth) const {
  if (depth > kMaxDepth) throw FormatError("flask: nesting too deep");
  const Type& t = types_[type];
  switch (kind) {
    case 0:
      return atom(t, off, obj);
    case 1: {  // enum: index into the file type's values
      const uint32_t i = u32(off);
      return i < t.fields.size() ? FlaskValue{t.fields[i].name} : FlaskValue{};
    }
    case 2: {  // flags: bit k = value k
      const uint32_t m = u32(off);
      auto list = std::make_shared<FlaskList>();
      for (size_t k = 0; k < t.fields.size() && k < 32; ++k)
        if (m >> k & 1) list->push_back({t.fields[k].name});
      return {list};
    }
    case 3:
      return structure(type, off, obj, depth + 1);
    case 4: {  // pointer within the object (sub_8212BAF8)
      need(data_, off, 4, "flask pointer");
      const uint16_t c = be16(&data_[off]), e = be16(&data_[off + 2]);
      if (c >= obj.chunks.size()) return {};
      const Chunk& ch = obj.chunks[c];
      if (ch.kind != 0 || e >= ch.count) return {};
      return structure(ch.type, ch.offset + size_t(e) * elem_size(ch), obj, depth + 1);
    }
    case 5: {  // record reference
      const uint32_t r = u32(off);
      return r < records_.size() ? FlaskValue{FlaskRef{r}} : FlaskValue{};
    }
  }
  return {};
}

FlaskValue Flask::structure(uint16_t type, size_t off, const Object& obj, int depth) const {
  if (depth > kMaxDepth) throw FormatError("flask: nesting too deep");
  const Type& t = types_[type];
  if (t.kind != 3) return value(type, uint8_t(t.kind), off, obj, depth + 1);
  auto s = std::make_shared<FlaskStruct>();
  s->type = t.name;
  for (const auto& [bt, bo] : t.bases) {
    const FlaskValue base = structure(bt, off + bo, obj, depth + 1);
    if (auto* b = std::get_if<std::shared_ptr<FlaskStruct>>(&base.v))
      for (auto& f : (*b)->fields) s->fields.push_back(f);
  }
  for (const Field& f : t.fields) {
    const size_t fo = off + f.offset;
    if (!f.array) {
      s->fields.emplace_back(f.name, value(f.type, f.kind, fo, obj, depth + 1));
      continue;
    }
    // Array (sub_8212BB80): chunk, first element, count.
    need(data_, fo, 8, "flask array");
    const uint16_t c = be16(&data_[fo]), first = be16(&data_[fo + 2]);
    const uint32_t count = be32(&data_[fo + 4]);
    auto list = std::make_shared<FlaskList>();
    const uint16_t want_kind = f.kind == 4 ? 1 : f.kind == 5 ? 2 : 0;
    if (count && c < obj.chunks.size()) {
      const Chunk& ch = obj.chunks[c];
      if (ch.kind == want_kind && size_t(first) + count <= ch.count) {
        for (uint32_t k = 0; k < count; ++k) {
          const size_t eo = ch.offset + (size_t(first) + k) * elem_size(ch);
          if (ch.kind == 0)
            list->push_back(value(ch.type, f.kind < 3 ? f.kind : 3, eo, obj, depth + 1));
          else
            list->push_back(value(ch.type, ch.kind == 1 ? 4 : 5, eo, obj, depth + 1));
        }
      }
    }
    s->fields.emplace_back(f.name, FlaskValue{list});
  }
  return {s};
}

}  // namespace geow2
