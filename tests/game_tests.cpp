// Unit tests for game logic, on small hand-built inputs (no game data).

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

#include "game/bloom_weights.h"
#include "game/bullets.h"
#include "game/camera.h"
#include "game/display_gamma.h"
#include "game/grid.h"
#include "game/light_map.h"
#include "game/line_quads.h"
#include "game/model_geometry.h"
#include "game/player_motion.h"
#include "game/rotation.h"
#include "game/spawn.h"
#include "game/wanderer.h"
#include "game/world.h"

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

static void test_grid() {
  Grid g;
  g.reset(300.0f, 200.0f);
  expect(near(g.rest(0, 0).x, -300.0f) && near(g.rest(0, 0).y, -200.0f), "grid corner (0, 0)");
  expect(near(g.rest(88, 132).x, 300.0f) && near(g.rest(88, 132).y, 200.0f), "grid corner (88, 132)");
  // Nothing pushing: the mesh stays at rest.
  g.step({}, 300.0f, 200.0f);
  expect(near(g.position(44, 66).x, g.rest(44, 66).x) && near(g.position(44, 66).y, g.rest(44, 66).y),
         "grid idle stays at rest");
  // A push at the centre: a point just right of it moves right, the border stays.
  GridInputs in;
  in.pushes.push_back({{0.0f, 0.0f, 30.0f, 4.0f}});
  for (int s = 0; s < 5; ++s) g.step(in, 300.0f, 200.0f);
  const int mid_row = 44, right_col = 66 + 2;
  expect(g.position(mid_row, right_col).x > g.rest(mid_row, right_col).x, "grid push moves points away");
  expect(g.position(0, 66).x == g.rest(0, 66).x && g.position(0, 66).y == g.rest(0, 66).y, "grid border fixed");
}

static void test_firing() {
  // Fire right from rest: three bullets (side, centre, side) at 520 / 600 /
  // 520 units per second, cooldown 0.1; then the cooldown counts down (six
  // steps of 1/60 bring it to ~0) and the next volley comes on the step
  // after, so volleys are 7 steps apart.
  PlayerMotion p;
  PlayerWeapon w;
  std::vector<Bullet> out;
  player_fire_step(p, w, {1.0f, 0.0f}, 1.0f / 60.0f, kMath, out);
  expect(out.size() == 3 && near(w.cooldown, 0.1f), "three bullets and cooldown");
  if (out.size() == 3) {
    expect(near(out[1].vx, 600.0f) && near(out[1].vy, 0.0f), "centre bullet speed 600");
    const float side = std::sqrt(out[0].vx * out[0].vx + out[0].vy * out[0].vy);
    expect(near(side, 520.0f) && out[0].type == 0 && out[1].type == 2, "side bullet speed 520");
  }
  int steps = 0;
  while (out.size() == 3 && steps < 20) {
    player_fire_step(p, w, {1.0f, 0.0f}, 1.0f / 60.0f, kMath, out);
    ++steps;
  }
  expect(steps == 7, "next volley 7 steps later");
  // A bullet leaving the arena is marked on contact and removed next step.
  Bullet b;
  b.x = 296.0f;
  b.vx = 600.0f;
  expect(bullet_step(b, 1.0f / 60.0f, 300.0f, 200.0f) && b.hit && near(b.x, 298.0f), "bullet wall contact");
  expect(!bullet_step(b, 1.0f / 60.0f, 300.0f, 200.0f), "bullet removed after contact");
}

static void test_line_quads() {
  // A line along +x: the body is offset by the width across it, the caps
  // reach width * cap past each end and fade to alpha 0 there.
  LineBatch b;
  const float colour[4] = {1.0f, 0.5f, 0.25f, 1.0f};
  emit_line(b, 1.1f, 0.6f, {0, 0, 0, 1}, {10, 0, 0, 1}, colour);
  expect(b.body.size() == 4 && b.caps.size() == 8, "line quad vertex counts");
  if (b.body.size() != 4 || b.caps.size() != 8) return;
  const LineVertex* v = b.body.data();
  expect(near(v[0].x, 0) && near(v[0].y, 1.1f) && v[0].side == -1.0f, "body v0");
  expect(near(v[1].x, 0) && near(v[1].y, -1.1f) && v[1].side == 1.0f, "body v1");
  expect(near(v[2].x, 10) && near(v[2].y, -1.1f) && v[2].side == 1.0f, "body v2");
  expect(near(v[3].x, 10) && near(v[3].y, 1.1f) && v[3].side == -1.0f, "body v3");
  expect(v[0].r == 18.0f && v[0].g == 9.0f && v[0].b == 4.5f && v[0].a == 1.0f, "colour times alpha times 18");
  const LineVertex* c = b.caps.data();
  expect(near(c[2].x, -0.66f) && near(c[2].y, -1.1f) && c[2].a == 0.0f && c[2].side == 1.0f, "p0 cap tip +");
  expect(near(c[3].x, -0.66f) && near(c[3].y, 1.1f) && c[3].a == 0.0f && c[3].side == -1.0f, "p0 cap tip -");
  expect(near(c[6].x, 10.66f) && near(c[6].y, -1.1f) && c[6].a == 0.0f, "p1 cap tip +");
  expect(near(c[7].x, 10.66f) && near(c[7].y, 1.1f) && c[7].a == 0.0f, "p1 cap tip -");
  expect(c[0].a == 1.0f && c[5].a == 1.0f, "cap bases opaque");
}

