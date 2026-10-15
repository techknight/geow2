// Unit tests for game logic, on small hand-built inputs (no game data).

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

#include "game/camera.h"
#include "game/model_geometry.h"
#include "game/player_motion.h"
#include "game/rotation.h"

using namespace geow2;

static int failures = 0;

static void expect(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

static bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

// Textbook constants with the same roles as the original's table (reduction
// by pi split in two parts, Taylor coefficients of sin(r)/r); the tests
// check the algorithm's structure, not the original's exact values.
static GameMath test_math() {
  GameMath::Constants c{};
  c.c00 = 1.5707963267948966;
  c.c08 = 0.3183098861837907;
  c.c10 = 1e9;
  c.c28 = 3.1415926218032837;
  c.c30 = 3.1415926535897931 - 3.1415926218032837;
  const double taylor[8] = {-1.0 / 6, 1.0 / 120, -1.0 / 5040, 1.0 / 362880, -1.0 / 39916800, 1.0 / 6227020800.0,
                            -1.0 / 1307674368000.0, 1.0 / 355687428096000.0};
  for (int i = 0; i < 8; ++i) c.poly[i] = taylor[i];
  c.one = 1.0;
  c.past_range = 0.0;
  c.c18 = 0.0f;
  c.c1c = 1.0f;
  c.c20 = -1.0f;
  c.c24 = 0.5f;
  // atan2 roles: angles exact for the axis directions used below.
  c.at_half_pi = 1.5707963267948966;
  c.at_pi = 3.1415926535897931;
  c.at_threshold = 0.2679491924311228;  // tan(pi/12)
  c.at_root3 = 1.7320508075688772;
  for (int i = 0; i < 4; ++i) c.at_p[i] = 0.0, c.at_q[i] = 0.0;
  c.at_q[0] = 1.0;
  c.at_zero = 0.0f;
  c.at_one = 1.0f;
  c.at_base[0] = 0.0;
  c.at_base[1] = 0.5235987755982988;
  c.at_base[2] = 1.5707963267948966;
  c.at_base[3] = 1.0471975511965976;
  // tan roles: the [4/4] Pade approximant
  // r (945 - 105 r^2 + r^4) / (945 - 420 r^2 + 15 r^4) in the routine's form
  // P = 1 + z (-e + z (b - z a)), Q = 1 + z (-g + z (f + z (z c - d))).
  c.tan_k = 0.6366197723675814;
  c.tan_range = 1e9;
  c.tan_hi = 1.5707963267948966;
  c.tan_lo = 0.0;
  c.tan_a = 0.0;
  c.tan_b = 1.0 / 945.0;
  c.tan_c = 0.0;
  c.tan_d = 0.0;
  c.tan_e = 1.0 / 9.0;
  c.tan_f = 1.0 / 63.0;
  c.tan_g = 4.0 / 9.0;
  c.tan_one = 1.0;
  return GameMath(c);
}

static const GameMath kMath = test_math();

static void test_game_math() {
  double worst = 0.0;
  for (int i = -2000; i <= 2000; ++i) {
    const double x = i * 0.0137;
    worst = std::fmax(worst, std::fabs(kMath.sin(x) - std::sin(x)));
    worst = std::fmax(worst, std::fabs(kMath.cos(x) - std::cos(x)));
  }
  expect(worst < 1e-12, "game sin/cos structure matches the C library");
  expect(kMath.sin(0.0) == 0.0 && kMath.cos(0.0) == 1.0, "sin(0), cos(0)");
  // atan2 special cases and axis directions.
  expect(kMath.atan2(0.0, 1.0) == 0.0, "atan2(0, 1)");
  expect(kMath.atan2(0.0, -1.0) == 3.1415926535897931, "atan2(0, -1)");
  expect(kMath.atan2(1.0, 0.0) == 1.5707963267948966, "atan2(1, 0)");
  expect(kMath.atan2(-1.0, 0.0) == -1.5707963267948966, "atan2(-1, 0)");
  expect(kMath.atan2(1.0, 1e-300) > 1.57, "atan2 steep");
  expect(kMath.atan2(-0.0, -0.0) == -3.1415926535897931, "atan2(-0, -0)");
}

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
  const ModelGeometry g = build_model_geometry(get(m), kMath);
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
  const ModelGeometry g = build_model_geometry(get(model({circle}, {})), kMath);
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
  const ModelGeometry g = build_model_geometry(get(model({xf}, {vec(1, 0, 0)})), kMath);
  expect(g.parts[0].size() == 1 && near(g.parts[0][0].p0.x, 7), "scale is prepended to the composition");
}

static void test_stick() {
  // Full deflection right: x = (32767 - 0.5) / 32767.5 ~ 1; Y inverted.
  expect(near(stick_axis(32767, false), 1.0f), "stick x full");
  expect(stick_axis(32767, true) < 0.0f, "stick y inverted");
  // Dead zone 0.25 on the clamped magnitude.
  const Vec2 small = stick_vector(0.2f, 0.0f);
  expect(small.x == 0.0f && small.y == 0.0f, "dead zone");
  const Vec2 big = stick_vector(2.0f, 0.0f);
  expect(near(big.x, 1.0f), "magnitude clamped to 1");
}

