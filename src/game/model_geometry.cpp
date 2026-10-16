#include "game/model_geometry.h"

#include "game/rotation.h"
#include "game/fused.h"

#include <cmath>
#include <string>

namespace geow2 {
namespace {


// All of the model build is single precision; fused multiply-adds are
// written with fmadds (game/fused.h) where the disassembly has fmadds/fmsubs.
constexpr float kTwoPi = 6.28318548f;     // dat_821825F8
constexpr float kDegToRad = 0.0174532924f;  // dat_82000F7C
constexpr int kMaxDepth = 9;                // sub_82062460 stops at depth 9

const FlaskStruct* as_struct(const FlaskValue* v) {
  if (!v) return nullptr;
  auto* p = std::get_if<std::shared_ptr<FlaskStruct>>(&v->v);
  return p ? p->get() : nullptr;
}

const FlaskList* as_list(const FlaskValue* v) {
  if (!v) return nullptr;
  auto* p = std::get_if<std::shared_ptr<FlaskList>>(&v->v);
  return p ? p->get() : nullptr;
}

float as_float(const FlaskValue* v) {
  if (!v) return 0.0f;
  if (auto* d = std::get_if<double>(&v->v)) return float(*d);
  if (auto* i = std::get_if<int64_t>(&v->v)) return float(*i);
  return 0.0f;
}

int as_int(const FlaskValue* v) {
  if (!v) return 0;
  if (auto* i = std::get_if<int64_t>(&v->v)) return int(*i);
  if (auto* u = std::get_if<uint64_t>(&v->v)) return int(*u);
  return 0;
}

Vec4 vector3(const FlaskStruct* s, float w) {
  if (!s) return {0, 0, 0, w};
  return {as_float(s->find("x")), as_float(s->find("y")), as_float(s->find("z")), w};
}

// sub_82061B58: compose a transform list onto `parent`. A scale is
// prepended (it scales the rows of the running matrix, sub_82040F28); the
// others are appended; the result is local * parent.
Mat4 compose(const GameMath& gm, const FlaskList* transforms, const Mat4& parent) {
  Mat4 local = Mat4::identity();
  if (transforms) {
    for (const FlaskValue& tv : *transforms) {
      const FlaskStruct* t = as_struct(&tv);
      if (!t) continue;
      if (t->type == "TransformScale") {
        const Vec4 s = vector3(as_struct(t->find("scale")), 0.0f);
        for (int c = 0; c < 4; ++c) {
          local.m[c] *= s.x;
          local.m[4 + c] *= s.y;
          local.m[8 + c] *= s.z;
        }
      } else if (t->type == "TransformRotate") {
        const Vec4 r = vector3(as_struct(t->find("rotation")), 0.0f);
        local = mul(local, rotation_euler(gm, r.x * kDegToRad, r.y * kDegToRad, r.z * kDegToRad));
      } else if (t->type == "TransformAxisAngle") {
        const Vec4 a = vector3(as_struct(t->find("axis")), 0.0f);
        const float inv = 1.0f / std::sqrt((a.y * a.y + a.z * a.z) + a.x * a.x);  // fsqrts, fdivs
        local = mul(local, rotation_axis(gm, as_float(t->find("angle")) * kDegToRad, a.x * inv, a.y * inv, a.z * inv));
      } else if (t->type == "TransformTranslate") {
        const Vec4 d = vector3(as_struct(t->find("translation")), 0.0f);
        local.m[12] += d.x;
        local.m[13] += d.y;
        local.m[14] += d.z;
        local.m[15] += d.w;
      }
    }
  }
  return mul(local, parent);
}

struct Walker {
  const GameMath& gm;
  std::vector<Vec4> vertices;
  std::vector<ModelLine>* out = nullptr;