static void test_bloom_weights() {
  // Radius 1, precision 1: a normalised Gaussian with sigma 1 over 15 taps.
  const auto w = blur_weights(1.0f, 1.0f);
  float total = w[0];
  for (int k = 1; k < 8; ++k) total += 2.0f * w[k];
  expect(std::fabs(total - 1.0f) < 1e-5f, "blur weights sum to 1");
  expect(near(w[0], 0.398942f) && near(w[1], 0.241971f) && w[7] < 1e-9f, "blur weights shape");
  const auto k = combine_weights(1.0f, 1.25f);
  expect(near(k[0], 0.8f) && near(k[1], 0.64f) && near(k[2], 0.512f) && near(k[3], 0.4096f) && near(k[4], 0.32768f),
         "level weights: falloff powers");
  const auto k2 = combine_weights(2.0f, 1.0f);
  expect(near(k2[0], 0.25f) && near(k2[4], 1.0f / 1024.0f), "level weights: precision powers");
}

static void test_grid_lines() {
  // A grid at rest: interior lines only, 87 * 132 horizontal and 88 * 131
  // vertical quads; every 8th line uses alpha level 0.
  Grid g;
  g.reset(300.0f, 200.0f);
  GridTuning t;
  std::vector<GridVertex> v;
  grid_lines(g, t, 1.0f, v);
  expect(v.size() == size_t(23012) * 4, "grid line quad count");
  if (v.size() < 8) return;
  // The first quad: the row 0 -> 1 vertical at column 1, width 0.84375.
  const float x = g.rest(0, 1).x;
  expect(near(v[0].x, x - 0.84375f) && near(v[1].x, x + 0.84375f), "vertical quad offsets across");
  expect(v[0].side == -1.125f && v[1].side == 1.125f && v[2].side == 1.125f && v[3].side == -1.125f, "grid sides");
  expect(near(v[0].alpha, t.alpha[1] * t.brightness) && v[0].z == -6.0f, "column alpha and depth");
  std::vector<GridVertex> w;
  grid_warp_mesh(g, t, w);
  expect(w.size() == size_t(Grid::kRows) * Grid::kCols && w[5].x == 0.0f && w[5].y == 0.0f, "warp mesh at rest");
  const std::vector<uint16_t> idx = grid_warp_indices();
  expect(idx.size() == 46464 && idx[2] == Grid::kCols + 1 && idx[3] == Grid::kCols, "warp mesh indices");
  // The fade eases by 5% of the remaining distance per step.
  expect(near(grid_fade_step(0.0f, true), 0.05f) && near(grid_fade_step(1.0f, false), 0.95f), "grid fade");
}

static void test_light_sprites() {
  std::vector<LightVertex> v;
  const float c[3] = {1.0f, 2.0f, 4.0f};
  emit_light(v, 30.0f, 10.0f, -5.0f, c);
  expect(v.size() == 4 && v[0].x == -20.0f && v[0].y == -35.0f && v[2].x == 40.0f && v[2].y == 25.0f,
         "light sprite square");
  expect(v[1].u == 1.0f && v[1].v == 0.0f && v[3].u == 0.0f && v[3].v == 1.0f && v[0].g == 3.0f,
         "light sprite uv and colour * 1.5");
  const std::vector<uint8_t> tex = light_sprite_texture();
  // Peak: the normal density at 0 (0.3989) * 255, truncated.
  expect(tex.size() == 31 * 31 && tex[15 * 31 + 15] == 101 && tex[0] == 0, "light sprite texture");
}

