#include "game/enemies.h"

#include <cmath>

#include "game/rotation.h"

namespace geow2 {
namespace {

constexpr float kTwoPow32 = 2.32830644e-10f;  // dat_820006E4
constexpr float kTwoPow31 = 4.65661287e-10f;  // dat_820006C8
constexpr float kPi = 3.14159274f;            // dat_820017AC, dat_82002558

// sub_82045A88: counted as an enemy.
void count_enemy(World& w, Entity& e) {
  if (!(e.flags & kEntityCounted)) {
    e.flags |= kEntityCounted;
    ++w.enemy_count;
  }
}

// The walls' reflection the grunt, duck and spinners share: v -= n *
// (2 (n . v)) (dat_820006FC).
void reflect(Entity& e, Vec2 n) {
  const float d = n.x * e.vx + n.y * e.vy;
  const float k = d * 2.0f;
  e.vx = e.vx - n.x * k;
  e.vy = e.vy - n.y * k;
}

// Toward a target at `pull` (only farther than 0.001, dat_82001A18), as
// the grunt and the spinner do; returns false without a target.
bool home(World& w, Entity& e, float pull) {
  const PlayerTarget* t = w.target(e);
  if (!t) return false;
  const float dx = t->x - e.x, dy = t->y - e.y;
  const float len = std::sqrt(dy * dy + dx * dx);
  const float inv = 1.0f / len;
  if (0.001f < len) {
    e.vx = e.vx + pull * (inv * dx);
    e.vy = e.vy + (inv * dy) * pull;
  }
  return true;
}

constexpr Vec4 kUp{0, 1, 0, 0};

}  // namespace

// ---------------------------------------------------------------------------
// Grunt

Grunt::Grunt(World& w, float px, float py) {
  x = px;
  y = py;
  unit = w.units.find("grunt");
  type = 2;
  flags |= 0x900;
  radius = 6.5f;  // dat_82001B18
  flags |= 0x14;  // fading in, touches players
  sx = sy = 1.0f;
  pull = 9.0f;
  pulse = 0.0f;
  shape_points = 8;  // the diamond at 0x82181758
  phase = float(w.rng.next()) * kTwoPow32;
  count_enemy(w, *this);
  // The colour is never set: it stays white (Unit.Grunt.Colour has no
  // reader).
}

void Grunt::update(World& w) {
  phase = std::fmaf(w.step, 5.0f, phase);  // dat_820006F0
  pulse = w.gm.cosf(phase);
  if (home(w, *this, pull)) {
    vx = vx * 0.9f;  // dat_82000D70
    vy = vy * 0.9f;
    pull = pull + 0.005f;  // dat_82001B54, no cap
  }
  w.separate(*this, radius, 1.0f);
  const float k = pulse * 0.2f;  // dat_82000D5C
  sx = k + 1.0f;
  sy = 1.0f - k;
}

void Grunt::wall(World&, Vec2 n) { reflect(*this, n); }

Mat4 Grunt::custom_placement(World& w) {
  // Leaning toward its target (or along its velocity), pulsing:
  // (S R) B with S = diag(sx, sy, 1).
  Vec4 f{vx, vy, 100.0f, 0};  // dat_82001020
  if (const PlayerTarget* t = w.target(*this)) f = {t->x - x, t->y - y, 100.0f, 0};
  const Mat4 basis = look_at_basis({x, y, 0, 1}, f, unit ? unit->up : kUp);
  const Mat4 r = rotation_axis(w.gm, heading, 0, 0, 1);
  Mat4 s = Mat4::identity();
  for (int c = 0; c < 4; ++c) {
    s.m[c] = s.m[c] * sx;
    s.m[4 + c] = s.m[4 + c] * sy;
  }
  return mul(mul(s, r), basis);
}

// ---------------------------------------------------------------------------
// Duck

namespace {

// Unit.Duck.Tick (0x8219E768, value 0x8219E788, default dat_820006C4).
constexpr float kDuckTick = 1.0f;
// Unit.Duck.Colour (0x8219E6F0; dat_820006C4, dat_82001150, dat_82001800).
constexpr float kDuckColour[4] = {1.0f, 0.0900000036f, 0.370000005f, 1.0f};

// The roll eases toward its target at most pi a second (two fsel).
void ease(float& cur, float target, float& last, float step) {
  const float d = target - cur;
  const float lo = step * -kPi, hi = step * kPi;  // dat_820010F8
  float v = lo - d >= 0.0f ? lo : d;
  v = d - hi >= 0.0f ? hi : v;
  last = v;
  cur = v + cur;
}

}  // namespace

Duck::Duck(World& w, float px, float py) {
  x = px;
  y = py;
  unit = w.units.find("duck");
  type = 0x21;
  flags |= 0x900;
  forbidden = -1;
  radius = 7.0f;  // dat_820017B0
  flip = false;
  flags |= 0x14;
  sx = sy = 2.5f;  // dat_82001158
  for (int i = 0; i < 4; ++i) colour[i] = kDuckColour[i];
  timer = (float(w.rng.next()) * kTwoPow32) * kDuckTick;
  shape_points = 16;  // two squares at 0x82181650
  count_enemy(w, *this);
}

void Duck::update(World& w) {
  for (int i = 0; i < 4; ++i) colour[i] = kDuckColour[i];
  const float t0 = timer;
  const float t = t0 + w.step;
  timer = t;
  if (!(t < kDuckTick)) {
    timer = t - kDuckTick;
    // A hop: 0 left, 1 right, 2 down, 3 up; draws 4 and 5 go along the
    // main axis toward the nearest player; never the way back.
    int r;
    do {
      r = int(w.rng.next() % 6u);
      if (r >= 4) {
        const PlayerTarget* p = w.nearest_player(*this);
        if (!p) {
          r = int(w.rng.next() & 3u);
        } else {
          const float dx = p->x - x, dy = p->y - y;
          if (std::fabs(dx) > std::fabs(dy))
            r = dx < 0.0f ? 0 : 1;
          else
            r = dy < 0.0f ? 2 : 3;
        }
      }
    } while (r == forbidden);
    const float s = flip ? 1.0f : -1.0f;  // dat_82000C88
    switch (r) {
      case 0:
        forbidden = 1;
        dir_x = -1.0f;
        dir_y = 0.0f;
        roll_y_target = std::fmaf(s, kPi, roll_y_target);
        break;
      case 1:
        forbidden = 0;
        dir_x = 1.0f;
        dir_y = 0.0f;
        roll_y_target = -std::fmaf(s, kPi, -roll_y_target);  // fnmsubs
        break;
      case 2:
        flip = !flip;
        roll_x_target = roll_x_target + kPi;
        forbidden = 3;
        dir_x = 0.0f;
        dir_y = -1.0f;
        break;
      default:
        flip = !flip;
        roll_x_target = roll_x_target - kPi;
        forbidden = 2;
        dir_x = 0.0f;
        dir_y = 1.0f;
        break;
    }
    // (Sound event 0x3A.)
  }
  heading_speed = std::fmaf(heading_target - heading, 0.2f, heading_speed * 0.8f);  // dat_82000D5C, dat_82000754
  heading = heading_speed + heading;
  ease(roll_y, roll_y_target, roll_y_step, w.step);
  ease(roll_x, roll_x_target, roll_x_step, w.step);
  // The hop's speed follows |sin(pi t)| over the tick: last step's share
  // comes off before the damping, this step's goes on after.
  constexpr float kHop = 40.0f;  // dat_8200105C
  const float s0 = std::fabs(w.gm.sinf(t0 * kPi));
  vx = vx - (dir_x * s0) * kHop;
  vy = vy - (dir_y * s0) * kHop;
  vx = vx * 0.9f;  // dat_82000D70
  vy = vy * 0.9f;
  const float lift = std::fabs(w.gm.sinf(roll_x) + w.gm.sinf(roll_y));
  radius = std::fmaf(-lift, 2.0f, 7.0f);
  w.separate(*this, radius, 1.0f);
  const float s1 = std::fabs(w.gm.sinf(timer * kPi));
  vx = (dir_x * s1) * kHop + vx;
  vy = vy + (dir_y * s1) * kHop;
}

void Duck::wall(World&, Vec2 n) { reflect(*this, n); }

Mat4 Duck::custom_placement(World& w) {
  // Rolled about y then x, lifted by |sin + sin| * 8 (dat_82000D18).
  Mat4 m = mul(rotation_axis(w.gm, roll_y, 0, 1, 0), rotation_axis(w.gm, roll_x, 1, 0, 0));
  const float lift = std::fabs(w.gm.sinf(roll_x) + w.gm.sinf(roll_y));
  m.m[12] = m.m[12] + x;
  m.m[13] = m.m[13] + y;
  m.m[14] = m.m[14] + lift * 8.0f;
  return m;
}

// ---------------------------------------------------------------------------
// Spinner

namespace {

// Unit.Spinner.Colour (0x821A0274) and Unit.TinySpinner.Colour (0x821A0538).
constexpr float kSpinnerColour[4] = {1.0f, 0.1f, 1.0f, 1.0f};
constexpr float kTinyColour[4] = {0.25f, 0.1f, 1.0f, 1.0f};

}  // namespace

Spinner::Spinner(World& w, float px, float py) {
  x = px;
  y = py;
  unit = w.units.find("spinner");
  type = 9;
  flags |= 0x900;
  phase = 0.0f;
  flags |= 0x14;
  radius = 7.0f;  // dat_820017B0
  for (int i = 0; i < 4; ++i) colour[i] = kSpinnerColour[i];
  shape_points = 12;  // 0x82182320
  count_enemy(w, *this);
}

void Spinner::update(World& w) {
  for (int i = 0; i < 4; ++i) colour[i] = kSpinnerColour[i];
  if (home(w, *this, 7.5f)) {  // dat_82001C74
    vx = vx * 0.965f;           // dat_82002594
    vy = vy * 0.965f;
  }
  phase = phase + 0.1f;  // dat_82000714
  heading = w.gm.sinf(phase);
  w.separate(*this, radius, 1.0f);
}

void Spinner::wall(World&, Vec2 n) { reflect(*this, n); }

Mat4 Spinner::custom_placement(World& w) {
  Vec4 f{0, 0, 1, 0};
  if (const PlayerTarget* t = w.target(*this)) f = {t->x - x, t->y - y, 150.0f, 0};  // dat_8200113C
  const Mat4 basis = look_at_basis({x, y, 0, 1}, f, kUp);
  return mul(rotation_axis(w.gm, heading, 0, 0, 1), basis);
}

void Spinner::kill(World& w, const Killer& k) {
  if (flags & kEntityRemoved) return;
  // Shot by a player (the killer object's type code 5): three children a
  // third of a turn apart from a random angle, each launched at 300 with
  // the shot's velocity; sound event 0x4D.
  if (k.object_type == 5) {
    const float base = std::fmaf(float(w.rng.next()), kTwoPow31, -1.0f) * kPi;
    for (int i = 0; i < 3; ++i) {
      const float a = std::fmaf(float(i) * 6.28318548f, 0.333333343f, base);  // dat_82182318, dat_820010F0
      const double t = double(a) * 0.15915494309137826;  // dat_82000D00
      const float angle = float((t - std::nearbyint(t)) * 6.2831853072);  // fctid, dat_82000CF8
      auto* c = static_cast<TinySpinner*>(
          w.add(std::make_unique<TinySpinner>(w, angle, 300.0f, x, y, k.vx, k.vy)));  // dat_82000CF4
      c->x = c->x + c->move_x;
      c->y = c->y + c->move_y;
    }
  }
  Entity::kill(w, k);
}

TinySpinner::TinySpinner(World& w, float angle, float speed, float px, float py, float vx0, float vy0) {
  x = px;
  y = py;
  unit = w.units.find("tinyspinner");
  type = 0xA;
  flags |= 0x900;
  heading = angle;
  flags |= 0x4;  // touches players at once: no fading in
  const float s = w.gm.sinf(angle), c = w.gm.cosf(angle);
  const float sv = s * speed, cv = c * speed;
  vy = vy0 + sv;
  vx = vx0 + cv;
  move_x = w.step * (cv * 2.0f);  // dat_820006FC
  move_y = (sv * 2.0f) * w.step;
  sx = sy = 1.25f;  // dat_82000768
  for (int i = 0; i < 4; ++i) colour[i] = kTinyColour[i];
  shape_points = 12;  // 0x82182408
  radius = 4.0f;      // dat_820006F4
  count_enemy(w, *this);
  spin = std::fmaf(float(w.rng.next()) * kTwoPow32, 0.01f, 0.09f);  // dat_820011B0, dat_82001150
  if ((w.rng.next() & 1u) == 0) spin = spin * -1.0f;
}

void TinySpinner::update(World& w) {
  for (int i = 0; i < 4; ++i) colour[i] = kTinyColour[i];
  vx = std::fmaf(w.gm.sinf(heading), 24.0f, vx);  // dat_82000F9C
  vy = std::fmaf(w.gm.cosf(heading), 24.0f, vy);
  vx = vx * 0.9f;
  vy = vy * 0.9f;
  heading = spin + heading;
  w.separate(*this, radius, 1.0f);
}

void TinySpinner::wall(World&, Vec2 n) { reflect(*this, n); }

void TinySpinner::draw(World& w, LineBatch& out) {
  if (!unit || !unit->has_model) return;
  const Mat4 basis = look_at_basis({x, y, 0, 1}, {vx, vy, 150.0f, 0}, kUp);
  const Mat4 m = mul(rotation_axis(w.gm, heading, 0, 0, 1), basis);
  const float white[4] = {1, 1, 1, 1};
  draw_model_part(out, unit->model, 0, m, white);
}

}  // namespace geow2