  void emit(const Vec4& a, const Vec4& b, const FlaskStruct* model_colour, const Mat4& m) {
    const FlaskStruct* c = model_colour ? as_struct(model_colour->find("colour")) : nullptr;
    const float r = c ? as_float(c->find("r")) : 0.0f, g = c ? as_float(c->find("g")) : 0.0f,
                bl = c ? as_float(c->find("b")) : 0.0f, al = c ? as_float(c->find("a")) : 0.0f;
    out->push_back({mul(a, m), mul(b, m), al * r, g * al, bl * al, 1.0f});
  }

  // sub_82062460, switching on the section type (constructor tags 1..4).
  void walk(const FlaskStruct* s, const Mat4& m, int depth) {
    if (!s || depth >= kMaxDepth) return;
    if (s->type == "SectionMesh") {
      const FlaskStruct* colour = as_struct(s->find("colour"));
      if (!colour) return;
      if (const FlaskList* segs = as_list(s->find("segments"))) {
        const int n = int(vertices.size());
        for (const FlaskValue& sv : *segs) {
          const FlaskStruct* seg = as_struct(&sv);
          if (!seg) continue;
          const int i = as_int(seg->find("i")), j = as_int(seg->find("j"));
          if (i < 0 || i >= n || j < 0 || j >= n) continue;
          emit(vertices[size_t(i)], vertices[size_t(j)], colour, m);
        }
      }
    } else if (s->type == "SectionCircle") {
      const FlaskStruct* colour = as_struct(s->find("colour"));
      if (!colour) return;
      const Vec4 c = vector3(as_struct(s->find("centre")), 1.0f);
      const float radius = as_float(s->find("radius"));
      const int sides = as_int(s->find("sides"));
      const float slip = as_float(s->find("slip"));
      const float slip_rad = slip * kDegToRad;  // fmuls
      for (int k = as_int(s->find("gap")); k < sides; ++k) {
        // fdivs of the converted ints, then fmadds (0x820626EC..0x820626F8);
        // points are fmadds of radius with sin/cos (0x82062730..0x8206278C).
        auto angle = [&](int i) { return fmadds(float(i) / float(sides), kTwoPi, slip_rad); };
        const float a0 = angle(k), a1 = angle(k + 1);
        const Vec4 p0{fmadds(radius, gm.cosf(a0), c.x), fmadds(radius, gm.sinf(a0), c.y), c.z, 1.0f};
        const Vec4 p1{fmadds(radius, gm.cosf(a1), c.x), fmadds(radius, gm.sinf(a1), c.y), c.z, 1.0f};
        emit(p0, p1, colour, m);
      }
    } else if (s->type == "SectionTransform") {
      walk(as_struct(s->find("section")), compose(gm, as_list(s->find("transforms")), m), depth + 1);
    } else if (s->type == "SectionCombine") {
      if (const FlaskList* list = as_list(s->find("sections")))
        for (const FlaskValue& v : *list) walk(as_struct(&v), m, depth + 1);
    }
  }
};

}  // namespace

ModelGeometry build_model_geometry(const FlaskStruct& model, const GameMath& gm) {
  ModelGeometry g;
  // The geometry header keeps the Model's first field, capSize (+0xC).
  g.cap_size = as_float(model.find("capSize"));
  Walker w{gm, {}, nullptr};
  if (const FlaskList* verts = as_list(model.find("vertices"))) {
    for (const FlaskValue& v : *verts) {
      const FlaskStruct* vert = as_struct(&v);
      w.vertices.push_back(vector3(vert ? as_struct(vert->find("position")) : nullptr, 1.0f));
    }
  }
  const Mat4 base = compose(gm, as_list(model.find("transform")), Mat4::identity());
  if (const FlaskList* parts = as_list(model.find("parts"))) {
    g.parts.resize(parts->size());
    for (size_t p = 0; p < parts->size(); ++p) {
      const FlaskStruct* part = as_struct(&(*parts)[p]);
      w.out = &g.parts[p];
      if (!part) continue;
      if (const FlaskList* sections = as_list(part->find("sections")))
        for (const FlaskValue& s : *sections) w.walk(as_struct(&s), base, 0);
    }
  }
  return g;
}

}  // namespace geow2
