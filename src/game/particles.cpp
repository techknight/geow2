#include "game/particles.h"
#include "game/fused.h"

#include <cmath>

namespace geow2 {
namespace {

// fsel(a, c, b): a >= 0 ? c : b.
double fsel(double a, double c, double b) { return a >= 0.0 ? c : b; }

}  // namespace

void Particles::add(float x, float y, float z, float vx, float vy, float vz, const float colour[3]) {
  if (full()) return;
  Particle p{x, y, z, vx, vy, vz, colour[0], colour[1], colour[2], 4.0f, 0.0f};
  p.speed = std::sqrt(fmadds(vy, vy, fmadds(vx, vx, vz * vz)));
  live_.push_back(p);
}

void Particles::step(const ParticleTuning& t, float dt, float half_x, float half_y,
                     const std::vector<ParticleAttractor>& attractors, const std::vector<ParticleRepulsor>& repulsors,
                     std::vector<LineVertex>& quads, std::vector<LightVertex>& lights) {
  size_t kept = 0;
  for (size_t i = 0; i < live_.size(); ++i) {
    const Particle p = live_[i];
    if (std::signbit(p.intensity)) continue;  // the int test of +0x24: dead below 0

    // Move by the damped velocity.
    float vx = p.vx * t.damp, vy = p.vy * t.damp;
    const float vz0 = p.vz * t.damp;
    const float nx = p.x + vx, ny = p.y + vy, nz = p.z + vz0;
    // Arena edges: positions mirrored back in, velocities times 0.75, or
    // -0.75 across a crossed edge (dat_82002780, dat_82002A68).
    const float m_left = half_x + nx, m_right = half_x - nx, m_bottom = half_y + ny, m_top = half_y - ny;
    const double fx = fsel(m_left, fsel(m_right, 0.75, -0.75), -0.75);
    const double fy = fsel(m_bottom, fsel(m_top, 0.75, -0.75), -0.75);
    const double x1 = fsel(m_left, nx, -double(half_x) - m_left);
    const double y1 = fsel(m_bottom, ny, -double(half_y) - m_bottom);
    const float px = float(fsel(m_right, x1, double(half_x) + m_right));
    const float py = float(fsel(m_top, y1, double(half_y) + m_top));
    const double margin = fsel(fsel(m_left, m_right, m_left), fsel(m_bottom, m_top, m_bottom), fsel(m_left, m_right, m_left));
    vx = float(fsel(margin, vx, fx * vx));
    vy = float(fsel(margin, vy, fy * vy));
    float vz = vz0;

    // Swirl around each attractor nearer than the edge of its unit sphere.
    for (const ParticleAttractor& a : attractors) {
      const float dz = a.z - nz, dx = a.x - px, dy = a.y - py;
      const float d2 = fmadds(dy, dy, fmadds(dx, dx, dz * dz));
      // fres in the original: an exact reciprocal here (DIFFERENCES.md).
      float k = (1.0f / d2) * 256.0f;  // dat_82001A04
      k = 0.5f - k >= 0.0f ? k : 0.5f;
      const float X = (dy - dx) * k + dx, Y = (-dx - dy) * k + dy, Z = 0.0f * k + dz;
      const double s2 = double(((Z * Z + Y * Y) + X * X) + 1e-5f);
      const double rs = emulator_rounding ? emulator_frsqrte(s2) : 1.0 / std::sqrt(s2);  // frsqrte, exact here
      const float nvx = float(X * rs) * k + vx, nvy = float(Y * rs) * k + vy, nvz = float(Z * rs) * k + vz;
      if (d2 - 1.0f >= 0.0f) {
        vx = nvx;
        vy = nvy;
        vz = nvz;
      }
    }
    // Pushed from each repulsor.
    for (const ParticleRepulsor& r : repulsors) {
      const float dy = r.y - py, dz = r.z - nz, dx = r.x - px;
      const float d = std::sqrt(fmadds(dx, dx, fmadds(dz, dz, dy * dy)));
      const float inv = 1.0f / d;
      float k = (1.0f / d) * 16.0f;  // fres, dat_82001104
      k = 0.5f - k >= 0.0f ? k : 0.5f;
      const float ax = (inv * dx) * k, ay = (dy * inv) * k;
      const float nvz = fmadds(-(inv * dz), k, vz);
      if (d - 1.0f >= 0.0f) {
        vz = nvz;
        vx = vx - ax * r.strength;
        vy = vy - ay * r.strength;
      }
    }

    // Speed smoothed toward |v|; brightness from the intensity, the speed
    // and how far it went.
    const float vmag = std::sqrt(fmadds(vx, vx, fmadds(vz, vz, vy * vy)));
    const float speed = fmadds(vmag - p.speed, 0.75f, p.speed);
    float a = p.intensity * 2.25f;  // dat_82002A64
    a = 1.5f - a >= 0.0f ? a : 1.5f;
    const float dmx = px - p.x, dmy = py - p.y;
    const float sp = speed * 0.75f;
    const float b = 1.0f - sp >= 0.0f ? sp : 1.0f;
    const double rs = 1.0 / std::sqrt(double(fmadds(dmy, dmy, dmx * dmx)));  // frsqrte, exact here
    const float cc = (sp - 2.0f) * 0.5f;
    const float cl = float(fsel(double(cc) - 1.0, 1.0, fsel(-double(cc), 0.0, cc)));
    const float ux = float(dmx * rs), uy = float(dmy * rs);
    const float bright = (a * fmadds((cl * cl) * cl, 0.25f, 1.0f)) * (b * b);
    const double fade = bright - 0.001f >= 0.0f ? 1.0 : 0.75;  // dat_82001A18
    const float intensity = float(std::fma(fade, double(p.intensity), -double(dt)));

    Particle& q = live_[kept++];
    q = Particle{px, py, nz, vx, vy, vz, p.r, p.g, p.b, intensity, speed};

    // The streak: from the old position to the new, half-width
    // Particle.LineWidth across, Particle.LineHeight beyond each end; the
    // last value runs -1 (old end) to +1 (new end).
    const float R = bright * p.r, G = bright * p.g, B = bright * p.b;
    const float wy = t.line_width * uy, wx = t.line_width * ux;
    const float hx = t.line_height * ux, hy = t.line_height * uy;
    quads.push_back({(p.x - wy) - hx, (wx + p.y) - hy, p.z, -1.0f, R, G, B, -1.0f});
    quads.push_back({(wy + p.x) - hx, (p.y - wx) - hy, p.z, 1.0f, R, G, B, -1.0f});
    quads.push_back({(hx + wy) + px, (py - wx) + hy, nz, 1.0f, R, G, B, 1.0f});
    quads.push_back({(px - wy) + hx, (hy + wx) + py, nz, -1.0f, R, G, B, 1.0f});
    // The light: size 15 at the middle (dat_82000D24), a quarter of the
    // colour.
    const float cx = (p.x + px) * 0.5f, cy = (p.y + py) * 0.5f;
    const float lr = R * 0.25f, lg = G * 0.25f, lb = B * 0.25f;
    lights.push_back({cx - 15.0f, cy - 15.0f, 0.0f, 0.0f, lr, lg, lb});
    lights.push_back({cx + 15.0f, cy - 15.0f, 1.0f, 0.0f, lr, lg, lb});
    lights.push_back({cx + 15.0f, cy + 15.0f, 1.0f, 1.0f, lr, lg, lb});
    lights.push_back({cx - 15.0f, cy + 15.0f, 0.0f, 1.0f, lr, lg, lb});
  }
  live_.resize(kept);
}

}  // namespace geow2
