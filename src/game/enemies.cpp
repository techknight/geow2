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

// The angle wrap rockets, snakes and spinners' children use: in double,
// a / 2pi minus its nearest integer (fctid), times 2pi (dat_82000D00,
// dat_82000CF8, both f64), rounded once.
float wrap_angle(float a) {
  const double t = double(a) * 0.15915494309137826;
  return float((t - std::nearbyint(t)) * 6.2831853072);
}

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
      const float angle = wrap_angle(a);
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

// ---------------------------------------------------------------------------
// Weaver

namespace {

// Unit.Weaver.Colour (0x821A05C0; dat_82000720, dat_820006C4, dat_820011B0).
constexpr float kWeaverColour[4] = {0.25f, 1.0f, 0.00999999978f, 1.0f};

// How directly shot `b` flies at the point (px, py): (cos of the angle
// between its velocity and the way from it to the point + 1) / 2; and the
// unit way from the shot to the point.
float aim(const Bullet& b, float px, float py, float& dxn, float& dyn) {
  const float dx = px - b.x, dy = py - b.y;
  const float invd = 1.0f / std::sqrt(dy * dy + dx * dx);
  const float invv = 1.0f / std::sqrt(b.vy * b.vy + b.vx * b.vx);
  dxn = invd * dx;
  dyn = invd * dy;
  const float vxn = invv * b.vx, vyn = b.vy * invv;
  return ((vyn * dyn + vxn * dxn) + 1.0f) * 0.5f;  // dat_82000724
}

// sub_82052860(r, &p): of the shots in the cells around p within r, the
// one with the lowest (1 - aim / 2) * distance squared. The cells are
// walked as the spatial hash walks (x fastest, wrapping at 32); within a
// cell the port takes the shots in firing order.
const Bullet* threat(const World& w, float r, float px, float py) {
  if (!w.shots) return nullptr;
  const uint32_t x0 = uint32_t(int(px - r) >> 5) & 0x1F, y0 = uint32_t(int(py - r) >> 5) & 0x1F;
  const uint32_t x1 = uint32_t(int(px + r) >> 5) & 0x1F, y1 = uint32_t(int(py + r) >> 5) & 0x1F;
  const Bullet* best = nullptr;
  float best_score = 0.0f;
  uint32_t cx = (x0 - 1) & 0x1F, cy = y0;
  for (;;) {
    if (cx == x1) {
      if (cy == y1) break;
      cy = (cy + 1) & 0x1F;
      cx = x0;
    } else {
      cx = (cx + 1) & 0x1F;
    }
    const int cell = int(cy << 5 | cx);
    for (const Bullet& b : *w.shots) {
      if (b.removed || World::cell_of(b.x, b.y) != cell) continue;
      const float dx = px - b.x, dy = py - b.y;
      const float d2 = dy * dy + dx * dx;
      if (!(d2 <= r * r)) continue;
      float dxn, dyn;
      const float a = aim(b, px, py, dxn, dyn);
      const float score = std::fmaf(-a, 0.5f, 1.0f) * d2;
      if (!best || score < best_score) {
        best = &b;
        best_score = score;
      }
    }
  }
  return best;
}

}  // namespace

Weaver::Weaver(World& w, float px, float py) {
  x = px;
  y = py;
  unit = w.units.find("weaver");
  type = 6;
  flags |= 0x900;
  timer = 0.0f;
  side = 0.0f;
  flags |= 0x14;
  for (int i = 0; i < 4; ++i) colour[i] = kWeaverColour[i];
  shape_points = 16;  // 0x82182528
  radius = 6.0f;      // dat_82001414
  count_enemy(w, *this);
}