static void test_display_gamma() {
  // sRGB display: decode then encode, close to identity.
  const auto s = display_gamma_table(DisplayGamma::kSrgb, 2.2f);
  expect(s[0] == 0 && s[1023] == 1023 && std::abs(int(s[512]) - 512) <= 1, "sRGB ramp is identity");
  // BT.709: 8-bit 128 (table entry 514) comes out at 463 / 1023.
  const auto b = display_gamma_ramp(DisplayGamma::kBt709, 2.2f);
  expect(b[0] == 0.0f && b[255] == 1.0f && std::fabs(b[128] - 0.4526f) < 0.002f, "BT.709 ramp");
}

static void test_walls() {
  // Four sides, each an inner and an outer quad: the left side runs from
  // (-300, -200) up to (-300, 200), 1.5 either side, 0.5 past the ends.
  std::vector<GridVertex> w;
  wall_quads(300.0f, 200.0f, -6.0f, 1.01f, w);
  expect(w.size() == 32, "wall vertex count");
  if (w.size() != 32) return;
  expect(w[0].x == -301.5f && w[0].y == -200.5f && w[1].x == -298.5f && w[2].y == 200.5f && w[3].x == -301.5f,
         "left wall quad");
  expect(w[0].side == -1.0f && w[1].side == 1.0f && w[2].side == 1.0f && w[3].side == -1.0f && w[0].z == -6.0f,
         "wall sides and depth");
  // Light-map coordinates: v0 reads 200 units inside, clamped to the map.
  expect(near(w[0].u, 1.0f / 3.0f) && w[0].v == 1.0f && w[1].u == 0.0f && w[2].u == 0.0f, "wall light coordinates");
  // The outer copy sits 3 units further out on both axes.
  expect(near(w[4].x, -304.5f) && near(w[4].y, -203.5f), "outer wall");
}

static void test_world() {
  // Hash cells: 32-unit cells, wrapping at 32 (negative coordinates wrap).
  expect(World::cell_of(0.0f, 0.0f) == 0 && World::cell_of(40.0f, 0.0f) == 1 && World::cell_of(0.0f, 33.0f) == 32,
         "hash cells");
  expect(World::cell_of(-5.5f, 10.0f) == 31, "hash cell wraps");
  GameRng rng;
  rng.seed(1);
  Units units;
  World w(kMath, rng, units);
  // The four spawn angles: 0, pi/2, -pi (2 * pi / 4 rounds up past 0.5
  // turns) and -pi/2 only.
  for (int i = 0; i < 40; ++i) {
    const float a = random_right_angle(w);
    expect(a == 0.0f || near(a, 1.5707964f) || near(a, -3.1415927f) || near(a, -1.5707964f), "spawn angle");
  }
  // Placed groups: the wall nearest the player, in line with them.
  w.players.resize(1);
  w.players[0].x = 250.0f;
  w.players[0].y = 20.0f;
  const Placement p = placed_position(w);
  expect(p.x == 292.0f && p.y == 20.0f && near(p.angle, -3.1415927f), "placed group on the right wall");
  w.players[0].x = -10.0f;
  w.players[0].y = -300.0f;
  const Placement q = placed_position(w);
  expect(q.y == -192.0f && q.x == -10.0f, "placed group clamped to the bottom wall");
  // Spawn positions keep their distance from the players.
  w.players[0] = PlayerTarget{};
  for (int i = 0; i < 20; ++i) {
    const Vec2 s = spawn_position(w, 165.0f);
    expect(s.x * s.x + s.y * s.y >= 165.0f * 165.0f && std::fabs(s.x) <= 300.0f && std::fabs(s.y) <= 200.0f,
           "spawn position away from the player");
  }
  // A wanderer fades in, then drifts at a little over 50 units a second.
  Entity* e = spawn_enemy(w, 1, 0.0f, 100.0f, 50.0f);
  expect(e && e->type == 0x12 && (e->flags & kEntityFadingIn) && w.enemy_count == 1, "wanderer spawned");
  for (int i = 0; i < 300; ++i) w.update_entities();
  const float speed = std::sqrt(e->vx * e->vx + e->vy * e->vy);
  expect(!(e->flags & kEntityFadingIn) && e->fade == 1.0f, "wanderer faded in");
  // Eased toward 50 after a random kick each step, so it runs a little
  // faster on average.
  expect(speed > 45.0f && speed < 70.0f, "wanderer speed near 50");
  expect(std::fabs(e->x) <= 300.0f && std::fabs(e->y) <= 200.0f, "wanderer stays in the arena");
}

int main() {
  test_world();
  test_walls();
  test_grid_lines();
  test_light_sprites();
  test_display_gamma();
  test_line_quads();
  test_bloom_weights();
  test_firing();
  test_grid();
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
