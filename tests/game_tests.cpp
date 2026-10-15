// Unit tests for game logic, on small hand-built inputs (no game data).

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

#include "game/model_geometry.h"

using namespace geow2;

static int failures = 0;

static void expect(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

static bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

// Helpers to build decoded Flask values by hand.
static FlaskValue num(double d) { return FlaskValue{d}; }
static FlaskValue integer(int64_t i) { return FlaskValue{i}; }

static FlaskValue st(const std::string& type, std::vector<std::pair<std::string, FlaskValue>> fields) {
  auto s = std::make_shared<FlaskStruct>();
  s->type = type;
  s->fields = std::move(fields);
  return FlaskValue{s};
}

static FlaskValue list(std::vector<FlaskValue> items) { return FlaskValue{std::make_shared<FlaskList>(std::move(items))}; }

static FlaskValue vec(double x, double y, double z) { return st("Vector", {{"x", num(x)}, {"y", num(y)}, {"z", num(z)}}); }

static FlaskValue colour(double r, double g, double b, double a) {
  return st("ModelColour",
            {{"name", FlaskValue{std::string("c")}},
             {"colour", st("Colour", {{"r", num(r)}, {"g", num(g)}, {"b", num(b)}, {"a", num(a)}})}});
}

static FlaskValue model(std::vector<FlaskValue> sections, std::vector<FlaskValue> verts, std::vector<FlaskValue> transform = {}) {
  std::vector<FlaskValue> vs;
  for (auto& v : verts) vs.push_back(st("Vertex", {{"position", v}}));
  return st("Model", {{"capSize", num(1.0)},
                      {"transform", list(std::move(transform))},
                      {"parts", list({st("Part", {{"name", FlaskValue{std::string("p")}}, {"sections", list(std::move(sections))}})})},
                      {"sections", list({})},
                      {"vertices", list(std::move(vs))},
                      {"colours", list({})}});
}

static const FlaskStruct& get(const FlaskValue& v) { return *std::get<std::shared_ptr<FlaskStruct>>(v.v); }

static void test_mesh() {
  // Two vertices, one valid segment and one out of range (skipped); colour
  // premultiplied by alpha, alpha stored as 1.
  auto mesh = st("SectionMesh", {{"colour", colour(1.0, 0.5, 0.25, 0.5)},
                                 {"segments", list({st("MeshSegment", {{"i", integer(0)}, {"j", integer(1)}}),
                                                    st("MeshSegment", {{"i", integer(0)}, {"j", integer(5)}})})}});
  const auto m = model({mesh}, {vec(1, 2, 0), vec(3, 4, 0)});
  const ModelGeometry g = build_model_geometry(get(m));
  expect(g.parts.size() == 1 && g.parts[0].size() == 1, "mesh line count");
  if (!g.parts.empty() && !g.parts[0].empty()) {
    const ModelLine& l = g.parts[0][0];
    expect(near(l.p0.x, 1) && near(l.p0.y, 2) && near(l.p0.w, 1) && near(l.p1.x, 3) && near(l.p1.y, 4), "mesh endpoints");
    expect(near(l.r, 0.5f) && near(l.g, 0.25f) && near(l.b, 0.125f) && near(l.a, 1.0f), "mesh colour premultiplied");
  }
}

static void test_circle() {
  // sides 4, gap 1: segments k = 1..3 only; slip 90 degrees rotates the start.
  auto circle = st("SectionCircle", {{"colour", colour(1, 1, 1, 1)},
                                     {"centre", vec(10, 0, 0)},
                                     {"radius", num(2.0)},
                                     {"sides", integer(4)},
                                     {"slip", num(90.0)},
                                     {"gap", integer(1)}});
  const ModelGeometry g = build_model_geometry(get(model({circle}, {})));
  expect(g.parts[0].size() == 3, "circle gap skips the first segment");
  if (g.parts[0].size() == 3) {
    // k = 1: angle = 1/4 turn + 90 degrees = 180 degrees -> (10 - 2, 0).
    expect(near(g.parts[0][0].p0.x, 8) && near(g.parts[0][0].p0.y, 0), "circle slip and start");
  }
}

static void test_scale_prepended() {
  // Translate then scale in the list: the scale still applies first
  // (sub_82040F28 scales the rows), so (1, 0) -> (2, 0) -> (2 + 5, 0).
  auto mesh = st("SectionMesh", {{"colour", colour(1, 1, 1, 1)},
                                 {"segments", list({st("MeshSegment", {{"i", integer(0)}, {"j", integer(0)}})})}});
  auto xf = st("SectionTransform", {{"section", mesh},
                                    {"transforms", list({st("TransformTranslate", {{"translation", vec(5, 0, 0)}}),
                                                         st("TransformScale", {{"scale", vec(2, 2, 2)}})})}});
  const ModelGeometry g = build_model_geometry(get(model({xf}, {vec(1, 0, 0)})));
  expect(g.parts[0].size() == 1 && near(g.parts[0][0].p0.x, 7), "scale is prepended to the composition");
}

int main() {
  test_mesh();
  test_circle();
  test_scale_prepended();
  std::printf(failures ? "%d failure(s)\n" : "all tests passed\n", failures);
  return failures ? 1 : 0;
}
