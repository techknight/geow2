#include "app/frontend.h"

#include <algorithm>
#include <cmath>

#include "audio/events.h"
#include "game/rotation.h"
#include "game/fused.h"

namespace geow2 {
namespace {

constexpr float kT = 0.75f;  // dat_82000718: a button or stick past this counts

// Mode select's items in order (sub_8208ED00): the mode name text ids.
constexpr int kModeItems[6] = {0x1E, 0x21, 0x1C, 0x20, 0x1F, 0x1D};

// sub_8208D390: a mode name text id to its mode.
int mode_of_text(int text) {
  switch (text) {
    case 0x1C: return 1;
    case 0x1D: return 2;
    case 0x1E: return 0;
    case 0x1F: return 3;
    case 0x20: return 4;
    case 0x21: return 5;
    default: return -1;
  }
}

// The selection pulse: fma(fma(cos(phase), 3, 1), 0.5, 1.5), 0.5 .. 3.5.
float pulse_factor(const Game& g, float phase) {
  const float c = float(g.gm->cos(phase));
  return fmadds(fmadds(c, 3.0f, 1.0f), 0.5f, 1.5f);
}

// The heading-style wrap to [-pi, pi]: frac(x / 2pi) (fctid: nearest) times
// 2pi, in double, rounded once.
float wrap_phase(float x) {
  const double u = double(x) * 0.15915494309137826;  // dat_82000D00
  return float((u - std::nearbyint(u)) * 6.2831853072);  // dat_82000CF8
}

}  // namespace

int LocalBoard::add(int64_t score, const std::string& name) {
  if (score <= 0) return -1;
  size_t at = 0;
  while (at < rows.size() && rows[at].score >= score) ++at;
  if (at >= kRows) return -1;
  rows.insert(rows.begin() + std::ptrdiff_t(at), Row{score, name});
  if (rows.size() > kRows) rows.resize(kRows);
  return int(at);
}

Frontend::Frontend(Game& g, const UiText& text, Profile& profile, std::array<LocalBoard, 6>& boards, Hooks hooks)
    : g_(g), text_(text), profile_(profile), boards_(boards), hooks_(std::move(hooks)) {}

void Frontend::sound(audio::Event e) const { g_.world->play(e); }

std::string Frontend::player_name() const {
  if (online_ && online_->paired()) {
    const std::string name = online_->player();
    if (!name.empty()) return name;
  }
  return pad_name(game_pad_);
}

std::string Frontend::pad_name(int pad) const {
  // Gamertags stand in as "%s %i" with text 0x82 and the pad (0x820049D0).
  return format("%s %i", text_(0x82).c_str(), pad + 1);
}

// ---------------------------------------------------------------------------
// The screen record

void Frontend::open(int id, std::function<void(const MenuInput&)> back, std::function<void(UiCanvas&)> draw,
                    bool reset, std::function<void(const MenuInput&)> update, int title) {
  // sub_8208FFF0: every optional part cleared; the selection kept unless
  // reset (or menu 0).
  clock_ = 0.0f;
  pulse_ = 0.0f;
  screen_ = Screen{};
  screen_.id = id;
  screen_.back = std::move(back);
  screen_.draw = std::move(draw);
  screen_.update = std::move(update);
  screen_.title = title;
  if (reset || id == 0) selections_[size_t(id)] = 0;
}

void Frontend::add_item(int text, Action action, Label label, Enabled enabled, bool adjust) {
  if (screen_.items.size() >= 24) return;
  Item it;
  it.text = text;
  it.action = std::move(action);
  it.label = std::move(label);
  it.enabled = std::move(enabled);
  it.adjust = adjust;
  screen_.items.push_back(std::move(it));
}

int& Frontend::selection() { return selections_[size_t(screen_.id)]; }
int Frontend::selection_value() const { return selections_[size_t(screen_.id)]; }

bool Frontend::item_enabled(int i) const {
  const Item& it = screen_.items[size_t(i)];
  return !it.enabled || it.enabled(i, it.text);
}

void Frontend::default_handler(const MenuInput& in) {
  const uint32_t p = in.pressed;
  const int n = int(screen_.items.size());
  // The callbacks are called through copies: one that opens another
  // screen replaces screen_ (and the function in it) while it runs.
  if (n == 0) {
    if (p & (kStart | kA)) {
      sound(audio::Event::kUiConfirm);
      if (const auto f = screen_.accept ? screen_.accept : screen_.back) f(in);
    } else if ((p & kBack) && screen_.back) {
      sound(audio::Event::kUiDelete);
      const auto b = screen_.back;
      b(in);
    }
    return;
  }
  int& s = selection();
  if (s >= n) s = 0;
  if (!item_enabled(s))
    for (int k = 0; k < n && !item_enabled(s); ++k) s = (s + n - 1) % n;
  if (p & kUp) {
    bool moved = true;
    if (n == 1) {
      s = 0;
      moved = false;
    } else if (n == 2) {
      if (s == 0) moved = false;
      s = 0;
    } else {
      s = (s + n - 1) % n;
      for (int k = 0; k < n && !item_enabled(s); ++k) s = (s + n - 1) % n;
    }
    if (moved) {
      reset_pulse();
      sound(audio::Event::kUiChange);
    }
  }
  if (p & kDown) {
    bool moved = true;
    if (n == 1) {
      s = 0;
      moved = false;
    } else if (n == 2) {
      if (s == 1) moved = false;
      s = 1;
    } else {
      s = (s + 1) % n;
      for (int k = 0; k < n && !item_enabled(s); ++k) s = (s + 1) % n;
    }
    if (moved) {
      reset_pulse();
      sound(audio::Event::kUiChange);
    }
  }
  const Item& it = screen_.items[size_t(s)];
  if (p & (kStart | kA)) {
    // Sound 0x17 for an item that goes back, else 0x18 (none when
    // adjustable).
    if (it.action && item_enabled(s)) {
      if (!it.adjust) sound(it.goes_back ? audio::Event::kUiDelete : audio::Event::kUiConfirm);
      const Action a = it.action;
      a(in, s, it.text);
    }
  } else if (p & kBack) {
    if (screen_.back) {
      sound(audio::Event::kUiDelete);
      const auto b = screen_.back;
      b(in);
    }
  } else if (p & (kLeft | kRight)) {
    if (it.adjust && it.action && item_enabled(s)) {
      const Action a = it.action;
      a(in, s, it.text);
    }
  }
}

void Frontend::step_menu(const MenuInput& in) {
  clock_ = kStep + clock_;
  if (screen_.update) {
    const auto u = screen_.update;
    u(in);
  } else {
    default_handler(in);
  }
  // The UI fades in by 0.03 a frame (dat_82001168), not per second.
  const float f = fade_ + 0.03f;
  fade_ = 1.0f - f >= 0.0f ? f : 1.0f;
  pulse_ = wrap_phase(fmadds(kStep, 8.0f, pulse_));  // dat_82000D18
}

// ---------------------------------------------------------------------------
// Input and the flow

void Frontend::frame(const std::array<PadState, 4>& pads) {
  pads_ = pads;
  // Which pads drive the menus: the player's while a game runs, else the
  // menu pad, or all four.
  uint32_t held = 0;
  int holder = -1;
  MenuInput in;
  auto read = [&](int i) {
    const PadState& ps = pads[size_t(i)];
    if (!ps.connected) return;
    const auto& a = ps.axis;
    uint32_t b = 0;
    if (a[0x10] > kT) b |= kStart;
    if (a[0xC] > kT) b |= kA;
    if (a[1] < -kT || a[5] < -kT) b |= kUp;
    if (a[1] > kT || a[5] > kT) b |= kDown;
    if (a[0] < -kT || a[4] < -kT) b |= kLeft;
    if (a[0] > kT || a[4] > kT) b |= kRight;
    if (a[0xD] > kT || a[0x11] > kT) b |= kBack;
    if (a[10] > kT) b |= kLb;
    if (a[11] > kT) b |= kRb;
    if (a[8] > kT) b |= kLt;
    if (a[9] > kT) b |= kRt;
    if (a[0xE] > kT) b |= kX;
    if (a[0xF] > kT) b |= kY;
    if (a[3] < -kT) b |= 0x2000;
    if (a[3] > kT) b |= 0x4000;
    if (a[2] < -kT) b |= 0x8000;
    if (a[2] > kT) b |= 0x10000;
    held |= b;
    if (b & (kStart | kA)) holder = i;
    static const int kAnalog[4] = {1, 3, 8, 9};
    for (int k = 0; k < 4; ++k) {
      const float v = a[size_t(kAnalog[k])];
      if (std::fabs(v) >= 0.25f) in.analog[k] = in.analog[k] + std::clamp(v, -1.0f, 1.0f);  // dat_82000720
    }
  };
  if (g_.flow == 2 || g_.flow == 3) {
    read(game_pad_);
  } else if (menu_pad_ >= 0 && menu_pad_ < 4) {
    read(menu_pad_);
  } else {
    for (int i = 0; i < 4; ++i) read(i);
  }
  in.pad = holder;
  in.held = held;
  in.raw = (prev_held_ ^ held) & held;
  // The axis lock: holding up or down blocks left and right (and the other
  // way round) while held and six frames after.
  if (lock_lr_ < 1 && (held & (kUp | kDown)))
    lock_ud_ = 6;
  else if (lock_ud_ < 1 && (held & (kLeft | kRight)))
    lock_lr_ = 6;
  in.pressed = in.raw;
  if (lock_ud_ > 0) in.pressed &= ~uint32_t(kLeft | kRight);
  if (lock_lr_ > 0) in.pressed &= ~uint32_t(kUp | kDown);
  prev_held_ = held;
  for (int pass = 0; pass < 2; ++pass) {
    const int before = g_.flow;
    switch (g_.flow) {
      case 0:
      case 3: step_menu(in); break;
      case 1: break;  // the start happens in request_game
      case 2: playing(in); break;
      case 4: finish(); break;
      default: break;
    }
    if (g_.flow == before) break;
    // The state changed: once more, with nothing newly pressed.
    in.pressed = in.raw = 0;
  }
  if (lock_ud_ > 0) --lock_ud_;
  if (lock_lr_ > 0) --lock_lr_;
  if (title_drawn_) {
    // fma(frequency * step, 2pi, phase) (dat_8218E7DC).
    logo_phase_ = fmadds(0.1f * kStep, 6.28318548f, logo_phase_);
    star_phase_ = fmadds(0.2f * kStep, 6.28318548f, star_phase_);
    title_drawn_ = false;
  }
}

void Frontend::request_game(int mode) {
  // sub_8208D410: only an unlocked mode; the HUD becomes the screen, then
  // sub_820334F8 (state 1 with that pad) and sub_82033148 at once.
  if (!profile_.mode_unlocked(mode)) return;
  open(0, {}, [this](UiCanvas& c) { draw_game_hud(g_, c.lines); }, false, {}, 0);
  game_pad_ = menu_pad_ >= 0 ? menu_pad_ : 0;
  start_game(g_, mode);  // (and the game music, sub_82078A50(1))
  g_.joined_pad[0] = game_pad_;
}

void Frontend::playing(const MenuInput& in) {
  // sub_820336D8.
  if (0.0f < g_.end_timer) {
    g_.end_timer = g_.end_timer - kStep;
    if (0.0f < g_.end_timer) {
      if (in.pressed & (kStart | kA)) end_game(g_.end_reason);  // skip the wait
    } else if (g_.flow == 2 || g_.flow == 3) {
      if (g_.end_reason == 2) {
        g_.end_timer = 0.0f;
        g_.flow = 4;
      }
    }
    return;
  }
  if (g_.rules.mode == 2 && (g_.sequence.phase == 5 || g_.sequence.phase == 6) && (in.pressed & (kStart | kA))) {
    g_.end_timer = 0.0f;
    g_.flow = 4;
    g_.end_reason = 2;
    return;
  }
  // START, a pad of the game's list (dat_8219CE90) gone, or the system UI
  // showing (sub_8207E228).
  bool lost = false;
  for (int j = 0; j < g_.joined_count && j < 4; ++j) {
    const int pad = g_.joined_pad[size_t(j)];
    if (pad >= 0 && pad < 4 && !pads_[size_t(pad)].connected) lost = true;
  }
  if (in.pressed & kStart) sound(audio::Event::kUiConfirm);
  if ((in.pressed & kStart) || lost || system_ui_) {
    pause(true);
    g_.flow = 3;
  }
}

void Frontend::end_game(int reason) {
  // sub_820335B0.
  if (g_.flow != 2 && g_.flow != 3) return;
  g_.end_timer = 0.0f;
  g_.flow = 4;
  g_.end_reason = reason;
}

void Frontend::finish() {
  // sub_820338D8: every object deleted, the menu music (sub_82078A50(0)).
  delete_objects(g_);
  const int reason = g_.end_reason;
  if (reason == 2 && g_.multiplayer) {
    // Co-op (one record): the co-op table under the joined players' names
    // (", " between them, 0x82000C6C); versus keeps only the match points.
    const int mode = g_.rules.mode;
    if (g_.record_count == 1 && mode >= 0 && mode < 6) {
      std::string name;
      for (int pad = 0; pad < 4; ++pad)
        if (g_.lobby[size_t(pad)].joined) name += (name.empty() ? "" : ", ") + pad_name(pad);
      const ScoreRecord& rec = g_.world->records[0];
      g_.coop_tables[size_t(mode)].insert(rec.score, rec.multiplier, name.substr(0, 0x47));
    }
  } else if (reason == 2) {
    const int mode = g_.rules.mode;
    const std::string name = player_name();
    if (mode >= 0 && mode < 6) {
      const ScoreRecord& rec = g_.world->records[0];
      g_.tables[size_t(mode)].insert(rec.score, rec.multiplier, name);
      // The local board in place of the Live score write (sub_8207E490).
      last_row_ = boards_[size_t(mode)].add(rec.score, name);
      if (last_row_ >= 0 && hooks_.save_boards) hooks_.save_boards();
      if (rec.score > 0 && hooks_.scored) hooks_.scored(mode, rec.score, rec.multiplier, name);
    }
    if (profile_.game_ended(mode, g_.world->records[0].kills, g_.world->records[0].score) && hooks_.save_profile)
      hooks_.save_profile();
    // Millionaire (every mode over 1,000,000) and Unlocked All Modes, then
    // the game's silent achievements are written.
    if (profile_.millions == 0x3F) award_achievement(g_, 0x020, 3);
    if (profile_.mode_unlocked(2)) award_achievement(g_, 0x010, 3);
  }
  if (g_.earned & ~g_.achieved) {
    const uint32_t fresh = g_.earned & ~g_.achieved;
    g_.achieved |= fresh;
    if (g_.unlocked) g_.unlocked(fresh);
  }
  g_.end_reason = 0;
  g_.flow = 0;
  fade_ = 0.0f;
  if (g_.music) g_.music(false);
  switch (reason) {
    case 1: after_game(); break;
    case 2: results(); break;
    default: main_menu(true); break;
  }
}

void Frontend::after_game() {
  // sub_8208EF90: the mode select after a single-player game, the
  // multiplayer setup after a multiplayer one.
  if (g_.multiplayer)
    lobby(false);
  else
    mode_select(true);
}

// ---------------------------------------------------------------------------
// Key tips

void Frontend::tips_menu(TextLayout& l) const {
  l.add(palette_[kUiHighlight], " \x1D\x1E ")->scale = 0.75f;
  l.add(palette_[kUiNormal], text_(0x2C));
  l.add(palette_[kUiHighlight], " A ");
  l.add(palette_[kUiNormal], text_(0x33));
}

void Frontend::tips_back(TextLayout& l) const {
  tips_menu(l);
  l.add(palette_[kUiHighlight], " B ");
  l.add(palette_[kUiNormal], text_(0x2E));
}

void Frontend::tips_adjust(TextLayout& l) const {
  l.add(palette_[kUiHighlight], " \x09\x0A ");
  l.add(palette_[kUiNormal], text_(0x2D));
  l.add(palette_[kUiHighlight], " \x1D\x1E ")->scale = 0.75f;
  l.add(palette_[kUiNormal], text_(0x2C));
  l.add(palette_[kUiHighlight], " B ");
  l.add(palette_[kUiNormal], text_(0x2E));
}

void Frontend::tips_continue(TextLayout& l) const {
  l.add(palette_[kUiHighlight], " A ");
  l.add(palette_[kUiNormal], text_(0x38));
}

void Frontend::adjust_label(TextLayout& l, int name, const std::string& value, bool left, bool right) const {
  if (auto* f = l.add(palette_[kUiNormal], format("%s: ", text_(name).c_str()))) f->align = 3;
  if (left)
    if (auto* f = l.add(palette_[kUiHighlight], "\x09")) f->pad_after = 4.0f;
  l.add(palette_[kUiNormal], value);
  if (auto* f = l.add(palette_[kUiHighlight], right ? "\x0A" : "\x0B")) f->pad_before = 4.0f;
}

// ---------------------------------------------------------------------------
// Drawing

void Frontend::draw_header(UiCanvas& c, int text) const {
  ui_bar(c, palette_, 18.0f, 40.0f);
  g_.font.centre_fit(c.lines, 320.0f, 24.0f, 20.0f, 10.0f, 540.0f, palette_[kUiNormal].data(), text_(text));
}

void Frontend::draw_title_block(UiCanvas& c) const {
  // sub_8208F718: Ui.Title.ShowBox and ShowLogo are off; ShowTex: the logo
  // texture across the top (Ui.Title.X 0, Y 6, Height 100) at alpha
  // fma(PulseStrength 0, cos(phase), Alpha 1); ShowStar: the flare (100 *
  // StarScale 1, centred on (StarX 153, StarY 50 + Title.Y)) at
  // fma(StarPulseStrength 0.2, cos(phase), StarAlpha 1.75). Both bent
  // (RemoveBend off).
  title_drawn_ = true;
  const float a = fmadds(0.0f, float(g_.gm->cos(logo_phase_)), 1.0f);
  c.sprite(0.0f, 6.0f, 640.0f, 100.0f, {a, a, a, a}, kTexLogo, false);
  const float s = 1.0f * 100.0f;  // dat_82001020
  const float sa = fmadds(0.2f, float(g_.gm->cos(star_phase_)), 1.75f);
  c.sprite(153.0f - s * 0.5f, (50.0f + 6.0f) - s * 0.5f, s, s, {sa, sa, sa, sa}, kTexStar, false);
  // Ui.Title.ShowLine: the panel's bottom edge at the menu's top.
  ui_panel(c, 0.0f, 18.0f, 640.0f, cursor_, palette_[kUiBackground], 0xF);
}

void Frontend::flat_icon(UiCanvas& c, float x, float y, float sx, float sy, const ModelGeometry& m,
                         const Rgba& colour) const {
  if (m.parts.empty()) return;
  for (const ModelLine& l : m.parts[0]) {
    const float lc[4] = {l.r * colour[0], l.g * colour[1], l.b * colour[2], l.a * colour[3]};
    const Vec4 p0{fmadds(l.p0.x, sx, 0.0f) + x, fmadds(-l.p0.y, sy, y) + 0.0f, 0.0f, 0.0f};
    const Vec4 p1{fmadds(l.p1.x, sx, 0.0f) + x, fmadds(-l.p1.y, sy, y) + 0.0f, 0.0f, 0.0f};
    emit_line(c.lines, kLineWidthUnit, m.cap_size, p0, p1, lc, 8.0f);  // dat_82000D18
  }
}

void Frontend::rules_icon(UiCanvas& c, float x, float y, float rot, float scale, Vec2 offset, const ModelGeometry& m,
                          Vec4 up, const Rgba& colour) const {
  // M = diag(-scale, scale, scale) * Rz(rot) * basis(eye, (offset, 100),
  // up), the eye at ((x - 320) * 0.9, (y - 180) * 0.9, 0) (dat_82000D70).
  const Vec4 eye{(x - 320.0f) * 0.9f, (y - 180.0f) * 0.9f, 0.0f, 1.0f};
  const Mat4 basis = look_at_basis(eye, {offset.x, offset.y, 100.0f, 0.0f}, up);
  Mat4 r = rotation_axis(*g_.gm, rot, 0.0f, 0.0f, 1.0f);
  const float k[3] = {-scale, scale, scale};
  for (int row = 0; row < 3; ++row)
    for (int col = 0; col < 4; ++col) r.m[row * 4 + col] = r.m[row * 4 + col] * k[row];
  draw_model_part(c.icons, m, 0, mul(r, basis), colour.data());
}

void Frontend::controller_diagram(UiCanvas& c, float x, float y, float size, bool move, bool fire, bool bomb) const {
  const Font& font = g_.font;
  const float hw = size * 0.5f, hh = (size * 0.64f) * 0.5f;
  c.sprite(x - hw, y - hh, size, size * 0.64f, palette_[kUiHighlight], kTexController, false);
  auto at = [&](float ax, float ay) { return Vec2{fmadds(ax, hw, x), fmadds(ay, hh, y)}; };
  // Ui.Pad.* anchors (defaults).
  const Vec2 L = at(-0.508f, 0.23f), R = at(0.247f, 0.6f);
  const bool bumpers = (profile_.controls & 4) != 0;
  const Vec2 LT = bumpers ? at(-0.57f, -0.57f) : at(-0.47f, -0.88f);
  const Vec2 RT = bumpers ? at(0.575f, -0.57f) : at(0.475f, -0.88f);
  const Rgba line_on = scaled(palette_[kUiSelection], 4.0f), &off = palette_[kUiDisabled];
  // Which stick moves and which fires (profile bits 0 and 1).
  std::vector<int> left, right;
  const bool move_right = profile_.controls & 1, fire_right = profile_.controls & 2;
  if (move) (move_right ? right : left).push_back(0x0C);
  if (fire) (fire_right ? right : left).push_back(0x0D);
  {
    const bool none = left.empty();
    const float d = left.size() == 2 ? 11.0f : 0.0f;
    const Rgba& lc = none ? off : line_on;
    emit_ui_line(c.lines, L.x, L.y, L.x, (L.y + 54.0f) - d, lc.data());
    if (none) left.push_back(0x26);
    for (size_t i = 0; i < left.size(); ++i)
      font.centre(c.lines, L.x, (L.y + (i == 0 ? 57.0f : 68.0f)) - d, 8.0f, 8.0f,
                  (none ? off : palette_[kUiNormal]).data(), text_(left[i]));
  }
  {
    const bool none = right.empty();
    const Rgba& lc = none ? off : line_on;
    emit_ui_line(c.lines, R.x, R.y, R.x, R.y + 15.0f, lc.data());
    if (none) right.push_back(0x26);
    for (size_t i = 0; i < right.size(); ++i)
      font.centre(c.lines, R.x, R.y + (i == 0 ? 18.0f : 29.0f), 8.0f, 8.0f, (none ? off : palette_[kUiNormal]).data(),
                  text_(right[i]));
  }
  const float yb = LT.y - 10.0f;
  const float W = RT.x - LT.x;
  const std::string label = text_(bomb ? 0x0E : 0x26);
  const float m = font.measure(label) * 8.0f;
  const float w = std::min(W - 12.0f, std::max(m, 0.0f));
  const float gap = ((W - w) - 8.0f) * 0.5f;
  const Rgba& lc = bomb ? line_on : off;
  emit_ui_line(c.lines, LT.x, LT.y, LT.x, yb, lc.data());
  emit_ui_line(c.lines, LT.x, yb, LT.x + gap, yb, lc.data());
  emit_ui_line(c.lines, RT.x, RT.y, RT.x, yb, lc.data());
  emit_ui_line(c.lines, RT.x, yb, RT.x - gap, yb, lc.data());
  font.centre_fit(c.lines, x, yb - 4.0f, 8.0f, 8.0f, w, (bomb ? palette_[kUiNormal] : off).data(), label);
}

void Frontend::draw(UiCanvas& c) const {
  const Font& font = g_.font;
  c.fade = fade_;
  const float D = fade_;
  const Rgba F{D, D, D, D};
  float cur = 348.0f;  // dat_82006284
  if (screen_.tips) {
    // The key tips (Ui.Menu.ShowTips).
    cur = 328.0f;  // dat_82006280
    TextLayout l;
    screen_.tips(l);
    ui_panel(c, 0.0f, cur, 640.0f, cur + 14.0f, palette_[kUiBackground], 6);
    l.transform({0, 0, 0, 0}, F);
    l.draw(font, c.lines, 320.0f, cur + 4.0f, 6.0f, 6.0f, 576.0f, 2);
  }
  const int n = int(screen_.items.size());
  if (n > 0 && !screen_.item_draw) {
    // The standard list (Ui.Menu.FontSize 12).
    const float fs = 12.0f;
    const float y0 = cur - fmadds(fs + 20.0f, float(n), screen_.descriptions ? 10.0f : 0.0f);
    cur = y0;
    std::vector<TextLayout> labels(static_cast<size_t>(n));
    float W = screen_.width;
    for (int i = 0; i < n; ++i) {
      const Item& it = screen_.items[size_t(i)];
      if (it.label)
        it.label(labels[size_t(i)]);
      else
        labels[size_t(i)].add(palette_[kUiNormal], text_(it.text));
      const float lw = labels[size_t(i)].width(font, fs);
      W = lw - W >= 0.0f ? lw : W;
    }
    W = W - 536.0f >= 0.0f ? 536.0f : W;  // dat_8200627C
    const float xl = fmadds(-(W + 48.0f), 0.5f, 320.0f), xr = fmadds(W + 48.0f, 0.5f, 320.0f);
    float y = y0, y_end = y0;
    for (int i = 0; i < n; ++i) {
      TextLayout& l = labels[size_t(i)];
      const bool disabled = !item_enabled(i);
      const bool selected = i == selection_value();
      if (disabled) {
        l.set_colour(palette_[kUiLockedNormal]);
      } else if (selected) {
        const float k = pulse_factor(g_, pulse_);
        l.transform({0, 0, 0, 0}, {k, k, k, 1.0f});
      }
      const float h = (screen_.descriptions && selected) ? 24.0f : 14.0f;
      const float y1 = (h + fs) + y;
      const Rgba& panel = selected ? palette_[kUiSelection] : disabled ? palette_[kUiLockedBackground] : palette_[kUiBackground];
      ui_panel(c, xl, y, xr, y1, panel, 0);
      if (selected) ui_panel(c, xl + 3.0f, y + 3.0f, xr - 3.0f, y1 - 3.0f, palette_[kUiSelection], 1);
      l.transform({0, 0, 0, 0}, F);
      l.draw(font, c.lines, 320.0f, y + 7.0f, fs, fs, W, 2);
      y_end = y1;
      y = y1 + 6.0f;
    }
    // The sides (Ui.Menu.ShowSides, SideAlpha 1).
    ui_panel(c, 0.0f, y0, xl - 6.0f, y_end, palette_[kUiBackground], 2);
    ui_panel(c, xr + 6.0f, y0, 640.0f, y_end, palette_[kUiBackground], 4);
  }
  if (screen_.title > 0) {
    // The title box above the menu.
    const std::string& t = text_(screen_.title);
    const float M = screen_.busy ? 550.0f : 560.0f;
    float w = 15.0f;
    float H = text_box_height(font, t, 15.0f, 10.0f, 6.0f, M) + 12.0f;
    if (H > 24.0f) {
      w = 10.0f;
      H = text_box_height(font, t, 10.0f, 10.0f, 6.0f, M) + 12.0f;
    }
    cur = cur - (H + 6.0f);
    ui_panel(c, 0.0f, cur, 640.0f, H + cur, palette_[kUiBackground], 6);
    text_box(font, c.lines, palette_, 320.0f, cur + 6.0f, w, 10.0f, 6.0f, M, 2, 0, palette_[kUiNormal], t);
  }
  cur = cur - 6.0f;
  cursor_ = cur;
  if (screen_.item_draw && n > 0)
    for (int i = 0; i < n; ++i)
      screen_.item_draw(c, i, screen_.items[size_t(i)].text, i == selection_value(), !item_enabled(i));
  if (screen_.draw) screen_.draw(c);
  // The player strip: the name and the controller ring with the player's
  // quadrant green.
  if (menu_pad_ >= 0 && menu_pad_ <= 3 && (g_.flow == 0 || g_.flow == 3) && !screen_.hide_strip) {
    const std::string name = display_name(player_name());
    const Rgba white = scaled(Rgba{1, 1, 1, 1}, 1.0f);
    const Rgba nc{F[0] * white[0], F[1] * white[1], F[2] * white[2], F[3] * white[3]};
    font.centre(c.lines, 320.0f, 346.0f, 6.0f, 6.0f, nc.data(), name);
    const float m = font.measure(name);
    const float x = (320.0f - fmadds(m, 6.0f, 2.0f) * 0.5f) - 2.0f;
    for (int k = 0; k < 4; ++k) {
      const Rgba col = k == menu_pad_ ? Rgba{0.15f, 1.5f, 0.15f, 1.0f} : Rgba{0.15f, 0.15f, 0.15f, 1.0f};
      const Rgba fc{col[0] * F[0], col[1] * F[1], col[2] * F[2], col[3] * F[3]};
      font.right(c.lines, x, 346.0f, 6.0f, 6.0f, fc.data(), std::string(1, char(0x11 + k)));
    }
  }
  // After the menu (sub_82093048): Sequence's between-levels screen.
  draw_intermission(c);
}

void Frontend::draw_intermission(UiCanvas& c) const {
  const Sequence& q = g_.sequence;
  if (!(in_game(g_) || ending(g_)) || g_.rules.mode != 2) return;
  if (q.phase == 3 || q.phase == 4) return;
  const bool sel = q.phase != 5 && q.phase != 6;
  const float s = std::clamp(q.intro, 0.0f, 1.0f);
  // The zoom onto the coming level's panel.
  const float ox = float(q.level % 5 - 2) * 75.0f, oy = (float(q.level / 5) - 1.5f) * 55.0f;  // dat_82000F74, dat_82000F78
  const float s2 = s * s;
  const float fade = std::sqrt(1.0f - s);
  const float zx = -ox * s2, zy = -oy * s2;
  float z = 1.0f / std::sqrt(1.0f - s2);
  z = -z >= 0.0f ? 0.0f : z;
  z = z - 10.0f >= 0.0f ? 10.0f : z;  // dat_8200070C
  auto X = [&](float a) { return fmadds(a, z, 320.0f); };
  auto Y = [&](float a) { return fmadds(a, z, 180.0f); };
  static const char* kIcon[5] = {"intermissionUnplayed", "intermissionSuccess", "intermissionDied",
                                 "intermissionOutOfTime", "intermissionSelection"};
  for (int r = 0; r < 4; ++r) {
    const float row_y = (float(r) - 1.5f) * 55.0f;
    for (int col = 0; col < 5; ++col) {
      const int i = r * 5 + col;
      const bool selected = i == q.level && sel;
      const float u = std::clamp(q.panel[size_t(i)].pos, -1.0f, 1.0f);
      const float px = fmadds(u, 400.0f, float(col - 2) * 75.0f);  // dat_82000F88
      const float py = row_y;  // (the vertical springs never move)
      const int outcome = q.outcome[i];
      const int slot = selected ? kUiIntermissionSelection
                                : outcome == 1   ? kUiIntermissionSuccess
                                  : outcome == 2 ? kUiIntermissionDied
                                  : outcome == 3 ? kUiIntermissionOutOfTime
                                                 : kUiIntermissionUnplayed;
      const Rgba& K = palette_[slot];
      const Rgba P = scaled(K, fade);
      ui_panel(c, X((px - 30.0f) + zx), Y((py - 20.0f) + zy), X((zx + px) + 30.0f), Y((zy + py) + 20.0f), P, 0x20);
      if (q.geom_target[size_t(i)] > 0) {
        // The level's geoms collected against its weight, up to 34 high.
        const float frac = std::clamp(float(q.geom_got[size_t(i)]) / float(q.geom_target[size_t(i)]), 0.0f, 1.0f);
        ui_panel(c, X((zx + px) + 24.0f), Y(fmadds(-frac, 34.0f, py + 17.0f) + zy), X((zx + px) + 27.0f),
                 Y((zy + py) + 17.0f), P, 0x21);
      }
      if (selected)
        ui_panel(c, X((px - 33.0f) + zx), Y((py - 23.0f) + zy), X((zx + px) + 33.0f), Y((zy + py) + 23.0f), P, 0x21);
      const char* icon = selected ? kIcon[4] : kIcon[std::clamp(outcome, 0, 3)];
      const auto m = g_.charmap.find(icon);
      if (m != g_.charmap.end()) {
        const Rgba I = scaled(K, std::sqrt(fade));
        flat_icon(c, (zx + px) * z + 320.0f, (zy + py) * z + 180.0f, z * 1.25f, z * 1.25f, m->second, I);  // dat_82000768
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Boot and the main menu

void Frontend::boot() {
  // sub_820334A8: the intro screen; no bloom until it is over.
  g_.flow = 0;
  g_.bloom_level = 0.0f;
  open(0, {}, [this](UiCanvas& c) { draw_intro(c); }, false, [this](const MenuInput& in) { intro(in); }, 0);
  screen_.hide_strip = true;
}

void Frontend::intro(const MenuInput& in) {
  // sub_8207E9F0 (Ui.Intro.Pause off; SpringFactor 0.8, SpringDamp 0.6).
  intro_time_ = kStep + intro_time_;
  const float a = fmadds(kStep, 4.0f, intro_fade_);  // dat_820006F4
  intro_fade_ = std::clamp(a, 0.0f, 1.0f);
  if (intro_phase_ == 0) {
    intro_scale_ = 1.0f;
    intro_speed_ = 0.0f;
  } else {
    intro_scale_ = intro_speed_ + intro_scale_;
    intro_speed_ = fmadds(1.0f - intro_scale_, 0.8f, intro_speed_) * 0.6f;
  }
  // A or START skips to the fade-out.
  if ((in.pressed & (kStart | kA)) && intro_time_ < 4.25f) intro_time_ = 4.25f;  // dat_820011F0
  if (intro_time_ > 5.0f) {  // dat_82004A28
    if (intro_phase_ == 0) {
      intro_scale_ = intro_time_ = intro_fade_ = intro_speed_ = 0.0f;
      intro_phase_ = 1;
      g_.bloom_level = 1.0f;
      sound(audio::Event::kBong);
    } else {
      g_.bloom_level = 1.0f;
      if (g_.music) g_.music(false);
      main_menu(false);
    }
  }
  fade_ = 0.0f;
}

void Frontend::draw_intro(UiCanvas& c) const {
  // sub_8207E628: fully shown until 4.25 s, gone at 4.75 s.
  const float f = std::clamp(((5.0f - intro_time_) - 0.25f) * 2.0f, 0.0f, 1.0f) * intro_fade_;
  if (intro_phase_ == 0) {
    // retroatvi.tga over the whole screen, flat; Ui.Intro.Alpha 0.55,
    // times 1.5 (dat_82000C90).
    const float k = (0.55f * f) * 1.5f;
    c.sprite(0.0f, 0.0f, 640.0f, 360.0f, {k, k, k, k}, kTexRetroAtvi, true);
  } else {
    // retrobizarre.tga springing to size (Ui.Intro.Size 260, Y 160), then
    // "Bizarre" / "Creations" under it (TextY -38, TextRatio 0.663; literal,
    // not localised) in the normal colour.
    const float size = 260.0f, S = size * intro_scale_;
    const Rgba& n = palette_[kUiNormal];
    const Rgba col{n[0] * f * 0.55f, n[1] * f * 0.55f, n[2] * f * 0.55f, n[3] * f * 0.55f};
    c.sprite(fmadds(-S, 0.5f, 320.0f), 160.0f - size * 0.5f, S, size, col, kTexRetroBizarre, false);
    const Rgba nf{n[0] * f, n[1] * f, n[2] * f, n[3] * f};
    const float y = ((size * 0.5f) + -38.0f) + 160.0f;
    g_.font.set_kerning(false);
    g_.font.centre_fit(c.lines, 320.0f, y, 40.0f, 10.0f, 0.663f * S, nf.data(), "Bizarre");
    g_.font.centre_fit(c.lines, 320.0f, y + 17.0f, 40.0f, 10.0f, 0.663f * S, nf.data(), "Creations");
    g_.font.set_kerning(true);
  }
  // (The ESRB line shows only on North American consoles or with
  // Ui.Intro.ShowEsrb; neither applies here.)
}

void Frontend::main_menu(bool reset) {
  open(1, {}, [this](UiCanvas& c) { draw_title_block(c); }, reset, {}, 0);
  screen_.tips = [this](TextLayout& l) { tips_menu(l); };
  add_item(0x01, [this](const MenuInput& in, int, int) {
    if (in.pad == -1) return;
    menu_pad_ = in.pad;
    mode_select(true);
  });
  // sub_8208F028: the pad that chose it becomes the active one and joins.
  add_item(0x02, [this](const MenuInput& in, int, int) {
    if (in.pad == -1) return;
    menu_pad_ = in.pad;
    lobby_open(in.pad);
    lobby(true);
  });
  add_item(0x04, [this](const MenuInput& in, int, int) {
    if (in.pad == -1) return;
    menu_pad_ = in.pad;
    leaderboards(true);
  });
  add_item(0x10, [this](const MenuInput& in, int, int) {
    if (in.pad == -1) return;
    achievements_screen();
  });
  add_item(0x05, [this](const MenuInput& in, int, int) {
    if (in.pad == -1) return;
    help(true);
    menu_pad_ = in.pad;
  });
  // The original's 0x07 "Return to arcade" names the console's dashboard;
  // the port quits, so the item is labelled with the game's own key tip for
  // it, 0x37 "Quit" (DIFFERENCES.md "Leaving the game").
  add_item(0x37, [this](const MenuInput&, int, int) { quit_confirm(); });
  menu_pad_ = -1;  // sub_82033698: every pad drives the main menu
}

void Frontend::quit_confirm() {
  open(0, [this](const MenuInput&) { main_menu(false); }, {}, false, {}, 0x73);
  screen_.tips = [this](TextLayout& l) { tips_menu(l); };
  add_item(0x17, [this](const MenuInput&, int, int) { main_menu(false); });
  screen_.items.back().goes_back = true;
  add_item(0x16, [this](const MenuInput&, int, int) {
    // XamLoaderLaunchTitle back to the dashboard: the program quits.
    if (hooks_.quit) hooks_.quit();
  });
}

// ---------------------------------------------------------------------------
// Mode select

void Frontend::mode_select(bool reset) {
  open(3, [this](const MenuInput&) { main_menu(false); },
       [this](UiCanvas& c) {
         draw_header(c, 0x41);
         ui_panel(c, 0.0f, 46.0f, 22.0f, cursor_, palette_[kUiBackground], 2);
         ui_panel(c, 618.0f, 46.0f, 640.0f, cursor_, palette_[kUiBackground], 4);
       },
       reset, [this](const MenuInput& in) { mode_select_update(in); }, 0);
  screen_.tips = [this](TextLayout& l) {
    const int sel = selection_value();
    const int mode = mode_of_text(kModeItems[sel < 6 ? sel : 0]);
    const bool open_mode = profile_.mode_unlocked(mode);
    l.add(open_mode ? palette_[kUiHighlight] : palette_[kUiDisabled], "X ");
    l.add(open_mode ? palette_[kUiNormal] : palette_[kUiDisabled], text_(0x2F));
    // The leaderboard key: local boards stand in for Live (always there).
    l.add(palette_[kUiHighlight], " Y ");
    l.add(palette_[kUiNormal], text_(0x30));
    l.add(palette_[kUiHighlight], " \x1D\x1E\x09\x0A ");
    l.add(palette_[kUiNormal], text_(0x2C));
    l.add(palette_[kUiHighlight], " A ");
    l.add(palette_[kUiNormal], text_(0x33));
    l.add(palette_[kUiHighlight], " B ");
    l.add(palette_[kUiNormal], text_(0x2E));
  };
  screen_.item_draw = [this](UiCanvas& c, int index, int text, bool selected, bool) { mode_tile(c, index, text, selected); };
  for (int t : kModeItems)
    add_item(
        t,
        [this](const MenuInput&, int, int text) {
          const int mode = mode_of_text(text);
          const int x = profile_index(mode);
          if (profile_.index_new(x))
            rules(x, [this](const MenuInput&) { mode_select(false); }, text,
                  [this, mode](const MenuInput&, int, int) { request_game(mode); });
          else
            request_game(mode);
        },
        {}, [this](int, int text) { return profile_.mode_unlocked(mode_of_text(text)); });
  // Preselection: the remembered item when unlocked, else the first; then
  // the first new mode (sound 0x6F when not the first item).
  int& s = selection();
  if (s >= 6 || !item_enabled(s)) s = 0;
  for (int i = 0; i < 6; ++i)
    if (profile_.index_new(profile_index(mode_of_text(kModeItems[i])))) {
      s = i;
      if (i != 0) sound(audio::Event::kUnlockedNewMode);
      break;
    }
}

void Frontend::mode_select_update(const MenuInput& in) {
  tile_phase_ = wrap_phase(fmadds(kStep, 5.0f, tile_phase_));  // dat_820006F0
  auto navigate = [&](uint32_t bits) {
    int& s = selection();
    int to = s;
    if ((bits & kUp) && s != 0 && s != 3) to = s - 1;
    if ((bits & kDown) && s != 2 && s != 5) to = s + 1;
    if ((bits & kLeft) && s > 2) to = s - 3;
    if ((bits & kRight) && s < 3) to = s + 3;
    // A locked tile cannot be selected (sub_820915C0).
    if (to != s && item_enabled(to)) {
      s = to;
      reset_pulse();
      sound(audio::Event::kUiChange);
      return true;
    }
    return false;
  };
  if (!navigate(in.pressed)) navigate((in.pressed ^ in.raw) & in.raw);
  if (in.pressed & kX) {
    sound(audio::Event::kUiConfirm);
    // The rules screen, without a start item.
    const int x = profile_index(mode_of_text(kModeItems[selection_value()]));
    rules(x, [this](const MenuInput&) { mode_select(false); }, 0, {});
    return;
  }
  if (in.pressed & kY) {
    sound(audio::Event::kUiConfirm);
    board_view(mode_of_text(kModeItems[selection_value()]), [this](const MenuInput&) { mode_select(false); });
    return;
  }
  MenuInput rest = in;
  rest.pressed &= ~uint32_t(0x3C);
  default_handler(rest);
}

void Frontend::mode_tile(UiCanvas& c, int index, int text, bool selected) const {
  const Font& font = g_.font;
  const float H = ((cursor_ - 46.0f) + 6.0f) * 0.333333343f;
  const float h = H - 6.0f;
  const float x0 = index / 3 == 0 ? 28.0f : 323.0f;
  const float y0 = fmadds(float(index % 3), H, 46.0f);
  const float p = selected ? pulse_factor(g_, pulse_) : 1.0f;
  const int mode = mode_of_text(text);
  const int x = profile_index(mode);
  const bool unlocked = profile_.mode_unlocked(mode);
  const bool fresh = profile_.index_new(x) && text != 0x1E;
  const int pre_x = prerequisite(x);
  const bool pre = pre_x >= 0 && profile_.mode_unlocked(mode_of_index(pre_x));
  const Rgba& base = unlocked ? palette_[kUiNormal] : pre ? palette_[kUiLockedNormalPending] : palette_[kUiLockedNormal];
  const Rgba col = scaled_rgb(base, p);
  if (unlocked && selected)
    ui_panel(c, x0 + 3.0f, y0 + 3.0f, (x0 + 289.0f) - 3.0f, (y0 + h) - 3.0f, palette_[kUiSelection], 1);
  const Rgba& tile = selected ? palette_[kUiSelection]
                     : unlocked ? palette_[kUiBackground]
                     : pre      ? palette_[kUiLockedBackgroundPending]
                                : palette_[kUiLockedBackground];
  ui_panel(c, x0, y0, x0 + 289.0f, y0 + h, tile, 0);
  // The icon: the item's CharMap model, then the name under it.
  const float yi = fmadds(h - 20.0f, 0.5f, y0) + 10.0f;
  static const char* kIcon[6] = {"deadline", "king", "evolved", "pacifism", "waves", "progression"};
  const auto icon = g_.charmap.find(kIcon[index]);
  if (icon != g_.charmap.end()) flat_icon(c, x0 + 44.0f, yi - 2.0f, 1.1f, 1.1f, icon->second, col);
  font.centre_fit(c.lines, x0 + 44.0f, yi + 10.0f, 6.0f, 6.0f, 75.0f, col.data(), text_(text));
  const float L = x0 + 88.0f, R = x0 + 280.0f, C = (R + L) * 0.5f;
  if (fresh) {
    text_box(font, c.lines, palette_, C, fmadds(h - 7.0f, 0.5f, y0), 7.0f, 7.0f, 3.0f, (R - L) - 18.0f, 2, 1, col,
             text_(0x88));
  } else if (unlocked) {
    // The local case: the session's best, and in place of the Live mini
    // table the local board's best.
    font.left(c.lines, L + 15.0f, y0 + h * 0.25f, 7.0f, 7.0f, palette_[kUiNormal].data(),
              format("%s:", text_(0x7B).c_str()));
    int64_t best = g_.tables[size_t(mode)].entry[1].score;
    const LocalBoard& b = boards_[size_t(mode)];
    if (!b.rows.empty() && b.rows[0].score > best) best = b.rows[0].score;
    font.right(c.lines, R - 15.0f, y0 + h * 0.25f, 7.0f, 7.0f, palette_[kUiNormal].data(), format_number(best));
  } else if (pre) {
    text_box(font, c.lines, palette_, C, y0 + 27.0f, 7.0f, 7.0f, 3.0f, (R - L) - 6.0f, 2, 2, col, text_(0x87));
    const float q = fmadds(float(g_.gm->cos(tile_phase_)) + 1.0f, 1.5f, 1.0f);
    const Rgba cq = scaled(col, q);
    int pct = int((float(profile_.progress[size_t(pre_x)]) * 100.0f) / float(kKillsToUnlock));
    pct = std::clamp(pct, 0, 100);
    font.centre_fit(c.lines, C, y0 + 37.0f, 7.0f, 7.0f, (R - L) - 6.0f, cq.data(),
                    format("%s: %i%%", text_(0x8A).c_str(), pct));
    static const int kHint[6] = {0x90, 0x8E, 0x91, 0x92, 0x8F, 0x93};
    text_box(font, c.lines, palette_, C, y0 + 52.0f, 7.0f, 7.0f, 3.0f, 192.0f, 2, 0, col, text_(kHint[x]));
  } else {
    text_box(font, c.lines, palette_, C, fmadds(h, 0.5f, y0), 7.0f, 7.0f, 3.0f, (R - L) - 18.0f, 2, 1, col,
             text_(0x87));
  }
}

// ---------------------------------------------------------------------------
// The rules screens

void Frontend::rules(int index, std::function<void(const MenuInput&)> back, int item_text, Action start) {
  static const int kTitle[6] = {0x1C, 0x1E, 0x20, 0x1F, 0x21, 0x1D};  // by profile index
  // The lines per mode: text id, the icon (a unit's model, a CharMap
  // model, or glyphs drawn as text), its rotation, scale, aim offset and
  // colour (sub_8207ABB0, sub_8207A8B8).
  struct Line {
    int text;
    const char* unit = nullptr;     // a Unit's model
    const char* charmap = nullptr;  // a CharMap model (up (0, 1, 0))
    const char* glyphs = nullptr;
    float rot = 0.0f, scale = 1.0f;
    Vec2 offset{0.0f, 0.0f};
    Rgba colour{1, 1, 1, 1};
  };
  struct Rules {
    std::vector<Line> lines;
    std::vector<int> tips;
    bool move, fire, bomb;
  };
  constexpr float kPi = 3.14159274f;  // dat_820048D0
  static const Rules kRules[6] = {
      {{{0x45, "player", nullptr, nullptr, -1.91986215f},  // dat_82004908
        {0x46, "spinner", nullptr, nullptr, 0.610865235f, 1.0f, {5.0f, -10.0f}},  // dat_82004904
        {0x47, nullptr, nullptr, "\x0C\x02"}},
       {0x48, 0x49}, true, true, true},  // Evolved
      {{{0x4A, "player", nullptr, nullptr, 2.26892805f},  // dat_82004920
        {0x4B, "wanderer", nullptr, nullptr, 0.383972436f, 1.0f, {0.0f, -20.0f}},  // dat_82004918
        {0x4C, "geom"}},
       {0x4D, 0x4E}, true, true, true},  // Deadline
      {{{0x4F, "player", nullptr, nullptr, 1.91986215f},  // dat_82004930
        {0x50, "gatezoom", nullptr, nullptr, 0.0f, 1.0f, {0.0f, 1.0f}},
        {0x51, "gate", nullptr, nullptr, 0.0f, 1.0f, {-20.0f, 20.0f}}},
       {0x52, 0x53}, true, false, false},  // Pacifism
      {{{0x54, "rocket", nullptr, nullptr, kPi}}, {0x55, 0x56, 0x57}, true, true, false},  // Waves
      {{{0x58, "hill", nullptr, nullptr, 0.0f, 10.0f, {0.0f, 1.0f}, {0.1f, 0.1f, 1.0f, 1.0f}},
        {0x59, "hill", nullptr, nullptr, 0.0f, 10.0f, {0.0f, 1.0f}, {0.1f, 1.0f, 0.1f, 1.0f}},
        {0x5A, "hill", nullptr, nullptr, 0.0f, 10.0f, {0.0f, 1.0f}, {1.0f, 0.1f, 0.1f, 1.0f}}},
       {0x5B, 0x5C}, true, true, false},  // King
      {{{0x5D, nullptr, "intermissionSuccess", nullptr, kPi, 1.0f, {0.0f, 0.0f}, {0, 0, 0, 0}},
        {0x5E, nullptr, "intermissionOutOfTime", nullptr, kPi, 1.0f, {0.0f, 20.0f}, {0, 0, 0, 0}},
        {0x5F, nullptr, "intermissionDied", nullptr, kPi, 1.0f, {0.0f, 0.0f}, {0, 0, 0, 0}}},
       {0x60, 0x61}, true, true, true},  // Sequence (palette slots 19, 21, 20)
  };
  const int x = std::clamp(index, 0, 5);
  open(0, back,
       [this, x](UiCanvas& c) {
         const Font& font = g_.font;
         draw_header(c, kTitle[x]);
         ui_panel(c, 0.0f, 46.0f, 22.0f, cursor_, palette_[kUiBackground], 2);
         ui_panel(c, 618.0f, 46.0f, 640.0f, cursor_, palette_[kUiBackground], 4);
         ui_panel(c, 28.0f, 46.0f, 612.0f, cursor_, palette_[kUiBackground], 0);
         font.left(c.lines, 52.0f, 58.0f, 8.0f, 8.0f, palette_[kUiHighlight].data(), text_(0x62));
         // (English: the Evolved spacing is the others'.)
         float y = 82.0f;
         auto line = [&](const Line* l, int text) {
           const float t = text_box(font, c.lines, palette_, 92.0f, y, 8.0f, 8.0f, 4.0f, 288.0f, 0, 0,
                                    palette_[kUiNormal], text_(text));
           const float yi = y + std::min(t * 0.5f, 10.0f);
           if (!l) {
             font.centre(c.lines, 72.0f, yi, 2.0f, 2.0f, palette_[kUiHighlight].data(), "O");
           } else if (l->glyphs) {
             font.centre(c.lines, 72.0f, yi, 6.0f, 6.0f, palette_[kUiHighlight].data(), l->glyphs);
           } else if (l->unit) {
             const UnitInfo* u = g_.units->find(l->unit);
             if (u && u->has_model) rules_icon(c, 72.0f, yi, l->rot, l->scale, l->offset, u->model, u->up, l->colour);
           } else if (l->charmap) {
             const auto m = g_.charmap.find(l->charmap);
             static const int kSlot[3] = {kUiIntermissionSuccess, kUiIntermissionOutOfTime, kUiIntermissionDied};
             const int k = l->text - 0x5D;
             if (m != g_.charmap.end())
               rules_icon(c, 72.0f, yi, l->rot, l->scale, l->offset, m->second, {0.0f, 1.0f, 0.0f, 0.0f},
                          palette_[kSlot[k >= 0 && k < 3 ? k : 0]]);
           }
           y = y + (t + 14.0f);
         };
         for (const Line& l : kRules[x].lines) line(&l, l.text);
         y = y + 8.0f;
         font.left(c.lines, 52.0f, y, 8.0f, 8.0f, palette_[kUiHighlight].data(), text_(0x63));
         y = y + 24.0f;
         for (int t : kRules[x].tips) line(nullptr, t);
         controller_diagram(c, 500.0f, 180.0f, 200.0f, kRules[x].move, kRules[x].fire, kRules[x].bomb);
       },
       false,
       [this](const MenuInput& in) {
         // sub_8207A658: without items, A or X go back.
         if (screen_.items.empty() && (in.pressed & (kA | kX))) {
           sound(audio::Event::kUiDelete);
           if (const auto b = screen_.back) b(in);
           return;
         }
         default_handler(in);
       },
       0);
  const bool has_item = bool(start);
  screen_.tips = [this, has_item](TextLayout& l) {
    if (has_item) {
      l.add(palette_[kUiHighlight], "A ");
      l.add(palette_[kUiNormal], text_(0x34));
      l.add(palette_[kUiHighlight], " B ");
    } else {
      l.add(palette_[kUiHighlight], "B ");
    }
    l.add(palette_[kUiNormal], text_(0x2E));
  };
  screen_.item_draw = [](UiCanvas&, int, int, bool, bool) {};  // sub_8207A780: nothing
  if (start) add_item(item_text, std::move(start));
}

void Frontend::how_to_play(bool reset) {
  const bool in = in_game(g_);
  open(9, [this](const MenuInput&) { help(false); },
       [this, in](UiCanvas& c) {
         if (in)
           draw_game_hud(g_, c.lines);
         else
           draw_title_block(c);
       },
       reset, {}, 0x42);
  screen_.width = 260.0f;
  screen_.tips = [this](TextLayout& l) { tips_back(l); };
  for (int t : kModeItems)
    add_item(t, [this](const MenuInput&, int, int text) {
      rules(profile_index(mode_of_text(text)), [this](const MenuInput&) { how_to_play(false); }, 0, {});
    });
  if (in && reset) {
    static const int kItem[6] = {0, 2, 5, 4, 3, 1};
    selection() = kItem[std::clamp(g_.rules.mode, 0, 5)];
  }
}

// ---------------------------------------------------------------------------
// Pause

void Frontend::hud_screen() {
  open(0, {}, [this](UiCanvas& c) { draw_game_hud(g_, c.lines); }, false, {}, 0);
}

void Frontend::pause(bool reset) {
  auto resume = [this](const MenuInput&) {
    // sub_82033570: back to playing with the HUD as the screen.
    if (g_.flow == 3) g_.flow = 2;
    hud_screen();
  };
  open(2, resume, [this](UiCanvas& c) { draw_game_hud(g_, c.lines); }, reset, {}, 0x44);
  screen_.tips = [this](TextLayout& l) { tips_menu(l); };
  add_item(0x08, [resume](const MenuInput& in, int, int) { resume(in); });
  screen_.items.back().goes_back = true;
  add_item(0x05, [this](const MenuInput&, int, int) { help(true); });
  add_item(0x04, [this](const MenuInput&, int, int) { leaderboards(true); });
  add_item(0x10, [this](const MenuInput&, int, int) { achievements_screen(); });
  add_item(0x09, [this](const MenuInput&, int, int) { exit_confirm(); });
}

void Frontend::exit_confirm() {
  open(0, [this](const MenuInput&) { pause(false); }, [this](UiCanvas& c) { draw_game_hud(g_, c.lines); }, false,
       {}, 0x72);
  screen_.tips = [this](TextLayout& l) { tips_menu(l); };
  add_item(0x17, [this](const MenuInput&, int, int) { pause(false); });
  screen_.items.back().goes_back = true;
  add_item(0x16, [this](const MenuInput&, int, int) { end_game(1); });
}

// ---------------------------------------------------------------------------
// Help and options

void Frontend::help(bool reset) {
  const bool in = in_game(g_);
  auto back = [this, in](const MenuInput&) {
    // sub_820867E8: a changed profile is saved on the way out.
    if (dirty_ && hooks_.save_profile) hooks_.save_profile();
    dirty_ = false;
    if (in)
      pause(false);
    else
      main_menu(false);
  };
  open(4, back,
       [this, in](UiCanvas& c) {
         if (in)
           draw_game_hud(g_, c.lines);
         else
           draw_title_block(c);
       },
       reset, {}, 0x05);
  if (reset) dirty_ = false;
  screen_.width = 260.0f;
  screen_.tips = [this](TextLayout& l) { tips_back(l); };
  add_item(0x0F, [this](const MenuInput&, int, int) { how_to_play(true); });
  add_item(0x11, [this](const MenuInput&, int, int) { controls(); });
  add_item(0x12, [this](const MenuInput&, int, int) { settings(); });
  if (!in) add_item(0x13, [this](const MenuInput&, int, int) { credits(); });
  // The port's online scores (no original: DIFFERENCES.md), out of a game.
  if (!in && online_)
    add_item(0, [this](const MenuInput&, int, int) { online_screen(true); },
             [this](TextLayout& l) { l.add(palette_[kUiNormal], "Online scores"); });
}

void Frontend::controls() {
  open(0, [this](const MenuInput&) { help(false); },
       [this](UiCanvas& c) {
         draw_header(c, 0x11);
         const float y1 = cursor_ - 6.0f;
         ui_panel(c, 0.0f, 46.0f, 22.0f, y1, palette_[kUiBackground], 2);
         ui_panel(c, 618.0f, 46.0f, 640.0f, y1, palette_[kUiBackground], 4);
         ui_panel(c, 28.0f, 46.0f, 612.0f, y1, palette_[kUiBackground], 0);
         const Rgba q = scaled(palette_[kUiBackground], 0.25f);
         for (int i = 0; i < 40; ++i)
           emit_ui_body(c.lines, float(i) * 0.025f * 640.0f, cursor_, float(i + 1) * 0.025f * 640.0f, cursor_, q.data(),
                        q.data());
         controller_diagram(c, 320.0f, fmadds(y1 + 46.0f, 0.5f, -5.0f), 200.0f, true, true, true);
       },
       false, {}, 0);
  screen_.width = 300.0f;
  screen_.tips = [this](TextLayout& l) { tips_adjust(l); };
  auto bit_item = [this](int text, uint32_t bit, int set_text, int clear_text) {
    add_item(
        text,
        [this, bit](const MenuInput& in, int, int) {
          const bool set = (profile_.controls & bit) != 0;
          if ((in.pressed & kLeft) && set) {
            profile_.controls &= ~bit;
            reset_pulse();
            sound(audio::Event::kUiConfirm);
            dirty_ = true;
          } else if ((in.pressed & kRight) && !set) {
            profile_.controls |= bit;
            reset_pulse();
            sound(audio::Event::kUiConfirm);
            dirty_ = true;
          }
        },
        [this, text, bit, set_text, clear_text](TextLayout& l) {
          const bool set = (profile_.controls & bit) != 0;
          adjust_label(l, text, text_(set ? set_text : clear_text), set, !set);
        },
        {}, true);
  };
  bit_item(0x0C, 1, 0x23, 0x22);
  bit_item(0x0D, 2, 0x23, 0x22);
  bit_item(0x0E, 4, 0x25, 0x24);
}

void Frontend::settings() {
  const bool in = in_game(g_);
  open(0, [this](const MenuInput&) { help(false); },
       [this, in](UiCanvas& c) {
         if (in)
           draw_game_hud(g_, c.lines);
         else
           draw_title_block(c);
       },
       false, {}, 0x12);
  screen_.width = 460.0f;
  screen_.tips = [this](TextLayout& l) { tips_adjust(l); };
  auto volume_item = [this](int text, float Profile::*v) {
    add_item(
        text,
        [this, v](const MenuInput& in, int, int) {
          float& x = profile_.*v;
          if (in.pressed & kLeft) {
            if (0.0f < x) {
              reset_pulse();
              sound(audio::Event::kUiConfirm);
            }
            const float y = x - 0.05f;  // dat_82000D60
            x = y >= 0.0f ? (y - 1.0f >= 0.0f ? 1.0f : y) : 0.0f;
            dirty_ = true;
          } else if (in.pressed & kRight) {
            if (x < 1.0f) {
              reset_pulse();
              sound(audio::Event::kUiConfirm);
            }
            const float y = x + 0.05f;
            x = y >= 0.0f ? (y - 1.0f >= 0.0f ? 1.0f : y) : 0.0f;
            dirty_ = true;
          }
        },
        [this, text, v](TextLayout& l) {
          const int n = int(fmadds(profile_.*v, 100.0f, 0.005f));
          adjust_label(l, text, format("%i%%", n), n > 0, n < 100);
        },
        {}, true);
  };
  volume_item(0x0B, &Profile::sound_volume);
  volume_item(0x0A, &Profile::music_volume);
  add_item(
      0x1A,
      [this](const MenuInput& in, int, int) {
        int& d = profile_.score_display;
        if ((in.pressed & kLeft) && d != 0) {
          --d;
          reset_pulse();
          sound(audio::Event::kUiConfirm);
          dirty_ = true;
        }
        if ((in.pressed & kRight) && uint32_t(d + 1) < 2) {
          ++d;
          reset_pulse();
          sound(audio::Event::kUiConfirm);
          dirty_ = true;
        }
      },
      [this](TextLayout& l) {
        const int d = profile_.score_display;
        if (d == 0) adjust_label(l, 0x1A, text_(0x28), false, true);
        if (d == 1) adjust_label(l, 0x1A, text_(0x27), true, false);
      },
      {}, true);
}

void Frontend::credits() {
  // The layout (sub_8207A0B0): space before and after each entry by type.
  static const float kBefore[5] = {80.0f, 39.0f, 14.0f, 0.0f, 80.0f};
  static const float kAfter[5] = {120.0f, 58.0f, 28.0f, 14.0f, 120.0f};
  std::vector<float> ys;
  float c = 392.0f;  // dat_820048C0
  for (size_t i = 0; i < g_.credits.size(); ++i) {
    const int t = std::clamp(g_.credits[i].first, 0, 4);
    float y = kBefore[t] + c;
    c = kAfter[t] + c;
    if (i == 0) {
      c = c - kBefore[t];
      y = y - kBefore[t];
    }
    ys.push_back(y);
  }
  credits_length_ = c;
  credits_scroll_ = 0.0f;
  open(0, [this](const MenuInput&) { help(false); },
       [this, ys](UiCanvas& cv) {
         static const int kSlot[5] = {kUiBright, kUiBright, kUiHighlight, kUiNormal, kUiNormal};
         static const float kSize[5] = {20.0f, 10.0f, 10.0f, 10.0f, 40.0f};
         for (size_t i = 0; i < g_.credits.size(); ++i) {
           const float Y = ys[i] - credits_scroll_;
           if (Y < -360.0f || Y > 720.0f) continue;
           const int t = std::clamp(g_.credits[i].first, 0, 4);
           const Rgba& col = palette_[kSlot[t]];
           g_.font.centre(cv.lines, 320.0f, Y, kSize[t], kSize[t], col.data(), g_.credits[i].second);
         }
       },
       false,
       [this](const MenuInput& in) {
         // Scrolled by the sticks and triggers; it loops.
         auto sq = [](float x) { return x >= 0.0f ? x * x : -(x * x); };
         float v = sq(in.analog[0]) + sq(in.analog[1]);
         v = v - sq(in.analog[2]);
         v = v + sq(in.analog[3]);
         v = std::clamp(v, -1.0f, 1.0f);
         credits_scroll_ = fmadds(fmadds(v, 400.0f, 40.0f), kStep, credits_scroll_);
         if (credits_length_ > 0.0f) {
           credits_scroll_ = float(std::fmod(double(credits_length_ + credits_scroll_), double(credits_length_)));
           credits_scroll_ = float(std::fmod(double(credits_length_ + credits_scroll_), double(credits_length_)));
         }
         default_handler(in);
       },
       0);
  screen_.hide_strip = true;
}

// ---------------------------------------------------------------------------
// Results

void Frontend::results() {
  open(0, [this](const MenuInput&) { after_game(); },
       [this](UiCanvas& c) {
         const Font& font = g_.font;
         const float B = cursor_;
         const UiPalette& P = palette_;
         ui_panel(c, 0.0f, 18.0f, 640.0f, 40.0f, P[kUiBackground], 0);
         font.centre_fit(c.lines, 320.0f, 24.0f, 20.0f, 10.0f, 540.0f, P[kUiNormal].data(), text_(0x64));
         const int mode = std::clamp(g_.rules.mode, 0, 5);
         const ScoreTable& T = g_.tables[size_t(mode)];
         auto heading = [&](float y, const std::string& t) {
           // Headings (sub_820877D8) in palette slot 10.
           ui_panel(c, 0.0f, y, 160.0f, y + 14.0f, P[10], 0);
           ui_panel(c, 166.0f, y, 474.0f, y + 14.0f, P[10], 0);
           ui_panel(c, 480.0f, y, 640.0f, y + 14.0f, P[10], 0);
           font.centre_fit(c.lines, 320.0f, (y + 7.0f) - 4.0f, 8.0f, 8.0f, 300.0f, P[kUiHighlight].data(), t);
           return 20.0f;
         };
         auto two = [&](float y, const std::string& a, const std::string& b) {
           const float xs[5][2] = {{0, 25}, {31, 311}, {317, 323}, {329, 609}, {615, 640}};
           for (const auto& x : xs) ui_panel(c, x[0], y, x[1], y + 14.0f, P[10], 0);
           font.centre_fit(c.lines, 171.0f, (y + 7.0f) - 4.0f, 8.0f, 8.0f, 276.0f, P[kUiHighlight].data(), a);
           font.centre_fit(c.lines, 469.0f, (y + 7.0f) - 4.0f, 8.0f, 8.0f, 276.0f, P[kUiHighlight].data(), b);
           return 20.0f;
         };
         auto box = [&](float y, float h, int64_t v, float size, float drop) {
           ui_panel(c, 0.0f, y, 22.0f, y + h, P[kUiBackground], 0);
           ui_panel(c, 28.0f, y, 612.0f, y + h, P[kUiBackground], 0);
           ui_panel(c, 618.0f, y, 640.0f, y + h, P[kUiBackground], 0);
           font.centre(c.lines, 320.0f, fmadds(h, 0.5f, y) - drop, size, size, P[kUiNormal].data(), format_number(v));
           return h + 6.0f;
         };
         if (g_.multiplayer) {
           if (g_.record_count <= 1)
             results_coop(c);
           else
             results_versus(c);
           return;
         }
         float y = 46.0f;
         y = y + heading(y, text_(0x65));
         y = y + box(y, 50.0f, T.entry[0].score, 20.0f, 10.0f);
         // "Friends Leaderboard": the local board here.
         y = y + two(y, text_(0x66), text_(0x67));
         const float list_h = B - 204.0f;
         ui_panel(c, 0.0f, 142.0f, 22.0f, 142.0f + list_h, P[kUiBackground], 0);
         ui_panel(c, 28.0f, 142.0f, 314.0f, 142.0f + list_h, P[kUiBackground], 0);
         ui_panel(c, 326.0f, 142.0f, 612.0f, 142.0f + list_h, P[kUiBackground], 0);
         ui_panel(c, 618.0f, 142.0f, 640.0f, 142.0f + list_h, P[kUiBackground], 0);
         emit_ui_line(c.lines, 320.0f, 142.0f, 320.0f, 142.0f + list_h, P[10].data());
         const float L = B - 68.0f;
         for (int k = 0; k < 8; ++k) {
           const float ry = fmadds(float(k), 14.0f, 148.0f);
           const ScoreEntry& e = T.entry[size_t(2 + k)];
           const bool last = k == 0 && T.entry[0].score > 0 && e.score == T.entry[0].score &&
                             e.multiplier == T.entry[0].multiplier && e.name == T.entry[0].name;
           const Rgba& col = last ? P[kUiHighlight] : P[kUiNormal];
           if (e.name.empty()) {
             font.left(c.lines, 34.0f, ry, 8.0f, 8.0f, P[kUiDisabled].data(), "-");
             font.right(c.lines, 308.0f, ry, 8.0f, 8.0f, P[kUiDisabled].data(), "-");
           } else {
             const std::string num = format_number(e.score);
             font.left_fit(c.lines, 34.0f, ry, 8.0f, 8.0f, fmadds(-font.measure(num), 8.0f, 258.0f), col.data(), display_name(e.name));
             font.right(c.lines, 308.0f, ry, 8.0f, 8.0f, col.data(), num);
           }
         }
         // The friends board (sub_82088AE0): with a score server the group's
         // board, eight rows from clamp(own - 4, 0, count - 8), the player's
         // row highlighted, names and scores only; without one the local
         // board's rows around the last game's.
         int own = -1;
         const std::vector<GroupRow> group = group_board(mode, own);
         const bool friends = online_ && online_->paired();
         const LocalBoard& b = boards_[size_t(mode)];
         const int count = friends ? int(group.size()) : int(b.rows.size());
         const int mark = friends ? own : last_row_;
         const int first = std::clamp(mark - 4, 0, std::max(count - 8, 0));
         const float pitch = ((L - 148.0f) + 6.0f) * 0.125f;
         const float off = ((pitch - 6.0f) - 8.0f) * 0.5f;
         float ry = 148.0f;
         for (int j = 0; j < 8; ++j) {
           const int r = first + j;
           const float yy = off + ry;
           ry = ry + pitch;
           if (r >= count) continue;
           const Rgba& col = r == mark ? P[kUiHighlight] : P[kUiNormal];
           const int64_t score = friends ? group[size_t(r)].score : b.rows[size_t(r)].score;
           const std::string num = format_number(score);
           const std::string label = friends ? display_name(group[size_t(r)].name)
                                             : format("%i. %s", r + 1, display_name(b.rows[size_t(r)].name).c_str());
           font.left_fit(c.lines, 332.0f, yy, 8.0f, 8.0f, fmadds(-font.measure(num), 8.0f, 252.0f), col.data(), label);
           font.right(c.lines, 600.0f, yy, 8.0f, 8.0f, col.data(), num);
         }
         // The board's scroll bar (sub_820913B0 at 606, eight rows).
         ui_scroll_bar(c, 606.0f, 148.0f, L, float(first), float(count), 8.0f, scaled(P[kUiBright], 0.5f),
                       scaled(P[kUiNormal], 0.25f), scaled(P[kUiDisabled], 0.5f));
         heading(B - 56.0f, text_(0x68));
         box(B - 36.0f, 36.0f, T.entry[1].score, 12.0f, 6.0f);
       },
       false, {}, 0);
  screen_.accept = [this](const MenuInput&) { after_game(); };
  screen_.tips = [this](TextLayout& l) { tips_continue(l); };
}

// ---------------------------------------------------------------------------
// The multiplayer setup

namespace {

// The lobby's items: 0..11 the mode tiles (row = index / 6, column =
// index mod 6; columns 0..2 Versus, 3..5 Cooperative), 12..15 the team
// presets, 16 and 17 copilots off / on.
constexpr int kLobbyItems[18] = {0x1E, 0x21, 0x1C, 0x1E, 0x21, 0x1C, 0x20, 0x1F, 0x1D,
                                 0x20, 0x1F, 0x1D, 0x18, 0x18, 0x18, 0x18, 0x19, 0x19};
// The team presets (0x8218E780): each pad's team.
constexpr int kPresets[4][4] = {{0, 1, 2, 3}, {0, 0, 1, 1}, {0, 1, 0, 1}, {0, 1, 1, 0}};

// sub_82082DD0: a pad's ship glyph alone (0x0C..0x0F), as pilot (0x16,
// 0x18, 0x1A, 0x1C) or as copilot (0x15, 0x17, 0x19, 0x1B).
std::string ship_glyph(int pad, int role) {
  const int c = role == 1 ? 0x16 + 2 * pad : role == 2 ? 0x15 + 2 * pad : 0x0C + pad;
  return std::string(1, char(c));
}

// The group of a lobby item: 0 Versus tiles, 1 Cooperative tiles, 2 team
// presets, 3 copilots.
int lobby_group(int i) { return i < 12 ? (i % 6 < 3 ? 0 : 1) : i < 16 ? 2 : 3; }

}  // namespace

int Frontend::joined() const {
  int n = 0;
  for (const LobbyPad& l : g_.lobby) n += l.joined ? 1 : 0;
  return n;
}

int Frontend::partner(int pad) const {
  // With copilots on: the one other joined pad of the same team (-2 when
  // more than one, -1 when none).
  if (!copilots_ || pad < 0 || pad > 3) return -1;
  int r = -1;
  for (int o = 0; o < 4; ++o) {
    const LobbyPad& l = g_.lobby[size_t(o)];
    if (!l.joined || o == pad || l.team != g_.lobby[size_t(pad)].team) continue;
    r = r == -1 ? o : -2;
  }
  return r;
}

int Frontend::one_team_preset() const {
  // The first preset (1..3) that puts every joined pad in one team.
  for (int p = 1; p < 4; ++p) {
    std::array<bool, 4> seen{};
    int teams = 0;
    for (int o = 0; o < 4; ++o)
      if (g_.lobby[size_t(o)].joined && !seen[size_t(kPresets[p][o])]) {
        seen[size_t(kPresets[p][o])] = true;
        ++teams;
      }
    if (teams == 1) return p;
  }
  return 0;
}

int Frontend::team_count() const {
  std::array<bool, 4> seen{};
  int teams = 0;
  for (const LobbyPad& l : g_.lobby)
    if (l.joined && !seen[size_t(l.team & 3)]) {
      seen[size_t(l.team & 3)] = true;
      ++teams;
    }
  return teams;
}

void Frontend::lobby_open(int pad) {
  // sub_82083AE0: every pad out, in its own team, nothing scored; copilots
  // off, preset 0, no swaps; the pad that chose Multiplayer joins.
  for (int p = 0; p < 4; ++p) g_.lobby[size_t(p)] = LobbyPad{false, p, 0, 0, 0, 0};
  copilots_ = false;
  preset_ = 0;
  swap_ = {};
  g_.lobby[size_t(std::clamp(pad, 0, 3))].joined = true;
}

void Frontend::lobby(bool reset) {
  open(6, [this](const MenuInput&) { lobby_back(); }, [this](UiCanvas& c) { lobby_frame(c); }, reset,
       [this](const MenuInput& in) { lobby_update(in); }, 0);
  screen_.tips = [this](TextLayout& l) {
    // sub_820833C0.
    const int n = joined();
    l.add(copilots_ ? palette_[kUiHighlight] : palette_[kUiDisabled], " Y ");
    l.add(copilots_ ? palette_[kUiNormal] : palette_[kUiDisabled], text_(0x39));
    l.add(n >= 2 ? palette_[kUiHighlight] : palette_[kUiDisabled], " \x1D\x1E ");
    l.add(n >= 2 ? palette_[kUiNormal] : palette_[kUiDisabled], text_(0x2C));
    l.add(n >= 2 ? palette_[kUiHighlight] : palette_[kUiDisabled], " A ");
    l.add(n >= 2 ? palette_[kUiNormal] : palette_[kUiDisabled], text_(0x33));
    l.add(palette_[kUiHighlight], " B ");
    l.add(palette_[kUiNormal], text_(0x2E));
  };
  screen_.item_draw = [this](UiCanvas& c, int index, int text, bool selected, bool disabled) {
    lobby_tile(c, index, text, selected, disabled);
  };
  for (int i = 0; i < 12; ++i)
    add_item(
        kLobbyItems[i],
        [this](const MenuInput& in, int index, int text) {
          // sub_820835E0: versus for columns 0..2; the game starts with
          // the multiplayer setup as its mode setup, the pad that chose it
          // driving the menus.
          const int mode = mode_of_text(text);
          if (!profile_.mode_unlocked(mode)) return;
          const bool versus = index % 6 < 3;
          lobby_roles();
          if (joined() < 2) return;
          open(0, {}, [this](UiCanvas& c) { draw_game_hud(g_, c.lines); }, false, {}, 0);
          game_pad_ = in.pad >= 0 ? in.pad : (menu_pad_ >= 0 ? menu_pad_ : 0);
          start_multiplayer(g_, mode, versus);
        },
        {},
        [this](int index, int text) {
          // sub_820838A8: two or more joined and the mode unlocked; with
          // copilots, no Pacifism and Versus only from three.
          const int n = joined();
          if (n < 2 || !profile_.mode_unlocked(mode_of_text(text))) return false;
          if (copilots_) {
            if (text == 0x20) return false;
            if (n < 3 && index % 6 < 3) return false;
          }
          return true;
        });
  for (int i = 12; i < 16; ++i)
    add_item(
        kLobbyItems[i],
        [this](const MenuInput&, int index, int) {
          // sub_82085BB8: preset 0 also turns copilots off.
          const int p = std::clamp(index - 12, 0, 3);
          if (p == preset_) return;
          if (p == 0) copilots_ = false;
          preset_ = p;
          reset_pulse();
        },
        {}, [this](int, int) { return joined() > 2; });
  for (int i = 16; i < 18; ++i)
    add_item(
        kLobbyItems[i],
        [this](const MenuInput&, int index, int) {
          // sub_82083780: with two joined, copilots put both in one team
          // (or stay off when no preset does); with three or more, preset
          // 0 becomes 1.
          const bool on = index != 16;
          if (on == copilots_) return;
          const int n = joined();
          copilots_ = on;
          if (n == 2) {
            preset_ = one_team_preset();
            if (preset_ < 1) copilots_ = false;
            for (int p = 0; p < 4; ++p) g_.lobby[size_t(p)].team = kPresets[preset_][p];
          } else if (n > 2 && preset_ == 0) {
            preset_ = 1;
          }
          reset_pulse();
        },
        {}, [this](int, int) { return joined() > 1; });
}

void Frontend::lobby_roles() {
  // sub_82082F10: the preset and copilots kept consistent with the joined
  // count, the teams from the preset, and with copilots the first two
  // joined of teams 0 and 1 as pilot and copilot (swapped by Y).
  const int n = joined();
  if (n < 2) {
    preset_ = 0;
    copilots_ = false;
  } else if (n < 3) {
    if (preset_ >= 1 && team_count() == 1) {
      if (!copilots_) preset_ = 0;
    } else {
      preset_ = 0;
      copilots_ = false;
    }
  } else if (preset_ == 0 && copilots_) {
    preset_ = 1;
  }
  std::array<int, 2> first{-1, -1}, second{-1, -1};
  std::array<bool, 2> pair{true, true};
  for (int p = 0; p < 4; ++p) {
    LobbyPad& l = g_.lobby[size_t(p)];
    l.team = kPresets[preset_][p];
    l.role = 0;
    if (!l.joined || l.team > 1) continue;
    const size_t t = size_t(l.team);
    if (first[t] == -1)
      first[t] = p;
    else if (second[t] == -1)
      second[t] = p;
    else
      pair[t] = false;
  }
  if (!copilots_) return;
  for (size_t t = 0; t < 2; ++t) {
    if (!pair[t] || first[t] == -1 || second[t] == -1) continue;
    g_.lobby[size_t(first[t])].role = swap_[t] ? 2 : 1;
    g_.lobby[size_t(second[t])].role = swap_[t] ? 1 : 2;
  }
}

void Frontend::lobby_update(const MenuInput& in) {
  // sub_820846E0: the join prompt's pulse turns at pi a second.
  {
    const double d = double(fmadds(kStep, 3.14159274f, lobby_phase_)) * 0.15915494309189535;
    lobby_phase_ = float((d - std::nearbyint(d)) * 6.2831853072);
  }
  // Each pad's bits (above 0.75): START, A, B or BACK, X, Y, not
  // connected; the pressed edges.
  for (int p = 0; p < 4; ++p) {
    const PadState& ps = pads_[size_t(p)];
    uint32_t b = 0;
    if (ps.axis[0x10] > 0.75f) b |= kStart;
    if (ps.axis[0xC] > 0.75f) b |= kA;
    if (ps.axis[0x11] > 0.75f || ps.axis[0xD] > 0.75f) b |= kBack;
    if (ps.axis[0xE] > 0.75f) b |= kX;
    if (ps.axis[0xF] > 0.75f) b |= kY;
    if (!ps.connected) b |= 0x20000;
    lobby_pressed_[size_t(p)] = (b ^ lobby_held_[size_t(p)]) & b;
    lobby_held_[size_t(p)] = b;
  }
  // Every pad but the active one joins with START or A and leaves with B,
  // BACK or when disconnected; Y swaps the copilot positions of a joined
  // pad's team.
  for (int p = 0; p < 4; ++p) {
    LobbyPad& l = g_.lobby[size_t(p)];
    if (p != menu_pad_) {
      if (lobby_pressed_[size_t(p)] & (kStart | kA))
        l.joined = true;
      else if ((lobby_pressed_[size_t(p)] & kBack) || (lobby_held_[size_t(p)] & 0x20000))
        l.joined = false;
    }
    if (l.joined && (lobby_pressed_[size_t(p)] & kY) && partner(p) > -1) {
      if (l.team == 0) swap_[0] = !swap_[0];
      else if (l.team == 1) swap_[1] = !swap_[1];
    }
  }
  lobby_roles();
  if (!lobby_navigate(in.pressed)) lobby_navigate((in.pressed ^ in.raw) & in.raw);
  MenuInput rest = in;
  rest.pressed &= ~uint32_t(0x3C);
  default_handler(rest);
  lobby_roles();
}

int Frontend::lobby_search(int col, int row, int dir) const {
  // sub_82083518: the nearest enabled item from `col` in direction `dir`
  // within the row's six; else `col`.
  for (int c = col + dir; c >= 0 && c < 6; c += dir)
    if (item_enabled(row * 6 + c)) return c;
  return col;
}

bool Frontend::lobby_navigate(uint32_t bits) {
  // sub_82084588.
  const int sel = selection_value();
  int col = std::clamp(sel % 6, 0, 5), row = std::clamp(sel / 6, 0, 2);
  if ((bits & kUp) && row > 0) {
    // sub_82084338: up a row (from the bottom row 0, 1, 1, 2, 3, 5), the
    // nearest enabled to the left when the target is not.
    static constexpr int kUpMap[6] = {0, 1, 1, 2, 3, 5};
    for (;;) {
      if (row == 1) {
        row = 0;
        if (item_enabled(col)) break;
      } else if (row == 2) {
        col = kUpMap[col];
        row = 1;
        if (item_enabled(6 + col)) break;
      } else {
        break;
      }
      col = lobby_search(col, row, -1);
      if (item_enabled(row * 6 + col)) break;
    }
  }
  if ((bits & kDown) && row < 2) {
    // sub_82084438: down a row (to the bottom row 0, 1, 3, 4, 4, 5), the
    // nearest enabled to the left, then (bottom row) to the right.
    static constexpr int kDownMap[6] = {0, 1, 3, 4, 4, 5};
    for (;;) {
      if (row == 0) {
        row = 1;
        if (item_enabled(6 + col)) break;
        col = lobby_search(col, 1, -1);
        if (item_enabled(6 + col)) break;
      } else if (row == 1) {
        col = kDownMap[col];
        row = 2;
        if (item_enabled(12 + col)) break;
        col = lobby_search(col, 2, -1);
        if (item_enabled(12 + col)) break;
        col = lobby_search(col, 2, 1);
        break;
      } else {
        break;
      }
    }
  }
  if (bits & kLeft) col = lobby_search(col, row, -1);
  if (bits & kRight) col = lobby_search(col, row, 1);
  const int to = row * 6 + col;
  if (to == sel || !item_enabled(to)) return false;
  selection() = to;
  reset_pulse();
  sound(audio::Event::kUiChange);
  return true;
}

void Frontend::lobby_back() {
  // sub_820832A0: straight to the main menu unless points were scored or
  // two or more joined; then a confirm (title 0x71: No / Yes).
  bool scored = false;
  for (const LobbyPad& l : g_.lobby)
    if (l.joined && l.total >= 1) scored = true;
  if (!scored && joined() < 2) {
    main_menu(false);
    return;
  }
  open(0, [this](const MenuInput&) { lobby(false); }, {}, false, {}, 0x71);
  screen_.tips = [this](TextLayout& l) { tips_menu(l); };
  add_item(0x17, [this](const MenuInput&, int, int) { lobby(false); });
  screen_.items.back().goes_back = true;
  add_item(0x16, [this](const MenuInput&, int, int) { main_menu(false); });
}

void Frontend::lobby_players(UiCanvas& c) const {
  // sub_82083C28: the title bar, then each pad's box (top row 46, bottom
  // 88; even pads left, odd right) in its team colour, or the locked
  // background with "Press A to join" (0x85; 0x86 without a pad) pulsing.
  const Font& font = g_.font;
  ui_bar(c, palette_, 18.0f, 40.0f);
  font.centre_fit(c.lines, 320.0f, 24.0f, 20.0f, 10.0f, 540.0f, palette_[kUiNormal].data(), text_(0x43));
  for (int p = 0; p < 4; ++p) {
    const LobbyPad& l = g_.lobby[size_t(p)];
    const float y = p < 2 ? 46.0f : 88.0f;
    const Rgba base = l.joined ? palette_[kUiMultiplayerRed + std::clamp(l.team, 0, 3)] : palette_[kUiLockedBackground];
    const bool odd = (p & 1) != 0;
    ui_panel(c, odd ? 323.0f : 0.0f, y, odd ? 640.0f : 317.0f, y + 36.0f, base, odd ? 4 : 2);
    const float ty = y + 6.0f;
    const bool connected = pads_[size_t(p)].connected;
    const std::string name = pad_name(p);
    if (!l.joined) {
      const Rgba col = scaled(base, 3.0f);
      const float k = (float(g_.gm->cos(lobby_phase_)) + 1.0f) * 4.0f + 1.0f;
      const Rgba pulse = scaled(col, k);
      const std::string& prompt = text_(connected ? 0x85 : 0x86);
      if (!odd) {
        font.left_fit(c.lines, 64.0f, ty, 10.0f, 10.0f, 220.0f, col.data(), name);
        font.left_fit(c.lines, 64.0f, ty + 14.0f, 10.0f, 10.0f, 220.0f, pulse.data(), prompt);
      } else {
        font.right_fit(c.lines, 576.0f, ty, 10.0f, 10.0f, 220.0f, col.data(), name);
        font.right_fit(c.lines, 576.0f, ty + 14.0f, 10.0f, 10.0f, 220.0f, pulse.data(), prompt);
      }
    } else {
      const Rgba col = scaled_rgb(base, 3.0f);
      int n = partner(p);
      if (p <= n) n = p;
      const std::string glyph = n >= 0 && (l.role == 1 || l.role == 2) ? ship_glyph(n, l.role) : ship_glyph(p, 0);
      const std::string score = format("%s: %i", text_(0x7A).c_str(), l.total);
      if (!odd) {
        font.left_fit(c.lines, 64.0f, ty, 10.0f, 10.0f, 220.0f, col.data(), name);
        font.left_fit(c.lines, 64.0f, ty + 14.0f, 10.0f, 10.0f, 220.0f, col.data(), score);
        font.right(c.lines, 308.0f, ty + 2.0f, 20.0f, 20.0f, col.data(), glyph);
      } else {
        font.right_fit(c.lines, 576.0f, ty, 10.0f, 10.0f, 220.0f, col.data(), name);
        font.right_fit(c.lines, 576.0f, ty + 14.0f, 10.0f, 10.0f, 220.0f, col.data(), score);
        font.left(c.lines, 332.0f, ty + 2.0f, 20.0f, 20.0f, col.data(), glyph);
      }
    }
  }
}

void Frontend::lobby_frame(UiCanvas& c) const {
  // sub_82084990.
  lobby_players(c);
  const Font& font = g_.font;
  const int n = joined();
  const bool two = n >= 2;
  const int sel = selection_value();
  const int group = lobby_group(sel);
  const Rgba& bg = palette_[kUiBackground];
  const Rgba& bar = palette_[10];
  auto bar_tint = [&](bool on) { return scaled(bar, on ? 2.0f : 0.5f); };
  ui_panel(c, 0.0f, 150.0f, 22.0f, 266.0f, bg, 2);
  ui_panel(c, 618.0f, 150.0f, 640.0f, 266.0f, bg, 4);
  ui_panel(c, 0.0f, 130.0f, 311.0f, 144.0f, bar_tint(two && group == 0), 2);
  ui_panel(c, 317.0f, 130.0f, 323.0f, 144.0f, bar, 0);
  ui_panel(c, 329.0f, 130.0f, 640.0f, 144.0f, bar_tint(two && group == 1), 4);
  const bool versus = two && !(copilots_ && n < 3);
  font.centre(c.lines, 170.0f, 133.0f, 8.0f, 8.0f, (versus ? palette_[kUiHighlight] : palette_[11]).data(), text_(0x70));
  font.centre(c.lines, 470.0f, 133.0f, 8.0f, 8.0f, (two ? palette_[kUiHighlight] : palette_[11]).data(), text_(0x6F));
  emit_ui_body(c.lines, 320.0f, 150.0f, 320.0f, 266.0f, bar.data(), bar.data());
  ui_panel(c, 0.0f, 292.0f, 22.0f, cursor_, bg, 2);
  ui_panel(c, 618.0f, 292.0f, 640.0f, cursor_, bg, 4);
  ui_panel(c, 0.0f, 272.0f, 311.0f, 286.0f, bar_tint(two && group == 2), 2);
  ui_panel(c, 317.0f, 272.0f, 323.0f, 286.0f, bar, 0);
  ui_panel(c, 329.0f, 272.0f, 640.0f, 286.0f, bar_tint(two && group == 3), 4);
  font.centre(c.lines, 170.0f, 275.0f, 8.0f, 8.0f, (n < 3 ? palette_[11] : palette_[kUiHighlight]).data(), text_(0x18));
  font.centre(c.lines, 470.0f, 275.0f, 8.0f, 8.0f, (n < 2 ? palette_[11] : palette_[kUiHighlight]).data(), text_(0x19));
  emit_ui_body(c.lines, 320.0f, 292.0f, 320.0f, cursor_, bar.data(), bar.data());
}

void Frontend::lobby_teams(TextLayout& l, int preset, bool copilots, bool joined_only) const {
  // sub_82084FC0: the pads' ship glyphs in the preset's order (0x8218E798
  // rows {0, 1, 2, 3}, {0, 1, 2, 3}, {0, 2, 1, 3}, {0, 3, 1, 2}) in team
  // colours times 2: four teams for preset 0; else two pairs with a gap,
  // as pilot and copilot glyphs when copilots are on and both are shown.
  static constexpr int kOrder[4][4] = {{0, 1, 2, 3}, {0, 1, 2, 3}, {0, 2, 1, 3}, {0, 3, 1, 2}};
  const int* o = kOrder[std::clamp(preset, 0, 3)];
  auto shown = [&](int p) { return !joined_only || g_.lobby[size_t(p)].joined; };
  auto colour = [&](int team) { return scaled_rgb(palette_[kUiMultiplayerRed + team], 2.0f); };
  auto field = [&](int team, const std::string& glyph, bool pad) {
    TextLayout::Field* f = l.add(colour(team), glyph);
    if (f && pad) f->pad_after = 2.0f;
  };
  if (preset == 0) {
    for (int k = 0; k < 4; ++k)
      if (shown(o[k])) field(k, ship_glyph(o[k], 0), k < 3);
    return;
  }
  const bool a = shown(o[0]) || shown(o[1]), b = shown(o[2]) || shown(o[3]);
  if (!copilots) {
    if (shown(o[0])) field(0, ship_glyph(o[0], 0), true);
    if (shown(o[1])) field(0, ship_glyph(o[1], 0), true);
    if (a && b)
      if (TextLayout::Field* f = l.add_empty()) f->pad_after = 6.0f;
    if (shown(o[2])) field(1, ship_glyph(o[2], 0), true);
    if (shown(o[3])) field(1, ship_glyph(o[3], 0), false);
    return;
  }
  const bool pair0 = !joined_only || (g_.lobby[size_t(o[0])].joined && g_.lobby[size_t(o[1])].joined);
  const bool pair1 = !joined_only || (g_.lobby[size_t(o[2])].joined && g_.lobby[size_t(o[3])].joined);
  if (shown(o[0])) field(0, ship_glyph(o[0], pair0 ? 1 : 0), true);
  if (shown(o[1])) field(0, ship_glyph(pair0 ? o[0] : o[1], pair0 ? 2 : 0), true);
  if (a && b)
    if (TextLayout::Field* f = l.add_empty()) f->pad_after = 6.0f;
  if (shown(o[2])) field(1, ship_glyph(o[2], pair1 ? 1 : 0), true);
  if (shown(o[3])) field(1, ship_glyph(pair1 ? o[2] : o[3], pair1 ? 2 : 0), false);
}

void Frontend::lobby_tile(UiCanvas& c, int index, int text, bool selected, bool disabled) const {
  // sub_82085C28.
  const Font& font = g_.font;
  const int row = index / 6, col = index % 6;
  float x = fmadds(float(col), 97.3333359f, col < 3 ? 0.0f : 6.0f) + 28.0f;
  float y = fmadds(float(row), 61.0f, 150.0f);
  float w = 91.3333359f, h = 55.0f;
  bool current = false;
  if (index >= 16) {
    x = fmadds(float(col - 4), 146.0f, 326.0f);
    y = 292.0f;
    w = 140.0f;
    h = 30.0f;
    current = index == (copilots_ ? 17 : 16);
  } else if (index >= 12) {
    x = fmadds(float(col), 73.0f, 28.0f);
    y = 292.0f;
    w = 67.0f;
    h = 30.0f;
    current = index == preset_ + 12;
  }
  const float p = selected ? pulse_factor(g_, pulse_) : 1.0f;
  const Rgba col_text = disabled ? palette_[kUiLockedNormal] : scaled_rgb(palette_[kUiNormal], p);
  if (!disabled) {
    if (current || selected)
      ui_panel(c, x + 3.0f, y + 3.0f, (x + w) - 3.0f, (y + h) - 3.0f,
               current ? palette_[kUiHighlight] : palette_[kUiSelection], 1);
    const float k = lobby_group(selection_value()) == lobby_group(index) ? 1.0f : 0.25f;  // dat_82000720
    ui_panel(c, x, y, x + w, y + h, selected ? palette_[kUiSelection] : scaled(palette_[kUiBackground], k), 0);
  } else {
    ui_panel(c, x, y, x + w, y + h, palette_[kUiLockedBackground], 0);
  }
  const float cx = fmadds(w, 0.5f, x), cy = fmadds(h, 0.5f, y);
  if (index < 12) {
    static const std::map<int, const char*> kIcon = {{0x1E, "deadline"}, {0x21, "king"},     {0x1C, "evolved"},
                                                     {0x20, "pacifism"}, {0x1F, "waves"},    {0x1D, "progression"}};
    const auto name = kIcon.find(text);
    if (name != kIcon.end()) {
      const auto icon = g_.charmap.find(name->second);
      if (icon != g_.charmap.end()) flat_icon(c, cx, cy - 5.0f, 1.1f, 1.1f, icon->second, col_text);
    }
    font.centre_fit(c.lines, cx, cy + 8.0f, 6.0f, 6.0f, 75.0f, col_text.data(), text_(text));
    return;
  }
  TextLayout l;
  const int n = joined();
  if (index < 16) {
    lobby_teams(l, index - 12, false, n > 2);
  } else {
    const bool on = index == 17;
    int shown = 0;
    if (on) {
      shown = one_team_preset();
      if (shown < 1) shown = 1;
    }
    if (shown <= preset_) shown = preset_;
    if (!on && n < 3) shown = 0;
    lobby_teams(l, shown, on, n > 1);
  }
  if (disabled) l.set_colour(palette_[kUiLockedNormal]);
  if (selected) l.transform({1.0f, 1.0f, 1.0f, 1.0f}, {p, p, p, p});
  l.draw(font, c.lines, cx, cy - 4.5f, 9.0f, 9.0f, -1.0f, 2);
}

void Frontend::results_coop(UiCanvas& c) const {
  // sub_82089218: the co-op table (sub_82034C78): the final score, the
  // previous co-op games and the best this session.
  const Font& font = g_.font;
  const float B = cursor_;
  const UiPalette& P = palette_;
  const int mode = std::clamp(g_.rules.mode, 0, 5);
  const ScoreTable& T = g_.coop_tables[size_t(mode)];
  auto heading = [&](float y, const std::string& t) {
    ui_panel(c, 0.0f, y, 160.0f, y + 14.0f, P[10], 0);
    ui_panel(c, 166.0f, y, 474.0f, y + 14.0f, P[10], 0);
    ui_panel(c, 480.0f, y, 640.0f, y + 14.0f, P[10], 0);
    font.centre_fit(c.lines, 320.0f, (y + 7.0f) - 4.0f, 8.0f, 8.0f, 300.0f, P[kUiHighlight].data(), t);
  };
  auto box = [&](float y, float h, int64_t v, float size, float drop) {
    ui_panel(c, 0.0f, y, 22.0f, y + h, P[kUiBackground], 0);
    ui_panel(c, 28.0f, y, 612.0f, y + h, P[kUiBackground], 0);
    ui_panel(c, 618.0f, y, 640.0f, y + h, P[kUiBackground], 0);
    if (size > 0.0f)
      font.centre(c.lines, 320.0f, fmadds(h, 0.5f, y) - drop, size, size, P[kUiNormal].data(), format_number(v));
  };
  heading(46.0f, text_(0x65));
  box(66.0f, 50.0f, T.entry[0].score, 20.0f, 10.0f);
  heading(122.0f, text_(0x66));
  box(142.0f, B - 204.0f, 0, 0.0f, 0.0f);
  for (int k = 0; k < 8; ++k) {
    const float ry = fmadds(float(k), 14.0f, 148.0f);
    const ScoreEntry& e = T.entry[size_t(2 + k)];
    const bool last = k == 0 && T.entry[0].score > 0 && e.score == T.entry[0].score &&
                      e.multiplier == T.entry[0].multiplier && e.name == T.entry[0].name;
    const Rgba& col = last ? P[kUiHighlight] : P[kUiNormal];
    if (e.name.empty()) {
      font.left(c.lines, 34.0f, ry, 8.0f, 8.0f, P[kUiDisabled].data(), "-");
      font.right(c.lines, 606.0f, ry, 8.0f, 8.0f, P[kUiDisabled].data(), "-");
    } else {
      const std::string num = format_number(e.score);
      const float max_w = std::max(572.0f - fmadds(font.measure(num), 8.0f, 20.0f), 1.0f);
      font.left_fit(c.lines, 34.0f, ry, 8.0f, 8.0f, max_w, col.data(), display_name(e.name));
      font.right(c.lines, 606.0f, ry, 8.0f, 8.0f, col.data(), num);
    }
  }
  heading(B - 56.0f, text_(0x68));
  box(B - 36.0f, 36.0f, T.entry[1].score, 12.0f, 6.0f);
}

void Frontend::results_versus(UiCanvas& c) const {
  // sub_82088978: this game's rankings (sub_82087F38) over the overall
  // ones (sub_820884D0), each in a box half the height left.
  const Font& font = g_.font;
  const float B = cursor_;
  const UiPalette& P = palette_;
  const float h = ((B - 46.0f) - 46.0f) * 0.5f;
  auto heading = [&](float y, const std::string& t) {
    ui_panel(c, 0.0f, y, 160.0f, y + 14.0f, P[10], 0);
    ui_panel(c, 166.0f, y, 474.0f, y + 14.0f, P[10], 0);
    ui_panel(c, 480.0f, y, 640.0f, y + 14.0f, P[10], 0);
    font.centre_fit(c.lines, 320.0f, (y + 7.0f) - 4.0f, 8.0f, 8.0f, 300.0f, P[kUiHighlight].data(), t);
  };
  auto box = [&](float y, float hh) {
    ui_panel(c, 0.0f, y, 22.0f, y + hh, P[kUiBackground], 0);
    ui_panel(c, 28.0f, y, 612.0f, y + hh, P[kUiBackground], 0);
    ui_panel(c, 618.0f, y, 640.0f, y + hh, P[kUiBackground], 0);
  };
  heading(46.0f, text_(0x69));
  box(66.0f, h);
  heading(72.0f + h, text_(0x6A));
  box(92.0f + h, h);
  const float* hi = P[kUiHighlight].data();
  auto columns = [&](float y, int third, int fourth) {
    font.centre(c.lines, 45.0f, y, 10.0f, 10.0f, hi, "#");
    font.left_fit(c.lines, 65.0f, y, 10.0f, 10.0f, 260.0f, hi, text_(0x6B));
    font.right_fit(c.lines, 530.0f, y, 10.0f, 10.0f, 200.0f, hi, text_(third));
    font.right_fit(c.lines, 600.0f, y, 10.0f, 10.0f, 64.0f, hi, text_(fourth));
  };
  auto team_colour = [&](int id) {
    return id >= 1 && id <= 4 ? scaled_rgb(P[kUiMultiplayerRed + id - 1], 2.0f) : P[kUiNormal];
  };
  // A joined index's glyph: the ship's alone, or pilot / copilot.
  auto glyph_of = [&](int j, const Ship& sh) {
    if (j == sh.pilot && j != sh.copilot) return ship_glyph(sh.number, 1);
    if (j != sh.pilot && j == sh.copilot) return ship_glyph(sh.number, 2);
    return ship_glyph(sh.number, 0);
  };
  auto slot_of = [&](int j) {
    for (int k = 0; k < g_.ship_count; ++k)
      if (g_.ships[size_t(k)].pilot == j || g_.ships[size_t(k)].copilot == j) return k;
    return 0;
  };
  // This game: records by score; per record its joined players (each
  // ship's pilot, then its copilot), 14 apart, 8 more between records.
  {
    float y = 72.0f;
    columns(y, 0x6C, 0x6D);
    y = y + 22.0f;
    std::array<int, 4> order{0, 1, 2, 3};
    const int n = g_.record_count;
    std::stable_sort(order.begin(), order.begin() + n, [this](int a, int b) {
      return g_.world->records[size_t(a)].score > g_.world->records[size_t(b)].score;
    });
    for (int i = 0; i < n; ++i) {
      const int r = order[size_t(i)];
      std::vector<int> js;
      for (int k = 0; k < g_.ship_count; ++k) {
        const Ship& sh = g_.ships[size_t(k)];
        if (sh.record != r) continue;
        for (const int j : {sh.pilot, sh.copilot})
          if (std::find(js.begin(), js.end(), j) == js.end() && js.size() < 4) js.push_back(j);
      }
      for (const int j : js) {
        const int pad = g_.joined_pad[size_t(j & 3)];
        const Ship& sh = g_.ships[size_t(slot_of(j))];
        const Rgba colr = team_colour(sh.colour);
        const std::string num = format_number(g_.world->records[size_t(r)].score);
        font.centre(c.lines, 45.0f, y, 10.0f, 10.0f, colr.data(), format("%i", g_.lobby[size_t(pad)].last_rank + 1));
        font.left(c.lines, 65.0f, y, 10.0f, 10.0f, colr.data(), glyph_of(j, sh));
        font.left_fit(c.lines, 85.0f, y, 10.0f, 10.0f, -(font.measure(num) * 10.0f - 429.0f), colr.data(), pad_name(pad));
        font.right(c.lines, 530.0f, y, 10.0f, 10.0f, colr.data(), num);
        font.right(c.lines, 600.0f, y, 10.0f, 10.0f, colr.data(),
                   format("+%s", format_number(g_.lobby[size_t(pad)].last_points).c_str()));
        y = y + 14.0f;
      }
      y = y + 8.0f;
    }
  }
  // Overall: the joined pads by total (joined first, then total, then
  // pad), sharing a place on equal totals, 22 apart.
  {
    float y = 98.0f + h;
    columns(y, 0x6D, 0x6E);
    y = y + 22.0f;
    std::array<int, 4> pads{0, 1, 2, 3};
    std::stable_sort(pads.begin(), pads.end(), [this](int a, int b) {
      const LobbyPad& la = g_.lobby[size_t(a)];
      const LobbyPad& lb = g_.lobby[size_t(b)];
      if (la.joined != lb.joined) return la.joined;
      return la.total > lb.total;
    });
    int place = 1, last_total = 0;
    for (int i = 0; i < 4; ++i) {
      const int pad = pads[size_t(i)];
      int j = -1;
      for (int k = 0; k < g_.joined_count; ++k)
        if (g_.joined_pad[size_t(k)] == pad) j = k;
      const LobbyPad& l = g_.lobby[size_t(pad)];
      if (!l.joined || j < 0) continue;
      if (last_total != l.total) {
        place = i + 1;
        last_total = l.total;
      }
      const Ship& sh = g_.ships[size_t(slot_of(j))];
      const Rgba colr = team_colour(sh.colour);
      font.centre(c.lines, 45.0f, y, 10.0f, 10.0f, colr.data(), format("%i", place));
      font.left(c.lines, 65.0f, y, 10.0f, 10.0f, colr.data(), glyph_of(j, sh));
      font.left(c.lines, 85.0f, y, 10.0f, 10.0f, colr.data(), pad_name(pad));
      font.right(c.lines, 530.0f, y, 10.0f, 10.0f, colr.data(), format("+%s", format_number(l.last_points).c_str()));
      font.right(c.lines, 600.0f, y, 10.0f, 10.0f, colr.data(), format_number(l.total));
      y = y + 22.0f;
    }
  }
}

// ---------------------------------------------------------------------------
// Local leaderboards

void Frontend::leaderboards(bool reset) {
  const bool in = in_game(g_);
  open(8,
       [this, in](const MenuInput&) {
         if (in)
           pause(false);
         else
           main_menu(false);
       },
       [this, in](UiCanvas& c) {
         if (in)
           draw_game_hud(g_, c.lines);
         else
           draw_title_block(c);
       },
       reset, {}, 4);
  screen_.width = 260.0f;
  screen_.tips = [this](TextLayout& l) { tips_back(l); };
  for (int t : kModeItems)
    add_item(t, [this](const MenuInput&, int, int text) {
      board_view(mode_of_text(text), [this](const MenuInput&) { leaderboards(false); });
    });
}

std::vector<GroupRow> Frontend::group_board(int mode, int& own) const {
  own = -1;
  if (!online_ || !online_->paired() || mode < 0 || mode > 5) return {};
  std::vector<GroupRow> rows = online_->boards()[size_t(mode)];
  const std::string me = online_->player_id();
  // This profile's best (its games from every device, and the one just
  // played) for the player's row, should the server not have it yet.
  const LocalBoard& local = boards_[size_t(mode)];
  const int64_t best = local.rows.empty() ? 0 : local.rows.front().score;
  auto it = std::find_if(rows.begin(), rows.end(), [&](const GroupRow& r) { return r.player == me; });
  if (best > 0 && (it == rows.end() || it->score < best)) {
    if (it != rows.end()) rows.erase(it);
    rows.push_back(GroupRow{me, online_->player(), best, 1, 0});
    std::stable_sort(rows.begin(), rows.end(), [](const GroupRow& a, const GroupRow& b) { return a.score > b.score; });
  }
  for (size_t i = 0; i < rows.size(); ++i)
    if (rows[i].player == me) own = int(i);
  return rows;
}

void Frontend::board_view(int mode, std::function<void(const MenuInput&)> back) {
  board_mode_ = std::clamp(mode, 0, 5);
  board_first_ = 0;
  // sub_8208AAB8 starts at Friends. Without a score server the port has
  // only the local board, shown as Overall.
  board_filter_ = online_ && online_->paired() ? 0 : 2;
  open(0, back,
       [this](UiCanvas& c) {
         const Font& font = g_.font;
         const UiPalette& P = palette_;
         ui_bar(c, P, 18.0f, 40.0f);
         ui_panel(c, 0.0f, 46.0f, 22.0f, cursor_, P[kUiBackground], 2);
         ui_panel(c, 618.0f, 46.0f, 640.0f, cursor_, P[kUiBackground], 4);
         static const int kBoardName[6] = {0x3D, 0x3B, 0x3C, 0x3F, 0x3E, 0x40};  // by board: Deadline, Evolved, ...
         static const int kBoardOfMode[6] = {0, 1, 2, 4, 3, 5};
         // The filter's name (sub_8208A2C0): 0x29 Friends, 0x2A My score, 0x2B Overall.
         font.centre_fit(c.lines, 320.0f, 24.0f, 15.0f, 10.0f, 540.0f, P[kUiNormal].data(),
                         format("%s - %s", text_(kBoardName[kBoardOfMode[board_mode_]]).c_str(),
                                text_(0x29 + board_filter_).c_str()));
         // The rows: the group's board (Friends, My score) or the local one.
         int own = -1;
         const std::vector<GroupRow> group = board_filter_ < 2 ? group_board(board_mode_, own) : std::vector<GroupRow>{};
         const LocalBoard& b = boards_[size_t(board_mode_)];
         const int count = board_filter_ < 2 ? int(group.size()) : int(b.rows.size());
         const float rh = ((cursor_ - 46.0f) + 6.0f) * 0.1f;
         for (int i = 0; i < 10; ++i) {
           const float y = fmadds(float(i), rh, 46.0f);
           const int r = board_first_ + i;
           // The player's own row is boxed, as the cursor row is (in My score
           // the cursor starts on it, sub_8208B178).
           const bool mine = r == own;
           ui_panel(c, 28.0f, y, 606.0f, (y + rh) - 6.0f, mine ? P[kUiSelection] : P[kUiBackground], 0);
           if (r >= count) continue;
           const Rgba& col = mine ? P[kUiHighlight] : P[kUiNormal];
           const std::string& name = board_filter_ < 2 ? group[size_t(r)].name : b.rows[size_t(r)].name;
           const int64_t score = board_filter_ < 2 ? group[size_t(r)].score : b.rows[size_t(r)].score;
           font.centre(c.lines, 28.0f + 80.0f, y + 2.0f, 8.0f, 8.0f, col.data(), format("%s.", format_number(r + 1).c_str()));
           font.left_fit(c.lines, 28.0f + 130.0f, y + 2.0f, 10.0f, 10.0f, 300.0f, col.data(), display_name(name));
           font.right(c.lines, 28.0f + 570.0f, y + 2.0f, 10.0f, 10.0f, col.data(), format_number(score));
         }
         // No entries (0x7E) on an empty group board.
         if (board_filter_ < 2 && count == 0)
           font.centre(c.lines, 320.0f, fmadds(rh, 0.5f, 46.0f) - 5.0f, 10.0f, 10.0f, P[kUiNormal].data(), text_(0x7E));
         // The scroll bar (sub_820913B0 at 612, ten rows).
         ui_scroll_bar(c, 612.0f, 46.0f, cursor_, float(board_first_), float(count), 10.0f,
                       scaled(P[kUiScrollBar], 0.5f), scaled(P[kUiBackground], 0.5f), scaled(P[kUiBackground], 0.25f));
       },
       false,
       [this](const MenuInput& in) {
         if (in.pressed & kBack) {
           if (const auto b = screen_.back) b(in);
           return;
         }
         // X steps the filter (sub_82089D58, sound 0x18); My score opens on
         // the page holding the player's row (sub_8208B178).
         if ((in.pressed & kX) && online_ && online_->paired()) {
           board_filter_ = (board_filter_ + 1) % 3;
           board_first_ = 0;
           if (board_filter_ == 1) {
             int own = -1;
             group_board(board_mode_, own);
             if (own >= 0) board_first_ = (own / 10) * 10;
           }
           sound(audio::Event::kUiConfirm);
         }
         int own_unused = -1;
         const int count = board_filter_ < 2 ? int(group_board(board_mode_, own_unused).size())
                                             : int(boards_[size_t(board_mode_)].rows.size());
         const int max_first = std::max(count - 10, 0);
         if (in.pressed & kUp) board_first_ = std::clamp(board_first_ - 1, 0, max_first);
         if (in.pressed & kDown) board_first_ = std::clamp(board_first_ + 1, 0, max_first);
         // sub_82089650: the right stick fills an accumulator (signed RY²
         // times 0.333 a frame), LB/LT and RB/RT add a page of 8; at most
         // one row each way a frame, sound 0x16 when that moves the row; the
         // remainder settles 4 frames after the last such input.
         const float ry = in.analog[1];
         if (std::fabs(ry) > 0.1f) {  // dat_82000714
           board_hold_ = 4;
           const float sq = ry * ry;
           board_scroll_ = fmadds(ry >= 0.0f ? sq : -sq, 0.333f, board_scroll_);  // fsel; dat_82005E44
         }
         if (in.pressed & (kLb | kLt)) {
           board_scroll_ = board_scroll_ - 8.0f;  // dat_82000D18
           board_hold_ = 4;
         }
         if (in.pressed & (kRb | kRt)) {
           board_scroll_ = board_scroll_ + 8.0f;
           board_hold_ = 4;
         }
         auto step = [&](int d) {
           const int row = std::clamp(board_first_ + d, 0, max_first);
           if (row != board_first_ && board_hold_ == 4) sound(audio::Event::kUiChange);
           board_first_ = row;
         };
         if (board_scroll_ <= -1.0f) {
           board_scroll_ = board_scroll_ + 1.0f;
           step(-1);
         }
         if (board_scroll_ >= 1.0f) {
           board_scroll_ = board_scroll_ - 1.0f;
           step(1);
         }
         if ((board_hold_ < 1 || --board_hold_ == 0) && std::fabs(board_scroll_) < 1.0f) board_scroll_ = 0.0f;
       },
       0);
  screen_.tips = [this](TextLayout& l) {
    // sub_82089BF0's X Change Filter (0x31), with a score server.
    if (online_ && online_->paired()) {
      l.add(palette_[kUiHighlight], " X ");
      l.add(palette_[kUiNormal], text_(0x31));
    }
    l.add(palette_[kUiHighlight], " \x1D\x1E ")->scale = 0.75f;
    l.add(palette_[kUiNormal], text_(0x2C));
    l.add(palette_[kUiHighlight], " B ");
    l.add(palette_[kUiNormal], text_(0x2E));
  };
}

// ---------------------------------------------------------------------------
// Online scores (the port's own: a group's score server in place of Xbox
// LIVE; DIFFERENCES.md "Online scores"). The screens follow the menus'
// look; their text is English, as the original has none for them.

void Frontend::preview_online(const std::string& state) {
  online_screen(true);
  if (state == "editing") {
    editing_ = true;
    edit_ = "geow2-scores.example.workers.dev";
  } else if (state == "waiting" || state == "failed") {
    pairing_screen();
  } else if (state == "boards" || state == "boards-mine") {
    // Deadline's board, Friends or My score ("paired" data).
    board_view(0, [this](const MenuInput&) { leaderboards(false); });
    if (state == "boards-mine") board_filter_ = 1;
  }
}

void Frontend::type(const std::string& utf8) {
  if (!editing_) return;
  // An address: printable ASCII only.
  for (const char ch : utf8)
    if (ch > 0x20 && ch < 0x7F && edit_.size() < 200) edit_.push_back(ch);
}

void Frontend::text_key(TextKey key) {
  if (!editing_) return;
  if (key == TextKey::kBackspace && !edit_.empty()) edit_.pop_back();
  if (key == TextKey::kEnter) finish_editing(true);
  if (key == TextKey::kEscape) finish_editing(false);
}

void Frontend::finish_editing(bool keep) {
  if (!editing_) return;
  editing_ = false;
  if (keep && online_) {
    online_message_ = online_->set_server(edit_);
    sound(online_message_.empty() ? audio::Event::kUiConfirm : audio::Event::kUiDelete);
  } else {
    sound(audio::Event::kUiDelete);
  }
  online_screen(false);
}

void Frontend::online_screen(bool reset) {
  if (!online_) return;
  online_paired_ = online_->paired();
  if (reset) online_message_.clear();
  open(9,
       [this](const MenuInput&) {
         editing_ = false;
         help(false);
       },
       [this](UiCanvas& c) {
         const Font& font = g_.font;
         const UiPalette& P = palette_;
         ui_bar(c, P, 18.0f, 40.0f);
         font.centre_fit(c.lines, 320.0f, 24.0f, 15.0f, 10.0f, 540.0f, P[kUiNormal].data(), "Online scores");
         ui_panel(c, 28.0f, 46.0f, 612.0f, cursor_, P[kUiBackground], 0);
         float y = 54.0f;
         auto line = [&](const std::string& s, const Rgba& col, float size = 8.0f) {
           font.left_fit(c.lines, 40.0f, y, size, size, 560.0f, col.data(), s);
           y += size + 6.0f;
         };
         if (online_->paired()) {
           const std::string group = online_->group();
           line(display_name(online_->player()), P[kUiHighlight], 12.0f);
           line(group.empty() ? online_->server() : group + "  (" + online_->server() + ")", P[kUiNormal]);
           y += 4.0f;
           y += text_box(font, c.lines, P, 40.0f, y, 7.0f, 7.0f, 3.0f, 560.0f, 0, 0, P[kUiNormal],
                         "This device's games, unlocked modes and achievements are shared with your other paired "
                         "devices, and the leaderboards show the group's best scores. It syncs when the game starts "
                         "and after each game.") +
                6.0f;
           // The status after a blank line.
           y += 10.0f;
           line("Current status: " + online_->status(), P[kUiNormal], 7.0f);
           return;
         }
         y += text_box(font, c.lines, P, 40.0f, y, 7.0f, 7.0f, 3.0f, 560.0f, 0, 0, P[kUiNormal],
                       "Share your high scores, unlocked modes and achievements between your devices and with your "
                       "friends, through your group's score server. Someone in the group sets one up (see "
                       "geow2.com) and sends invite links: open yours on your phone, then enter the server's "
                       "address here and pair this device.") +
              8.0f;
         if (editing_) {
           line("Server address:", P[kUiNormal], 7.0f);
           const bool blink = std::fmod(clock_, 1.0f) < 0.5f;
           line(edit_ + (blink ? "_" : " "), P[kUiHighlight], 10.0f);
           line("Enter or A: done.  Escape or B: cancel.  On a Steam Deck, STEAM + X shows the keyboard.",
                P[kUiNormal], 6.0f);
         } else if (!online_message_.empty()) {
           line(online_message_, P[kUiHighlight], 7.0f);
         }
       },
       reset,
       [this](const MenuInput& in) {
         if (editing_) {
           // A pad finishes or cancels; the keyboard types (text_key).
           if (in.pressed & kA) finish_editing(true);
           else if (in.pressed & kBack) finish_editing(false);
           return;
         }
         if (online_->paired() != online_paired_) {
           online_screen(false);
           return;
         }
         default_handler(in);
       },
       0);
  screen_.width = 300.0f;
  screen_.tips = [this](TextLayout& l) {
    if (!editing_) tips_back(l);
  };
  if (!online_paired_) {
    add_item(0,
             [this](const MenuInput&, int, int) {
               editing_ = true;
               edit_ = online_->server();
               online_message_.clear();
               sound(audio::Event::kUiConfirm);
             },
             [this](TextLayout& l) {
               const std::string s = online_->server();
               l.add(palette_[kUiNormal], s.empty() ? "Enter the server's address" : "Server: " + s);
             });
    // Only once there is an address: a greyed item in a two-item menu would
    // take the selection for a frame (sub_82090180 moves to the other of
    // two items without checking it).
    if (!online_->server().empty())
      add_item(0, [this](const MenuInput&, int, int) { pairing_screen(); },
               [this](TextLayout& l) { l.add(palette_[kUiNormal], "Pair this device"); });
  } else {
    add_item(0, [this](const MenuInput&, int, int) {
      online_->sync_now();
      sound(audio::Event::kUiConfirm);
    }, [this](TextLayout& l) { l.add(palette_[kUiNormal], "Sync now"); });
    add_item(0, [this](const MenuInput&, int, int) {
      online_->unpair();
      sound(audio::Event::kUiDelete);
      online_screen(false);
    }, [this](TextLayout& l) { l.add(palette_[kUiNormal], "Unpair this device"); });
  }
}

void Frontend::pairing_screen() {
  online_->start_pairing();
  open(0,
       [this](const MenuInput&) {
         online_->cancel_pairing();
         online_screen(false);
       },
       [this](UiCanvas& c) {
         const Font& font = g_.font;
         const UiPalette& P = palette_;
         ui_bar(c, P, 18.0f, 40.0f);
         font.centre_fit(c.lines, 320.0f, 24.0f, 15.0f, 10.0f, 540.0f, P[kUiNormal].data(), "Pair this device");
         ui_panel(c, 28.0f, 46.0f, 612.0f, cursor_, P[kUiBackground], 0);
         const OnlineMenu::Pairing p = online_->pairing();
         const float top = 54.0f, side = std::min(cursor_ - 8.0f - top, 190.0f);
         float x = 40.0f;
         // The confirming page's address as a QR code (texture kTexQr, made
         // by the caller from qr_text()), flat so the camera sees squares.
         qr_text_ = p.state == OnlineMenu::Pairing::kWaiting ? p.url : std::string();
         if (!qr_text_.empty()) {
           // (A sprite's colour is stored times 8; past full brightness, so
           // the white stays white under the UI's dimming.)
           const float k = 0.25f;
           c.sprite(x, top, side, side, {k, k, k, 1.0f}, kTexQr, true);
           x += side + 20.0f;
         }
         float y = top;
         const float w = 600.0f - x;
         auto line = [&](const std::string& s, const Rgba& col, float size) {
           font.left_fit(c.lines, x, y, size, size, w, col.data(), s);
           y += size + 6.0f;
         };
         switch (p.state) {
           case OnlineMenu::Pairing::kIdle:
           case OnlineMenu::Pairing::kStarting:
             line("Asking the server for a code...", P[kUiNormal], 8.0f);
             break;
           case OnlineMenu::Pairing::kWaiting:
             y += text_box(font, c.lines, P, x, y, 7.0f, 7.0f, 3.0f, w, 0, 0, P[kUiNormal],
                           "On the phone you joined the group with, scan the code, or open your page on the server "
                           "and type this code:") +
                  8.0f;
             line(p.code, P[kUiHighlight], 20.0f);
             y += 4.0f;
             text_box(font, c.lines, P, x, y, 7.0f, 7.0f, 3.0f, w, 0, 0, P[kUiNormal],
                      "Then confirm on the phone. This screen closes by itself once it's done. Codes last ten "
                      "minutes.");
             break;
           case OnlineMenu::Pairing::kPaired:
             line("Paired.", P[kUiHighlight], 10.0f);
             break;
           case OnlineMenu::Pairing::kFailed:
             line("Pairing failed:", P[kUiNormal], 8.0f);
             text_box(font, c.lines, P, x, y, 7.0f, 7.0f, 3.0f, w, 0, 0, P[kUiHighlight], p.error);
             break;
         }
       },
       false,
       [this](const MenuInput& in) {
         if (online_->paired()) {
           sound(audio::Event::kUiConfirm);
           online_screen(false);
           return;
         }
         if (in.pressed & kBack) {
           if (const auto b = screen_.back) b(in);
         }
       },
       0);
  screen_.tips = [this](TextLayout& l) {
    l.add(palette_[kUiHighlight], " B ");
    l.add(palette_[kUiNormal], text_(0x2E));
  };
}

// ---------------------------------------------------------------------------
// Achievements (the dashboard's list in the original; DIFFERENCES.md)

void Frontend::achievements_screen() {
  const bool in = in_game(g_);
  achievement_first_ = 0;
  open(0,
       [this, in](const MenuInput&) {
         if (in)
           pause(false);
         else
           main_menu(false);
       },
       [this](UiCanvas& c) {
         const Font& font = g_.font;
         const UiPalette& P = palette_;
         int unlocked = 0, score = 0, total = 0;
         for (const auto& a : g_.achievements) {
           total += a.gamerscore;
           if (g_.achieved & a.bit) {
             ++unlocked;
             score += a.gamerscore;
           }
         }
         ui_bar(c, P, 18.0f, 40.0f);
         font.centre_fit(c.lines, 320.0f, 24.0f, 15.0f, 10.0f, 540.0f, P[kUiNormal].data(),
                         format("%s - %i / %i - %i / %i", text_(0x10).c_str(), unlocked, int(g_.achievements.size()),
                                score, total));
         ui_panel(c, 0.0f, 46.0f, 22.0f, cursor_, P[kUiBackground], 2);
         ui_panel(c, 618.0f, 46.0f, 640.0f, cursor_, P[kUiBackground], 4);
         // Six rows at a time.
         const float rh = ((cursor_ - 46.0f) + 6.0f) / 6.0f;
         for (int i = 0; i < 6; ++i) {
           const int k = achievement_first_ + i;
           if (k >= int(g_.achievements.size())) break;
           const auto& a = g_.achievements[size_t(k)];
           const bool got = (g_.achieved & a.bit) != 0;
           const float y = fmadds(float(i), rh, 46.0f), y1 = (y + rh) - 6.0f;
           ui_panel(c, 28.0f, y, 612.0f, y1, got ? P[kUiBackground] : P[kUiLockedBackground], 0);
           const float icon = (y1 - y) - 8.0f;
           const float b = got ? 0.125f : 0.03125f;
           c.sprite(34.0f, y + 4.0f, icon, icon, {b, b, b, 1.0f}, kTexAchievement0 + k, false);
           const Rgba& tc = got ? P[kUiNormal] : P[kUiDisabled];
           const float x = 34.0f + icon + 10.0f;
           font.left_fit(c.lines, x, y + 5.0f, 10.0f, 10.0f, 600.0f - x - 60.0f, tc.data(), a.name);
           font.right(c.lines, 600.0f, y + 5.0f, 10.0f, 10.0f, tc.data(), format("%i", a.gamerscore));
           text_box(font, c.lines, P, x, y + 20.0f, 7.0f, 7.0f, 3.0f, 600.0f - x, 0, 0,
                    got ? P[kUiHighlight] : P[kUiDisabled], a.description);
         }
       },
       false,
       [this](const MenuInput& in) {
         if (in.pressed & kBack) {
           if (const auto b = screen_.back) b(in);
           return;
         }
         const int max_first = std::max(int(g_.achievements.size()) - 6, 0);
         if (in.pressed & kUp) achievement_first_ = std::clamp(achievement_first_ - 1, 0, max_first);
         if (in.pressed & kDown) achievement_first_ = std::clamp(achievement_first_ + 1, 0, max_first);
       },
       0);
  screen_.tips = [this](TextLayout& l) {
    l.add(palette_[kUiHighlight], " \x1D\x1E ")->scale = 0.75f;
    l.add(palette_[kUiNormal], text_(0x2C));
    l.add(palette_[kUiHighlight], " B ");
    l.add(palette_[kUiNormal], text_(0x2E));
  };
}

}  // namespace geow2