static void test_thrust_and_integrate() {
  // One frame of full thrust right from rest: v = 50 * 1 * 0.8 = 40;
  // x += 40 / 60.
  PlayerMotion p;
  player_thrust(p, {1.0f, 0.0f}, kMath);
  expect(near(p.vx, 40.0f) && near(p.vy, 0.0f), "thrust then damping");
  expect(near(p.thrust_level, 1.0f), "thrust level");
  integrate(p, 1.0f / 60.0f);
  expect(near(p.x, 40.0f / 60.0f), "integrate");
  // Heading eases a tenth of the way: target atan2(0, 1) = 0 from 0.
  expect(near(p.heading, 0.0f), "heading toward 0");
  PlayerMotion q;
  q.heading = 3.0f;
  player_thrust(q, {-1.0f, 0.0f}, kMath);  // target pi: shortest turn is +0.14159
  expect(near(q.heading, 3.0f + 0.1f * (3.14159265f - 3.0f)), "heading shortest turn");
  PlayerMotion w;
  w.heading = -3.0f;
  player_thrust(w, {-1.0f, 0.0f}, kMath);  // target pi from -3: the short way is -0.14159 (through -pi)
  expect(near(w.heading, -3.0f - 0.1f * 0.14159265f), "heading wraps through -pi");
}

static void test_arena() {
  // Radius 5 in a 300 x 200 half-extent arena: x = 299 is past 295.
  float x = 299.0f, y = 0.0f;
  Vec2 n;
  expect(arena_contact(5.0f, 300.0f, 200.0f, x, y, n), "arena contact");
  expect(near(x, 295.0f) && near(n.x, -1.0f) && near(n.y, 0.0f), "arena clamp and normal");
  float cx = 299.0f, cy = 199.0f;
  arena_contact(5.0f, 300.0f, 200.0f, cx, cy, n);
  expect(near(n.x, -0.707f) && near(n.y, -0.707f), "corner normal 0.707");
  float ix = 0.0f, iy = 0.0f;
  expect(!arena_contact(5.0f, 300.0f, 200.0f, ix, iy, n), "inside");
  // Wall slide keeps the tangential part of the velocity.
  PlayerMotion p;
  p.vx = 10.0f;
  p.vy = 3.0f;
  const bool sound = player_wall(p, {-1.0f, 0.0f});
  expect(near(p.vx, 0.0f) && near(p.vy, 3.0f) && sound && p.wall_sound_frames == 2, "wall slide");
}

static void test_camera_pieces() {
  // The in-game camera basis: looking down -z with up (0, -1, 0).
  const Mat4 b = look_at_basis({3, 4, 512, 1}, {0, 0, -512, 0}, {0, -1, 0, 0});
  expect(near(b.m[0], 1) && near(b.m[1], 0) && near(b.m[5], -1) && near(b.m[10], -1), "camera basis rows");
  expect(near(b.m[12], 3) && near(b.m[13], 4) && near(b.m[14], 512), "camera basis position");
  // Inverse times the matrix is the identity.
  const Mat4 i = mul(b, inverse(b));
  bool identity = true;
  for (int k = 0; k < 16; ++k) identity = identity && near(i.m[k], (k % 5 == 0) ? 1.0f : 0.0f);
  expect(identity, "inverse");
  // Style None placement: a point at +x on the model, heading pi/2, at (10, 20).
  const Mat4 p = placement_style_none(kMath, 10.0f, 20.0f, 1.57079633f);
  const Vec4 v = mul(Vec4{1, 0, 0, 1}, p);
  expect(near(v.x, 10.0f) && near(v.y, 21.0f), "placement rotates then translates");
  // Perspective with fov pi/2: ys = 1 / tan(pi/4) = 1.
  const Mat4 proj = perspective(kMath, 1.57079633f, 2.0f, 1.0f, 10000.0f);
  expect(near(proj.m[5], 1.0f) && near(proj.m[0], 0.5f) && proj.m[11] == 1.0f, "perspective");
}

static void test_camera_follow() {
  // One player at (100, 0): the target is half of it (50), reached at 0.15
  // per step; the zoom stays 1.
  CameraState c;
  const float x = 100.0f, y = 0.0f;
  camera_follow_single(c, &x, &y, 1, 300.0f, 200.0f);
  expect(near(c.x, 7.5f) && near(c.y, 0.0f) && near(c.zoom, 1.0f), "camera eases toward half the player position");
  for (int i = 0; i < 200; ++i) camera_follow_single(c, &x, &y, 1, 300.0f, 200.0f);
  expect(near(c.x, 50.0f), "camera settles at half the player position");
}

int main() {
  test_camera_follow();
  test_camera_pieces();
  test_game_math();
  test_stick();
  test_thrust_and_integrate();
  test_arena();
  test_mesh();
  test_circle();
  test_scale_prepended();
  std::printf(failures ? "%d failure(s)\n" : "all tests passed\n", failures);
  return failures ? 1 : 0;
}