void Weaver::update(World& w) {
  for (int i = 0; i < 4; ++i) colour[i] = kWeaverColour[i];
  if (const PlayerTarget* t = w.target(*this)) {
    // Away from the shot most likely to hit: 90 per step times 1 - (a -
    // 0.95) * 20 above 0.95, sqrt(a) above 0.375, else nothing.
    if (const Bullet* b = threat(w, 64.0f, x, y)) {  // dat_82001A0C
      float dxn, dyn;
      const float a = aim(*b, x, y, dxn, dyn);
      float g = 0.0f;
      if (a > 0.95f)  // dat_82001174
        g = std::fmaf(-(a - 0.95f), 20.0f, 1.0f);  // dat_82000CE8
      else if (a > 0.375f)  // dat_820022FC
        g = std::sqrt(a);
      g = g * 90.0f;  // dat_82001100
      vx = vx + dxn * g;
      vy = vy + dyn * g;
    }
    timer = timer - w.step;
    const float tx = t->x - x, ty = t->y - y;
    if (timer <= 0.0f) {
      side = std::fmaf(float(w.rng.next()), kTwoPow31, -1.0f) * 9.0f;  // sub_82030220(dat_82001B14)
      timer = std::fmaf(float(w.rng.next()), kTwoPow32, 1.0f) * 0.2f;  // dat_82000D5C
    }
    const float len = std::sqrt(ty * ty + tx * tx);
    const float inv = 1.0f / len;
    const float nx = inv * tx, ny = inv * ty;
    if (0.001f < len) {
      vx = nx * 18.0f + vx;  // dat_82002728
      vy = ny * 18.0f + vy;
      vx = vx + ny * side;
      vy = vy + (-nx) * side;
    }
    vx = vx * 0.9f;  // dat_82000D70
    vy = vy * 0.9f;
  }
  // It turns with the square of its speed.
  const float m = std::sqrt(vy * vy + vx * vx) * w.step;
  heading = std::fmaf(m * m, 0.01f, heading);  // dat_820011B0
  w.separate(*this, radius, 1.0f);
}

void Weaver::wall(World&, Vec2 n) { reflect(*this, n); }

// ---------------------------------------------------------------------------
// Rocket

namespace {

// Unit.Rocket.Colour (0x8219FE58; dat_820006C4, dat_82000720, dat_820011B0).
constexpr float kRocketColour[4] = {1.0f, 0.25f, 0.00999999978f, 1.0f};
// Unit.Rocket tuning (values at 0x8219FD40 .. 0x8219FE44).
constexpr float kRocketSpeed = 12.5f;
constexpr float kRocketDrag = 0.95f;
constexpr float kRocketBrake = 0.95f;
constexpr float kRocketLookAhead = 70.0f;
constexpr float kRocketDampRotation = 0.25f;
constexpr float kRocketMaxRotation = 0.15f;

}  // namespace

Rocket::Rocket(World& w, float angle, float px, float py, bool v) {
  x = px;
  y = py;
  unit = w.units.find("rocket");
  type = 0x16;
  flags |= 0x900;
  variant = v;
  radius = 5.5f;  // dat_8200152C
  flags |= 0x14;
  sx = sy = 9.0f;  // dat_82001B14
  heading = angle;
  turning = false;
  axis = angle;
  turn_left = 0.0f;
  sign = 1.0f;
  roll = std::fmaf(float(w.rng.next()), kTwoPow31, -1.0f) * kPi;  // dat_82002128
  for (int i = 0; i < 4; ++i) colour[i] = kRocketColour[i];
  shape_points = 8;  // 0x82181E90
  count_enemy(w, *this);
}

void Rocket::update(World& w) {
  for (int i = 0; i < 4; ++i) colour[i] = kRocketColour[i];
  if (!turning) {
    const float s = w.gm.sinf(axis), c = w.gm.cosf(axis);
    const float ay = sign * s, ax = sign * c;
    vy = vy + ay * kRocketSpeed;
    vx = vx + ax * kRocketSpeed;
    vx = kRocketDrag * vx;
    vy = vy * kRocketDrag;
    // Looking 70 ahead: past a wall while heading out, it turns back.
    float lx = ax * kRocketLookAhead + x, ly = y + ay * kRocketLookAhead;
    Vec2 n;
    if (arena_contact(radius, w.half_x, w.half_y, lx, ly, n)) {
      const float s2 = w.gm.sinf(axis), c2 = w.gm.cosf(axis);
      const float dot = (s2 * sign) * n.y + (c2 * sign) * n.x;
      if (dot < -0.5f) {  // dat_82001230
        turning = true;
        sign = sign * -1.0f;
        turn_left = w.random_float(1.0f) > 0.5f ? kPi : -kPi;  // sub_82030430
        // (Sound event 0x39.)
      }
    }
  } else {
    // Turning by a quarter of what is left, at most 0.15 a step, braking.
    const float t = turn_left * kRocketDampRotation;
    float st = -kRocketMaxRotation - t >= 0.0f ? -kRocketMaxRotation : t;
    st = t - kRocketMaxRotation >= 0.0f ? kRocketMaxRotation : st;
    const float h = st + heading;
    turn_left = turn_left - st;
    heading = wrap_angle(h);
    vx = vx * kRocketBrake;
    vy = vy * kRocketBrake;
    if (std::fabs(turn_left) < 0.01f) {  // dat_820011B0
      heading = turn_left + heading;
      turn_left = 0.0f;
      turning = false;
    }
  }
  roll = wrap_angle(roll + 0.1f);  // dat_82000714
}

void Rocket::wall(World&, Vec2 n) { reflect(*this, n); }

Mat4 Rocket::custom_placement(World& w) {
  // Rolled about its x axis, then turned by the heading, then moved.
  Mat4 m = mul(rotation_axis(w.gm, roll, 1, 0, 0), rotation_axis(w.gm, heading, 0, 0, 1));
  m.m[12] = m.m[12] + x;
  m.m[13] = m.m[13] + y;
  return m;
}

// ---------------------------------------------------------------------------
// Proton

Proton::Proton(World& w, float angle, float speed, float px, float py) {
  x = px;
  y = py;
  unit = w.units.find("proton");
  type = 4;
  flags |= 0x900;
  delay = 0.166f;  // dat_82002000
  phase = (float(w.rng.next()) * kTwoPow32) * 6.28318548f;  // dat_82181D7C
  pulse = 1.0f;
  jitter_time = 0.0f;
  jitter_x = jitter_y = 0.0f;
  flags |= 0x44;  // shootable, harmless until the delay runs out
  heading = angle;
  vx = (w.gm.cosf(angle) * speed) * 1.3f;  // dat_82001A00
  vy = (w.gm.sinf(angle) * speed) * 1.3f;
  colour[0] = 0.2f;  // dat_82000D5C
  colour[1] = 0.6f;  // dat_82001160
  colour[2] = 1.0f;
  colour[3] = 1.0f;
  radius = 4.75f;  // dat_82001FFC
  spin = (w.rng.next() & 1u) ? -1.0f : 1.0f;
  shape_points = 0x18;  // the ring at 0x8219FA08
  count_enemy(w, *this);
}

void Proton::update(World& w) {
  const PlayerTarget* t = w.target(*this);
  if (delay > 0.0f) {
    delay = delay - w.step;
    if (delay <= 0.0f) flags &= ~0x40u;
  } else if (t) {
    const float dx = t->x - x, dy = t->y - y;
    const float len = std::sqrt(dy * dy + dx * dx);
    const float inv = 1.0f / len;
    const float nx = inv * dx, ny = inv * dy;
    if (len > 0.001f) {  // dat_82001A18
      vx = vx + nx * 8.0f;  // dat_82000D18
      vy = ny * 8.0f + vy;
    }
    jitter_time = jitter_time - w.step;
    if (jitter_time <= 0.0f) {
      const Vec2 j = w.random_vector(4.0f);  // dat_820006F4
      jitter_x = j.x;
      jitter_y = j.y;
      jitter_time = (w.random_float(1.0f) + 1.0f) * 0.2f;  // dat_82000D5C
    }
    vx = jitter_x + vx;
    vy = jitter_y + vy;
    vx = vx * 0.97f;  // dat_820019CC
    vy = vy * 0.97f;
  }
  phase = phase + 0.25f;  // dat_82000720
  pulse = std::fmaf(w.gm.cosf(phase), 0.5f, 1.0f);
  const float speed = std::sqrt(vy * vy + vx * vx);
  heading = std::fmaf((speed * spin) * w.step, 0.05f, heading);  // dat_82000D60
}

void Proton::wall(World&, Vec2 n) { reflect(*this, n); }

void Proton::draw(World& w, LineBatch& out) {
  if (!unit || !unit->has_model) return;
  const Mat4 m = placement(w);
  if (!(flags & kEntityFadingIn)) {
    const float c[4] = {1.0f, 1.0f, 1.0f, pulse};  // a brightness pulse
    draw_model_part(out, unit->model, 0, m, c);
    return;
  }
  for (int i = 0; i < 2; ++i) {
    // The alpha folds back above 1; the size does not (sub_82053A80).
    const float g = float(std::fmod(double(fade * 3.0f), 1.0)) + float(i);
    float f = g;
    if (f >= 1.0f) f = 2.0f - f;
    if (!(f >= 0.025f)) continue;  // dat_82000D64
    const float q = std::fmaf(g, 1.4f, 0.1f);  // dat_82000FF8, dat_82000714
    Mat4 scaled = m;
    for (int r = 0; r < 3; ++r)
      for (int c = 0; c < 4; ++c) scaled.m[r * 4 + c] = m.m[r * 4 + c] * q;
    const float ghost[4] = {1.0f, 1.0f, 1.0f, f};
    draw_model_part(out, unit->model, 0, scaled, ghost);
  }
}

}  // namespace geow2
