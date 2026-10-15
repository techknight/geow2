#include "app/frontend.h"

#include <algorithm>
#include <cmath>

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

// The text id of a mode's name.
int text_of_mode(int mode) {
  static const int kText[6] = {0x1E, 0x1C, 0x1D, 0x1F, 0x20, 0x21};
  return mode >= 0 && mode < 6 ? kText[mode] : 0x1E;
}

// The selection pulse: fma(fma(cos(phase), 3, 1), 0.5, 1.5), 0.5 .. 3.5.
float pulse_factor(const Game& g, float phase) {
  const float c = float(g.gm->cos(phase));
  return std::fmaf(std::fmaf(c, 3.0f, 1.0f), 0.5f, 1.5f);
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

std::string Frontend::player_name() const {
  // Gamertags stand in as "%s %i" with text 0x82 and the pad (0x820049D0).
  return format("%s %i", text_(0x82).c_str(), game_pad_ + 1);
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
  if (n == 0) {
    if (p & (kStart | kA)) {
      // Sound 0x18.
      if (screen_.accept)
        screen_.accept(in);
      else if (screen_.back)
        screen_.back(in);
    } else if ((p & kBack) && screen_.back) {
      screen_.back(in);  // sound 0x17
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
    if (moved) reset_pulse();  // sound 0x16
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
    if (moved) reset_pulse();
  }
  const Item& it = screen_.items[size_t(s)];
  if (p & (kStart | kA)) {
    // Sound 0x17 for an item that goes back, else 0x18 (none when
    // adjustable).
    if (it.action && item_enabled(s)) {
      const Action a = it.action;
      a(in, s, it.text);
    }
  } else if (p & kBack) {
    if (screen_.back) screen_.back(in);
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
  pulse_ = wrap_phase(std::fmaf(kStep, 8.0f, pulse_));  // dat_82000D18
}

// ---------------------------------------------------------------------------
// Input and the flow

void Frontend::frame(const std::array<PadState, 4>& pads) {
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
}

void Frontend::request_game(int mode) {
  // sub_8208D410: only an unlocked mode; the HUD becomes the screen, then
  // sub_820334F8 (state 1 with that pad) and sub_82033148 at once.
  if (!profile_.mode_unlocked(mode)) return;
  open(0, {}, [this](UiCanvas& c) { draw_game_hud(g_, c.lines); }, false, {}, 0);
  game_pad_ = menu_pad_ >= 0 ? menu_pad_ : 0;
  start_game(g_, mode);  // (and the game music, sub_82078A50(1))
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
  if (in.pressed & kStart) {
    // Sound 0x18.
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
  // sub_820338D8 (menu music, sub_82078A50(0)).
  const int reason = g_.end_reason;
  if (reason == 2) {
    const int mode = g_.rules.mode;
    const std::string name = player_name();
    if (mode >= 0 && mode < 6) {
      g_.tables[size_t(mode)].insert(g_.world->score, g_.world->multiplier, name);
      // The local board in place of the Live score write (sub_8207E490).
      last_row_ = boards_[size_t(mode)].add(g_.world->score, name);
      if (last_row_ >= 0 && hooks_.save_boards) hooks_.save_boards();
    }
    if (profile_.game_ended(mode, g_.world->kills, g_.world->score) && hooks_.save_profile) hooks_.save_profile();
  }
  g_.end_reason = 0;
  g_.flow = 0;
  fade_ = 0.0f;
  switch (reason) {
    case 1: after_game(); break;
    case 2: results(); break;
    default: main_menu(true); break;
  }
}

void Frontend::after_game() { mode_select(true); }  // sub_8208EF90, single player

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
  // sub_8208F718: Ui.Title.ShowBox and ShowLogo are off; the logo texture
  // and the star flare (ShowTex, ShowStar) are sprites, not drawn yet.
  // Then Ui.Title.ShowLine: the panel's bottom edge at the menu's top.
  ui_panel(c, 0.0f, 18.0f, 640.0f, cursor_, palette_[kUiBackground], 0xF);
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
    const float y0 = cur - std::fmaf(fs + 20.0f, float(n), screen_.descriptions ? 10.0f : 0.0f);
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
    const float xl = std::fmaf(-(W + 48.0f), 0.5f, 320.0f), xr = std::fmaf(W + 48.0f, 0.5f, 320.0f);
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
    const std::string name = player_name();
    const Rgba white = scaled(Rgba{1, 1, 1, 1}, 1.0f);
    const Rgba nc{F[0] * white[0], F[1] * white[1], F[2] * white[2], F[3] * white[3]};
    font.centre(c.lines, 320.0f, 346.0f, 6.0f, 6.0f, nc.data(), name);
    const float m = font.measure(name);
    const float x = (320.0f - std::fmaf(m, 6.0f, 2.0f) * 0.5f) - 2.0f;
    for (int k = 0; k < 4; ++k) {
      const Rgba col = k == menu_pad_ ? Rgba{0.15f, 1.5f, 0.15f, 1.0f} : Rgba{0.15f, 0.15f, 0.15f, 1.0f};
      const Rgba fc{col[0] * F[0], col[1] * F[1], col[2] * F[2], col[3] * F[3]};
      font.right(c.lines, x, 346.0f, 6.0f, 6.0f, fc.data(), std::string(1, char(0x11 + k)));
    }
  }
}

// ---------------------------------------------------------------------------
// Boot and the main menu

void Frontend::boot() {
  // The intro's two splashes are textures (retroatvi.tga, retrobizarre.tga)
  // not drawn yet; the main menu comes at once.
  g_.flow = 0;
  main_menu(false);
}

void Frontend::main_menu(bool reset) {
  open(1, {}, [this](UiCanvas& c) { draw_title_block(c); }, reset, {}, 0);
  screen_.tips = [this](TextLayout& l) { tips_menu(l); };
  add_item(0x01, [this](const MenuInput& in, int, int) {
    if (in.pad == -1) return;
    menu_pad_ = in.pad;
    mode_select(true);
  });
  // Multiplayer and the achievements screen are not ported yet: shown
  // locked.
  add_item(0x02, {}, {}, [](int, int) { return false; });
  add_item(0x04, [this](const MenuInput& in, int, int) {
    if (in.pad == -1) return;
    menu_pad_ = in.pad;
    leaderboards(true);
  });
  add_item(0x10, {}, {}, [](int, int) { return false; });
  add_item(0x05, [this](const MenuInput& in, int, int) {
    if (in.pad == -1) return;
    help(true);
    menu_pad_ = in.pad;
  });
  add_item(0x07, [this](const MenuInput&, int, int) { quit_confirm(); });
  menu_pad_ = -1;  // sub_82033698: every pad drives the main menu
}

void Frontend::quit_confirm() {
  open(0, [this](const MenuInput&) { main_menu(false); }, {}, false, {}, 0x73);
  screen_.tips = [this](TextLayout& l) { tips_menu(l); };
  add_item(0x17, [this](const MenuInput&, int, int) { main_menu(false); });
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
      break;
    }
}

void Frontend::mode_select_update(const MenuInput& in) {
  tile_phase_ = wrap_phase(std::fmaf(kStep, 5.0f, tile_phase_));  // dat_820006F0
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
      reset_pulse();  // sound 0x16
      return true;
    }
    return false;
  };
  if (!navigate(in.pressed)) navigate((in.pressed ^ in.raw) & in.raw);
  if (in.pressed & kX) {
    // The rules screen, without a start item.
    const int x = profile_index(mode_of_text(kModeItems[selection_value()]));
    rules(x, [this](const MenuInput&) { mode_select(false); }, 0, {});
    return;
  }
  if (in.pressed & kY) {
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
  const float y0 = std::fmaf(float(index % 3), H, 46.0f);
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
  // The icon (a CharMap model, sub_82061FC0) is not drawn yet; the name
  // under it.
  const float yi = std::fmaf(h - 20.0f, 0.5f, y0) + 10.0f;
  font.centre_fit(c.lines, x0 + 44.0f, yi + 10.0f, 6.0f, 6.0f, 75.0f, col.data(), text_(text));
  const float L = x0 + 88.0f, R = x0 + 280.0f, C = (R + L) * 0.5f;
  if (fresh) {
    text_box(font, c.lines, palette_, C, std::fmaf(h - 7.0f, 0.5f, y0), 7.0f, 7.0f, 3.0f, (R - L) - 18.0f, 2, 1, col,
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
    const float q = std::fmaf(float(g_.gm->cos(tile_phase_)) + 1.0f, 1.5f, 1.0f);
    const Rgba cq = scaled(col, q);
    int pct = int((float(profile_.progress[size_t(pre_x)]) * 100.0f) / float(kKillsToUnlock));
    pct = std::clamp(pct, 0, 100);
    font.centre_fit(c.lines, C, y0 + 37.0f, 7.0f, 7.0f, (R - L) - 6.0f, cq.data(),
                    format("%s: %i%%", text_(0x8A).c_str(), pct));
    static const int kHint[6] = {0x90, 0x8E, 0x91, 0x92, 0x8F, 0x93};
    text_box(font, c.lines, palette_, C, y0 + 52.0f, 7.0f, 7.0f, 3.0f, 192.0f, 2, 0, col, text_(kHint[x]));
  } else {
    text_box(font, c.lines, palette_, C, std::fmaf(h, 0.5f, y0), 7.0f, 7.0f, 3.0f, (R - L) - 18.0f, 2, 1, col,
             text_(0x87));
  }
}

// ---------------------------------------------------------------------------
// The rules screens

void Frontend::rules(int index, std::function<void(const MenuInput&)> back, int item_text, Action start) {
  static const int kTitle[6] = {0x1C, 0x1E, 0x20, 0x1F, 0x21, 0x1D};  // by profile index
  // The lines per mode: text id, and the icon's name when it is text.
  struct Line {
    int text;
    const char* glyphs;  // drawn as text when the icon is not a model
  };
  struct Rules {
    std::vector<Line> lines;
    std::vector<int> tips;
    bool move, fire, bomb;
  };
  static const Rules kRules[6] = {
      {{{0x45, nullptr}, {0x46, nullptr}, {0x47, "\x0C\x02"}}, {0x48, 0x49}, true, true, true},  // Evolved
      {{{0x4A, nullptr}, {0x4B, nullptr}, {0x4C, nullptr}}, {0x4D, 0x4E}, true, true, true},     // Deadline
      {{{0x4F, nullptr}, {0x50, nullptr}, {0x51, nullptr}}, {0x52, 0x53}, true, false, false},   // Pacifism
      {{{0x54, nullptr}}, {0x55, 0x56, 0x57}, true, true, false},                                // Waves
      {{{0x58, nullptr}, {0x59, nullptr}, {0x5A, nullptr}}, {0x5B, 0x5C}, true, true, false},    // King
      {{{0x5D, nullptr}, {0x5E, nullptr}, {0x5F, nullptr}}, {0x60, 0x61}, true, true, true},     // Sequence
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
         auto line = [&](int text, const char* glyphs, bool tip) {
           const float t = text_box(font, c.lines, palette_, 92.0f, y, 8.0f, 8.0f, 4.0f, 288.0f, 0, 0,
                                    palette_[kUiNormal], text_(text));
           const float yi = y + std::min(t * 0.5f, 10.0f);
           if (tip)
             font.centre(c.lines, 72.0f, yi, 2.0f, 2.0f, palette_[kUiHighlight].data(), "O");
           else if (glyphs)
             font.centre(c.lines, 72.0f, yi, 6.0f, 6.0f, palette_[kUiHighlight].data(), glyphs);
           // (Model icons, sub_8207A798: not drawn yet.)
           y = y + (t + 14.0f);
         };
         for (const Line& l : kRules[x].lines) line(l.text, l.glyphs, false);
         y = y + 8.0f;
         font.left(c.lines, 52.0f, y, 8.0f, 8.0f, palette_[kUiHighlight].data(), text_(0x63));
         y = y + 24.0f;
         for (int t : kRules[x].tips) line(t, nullptr, true);
         // (The controller diagram sub_82091630(500, 180, 200, ...) is a
         // texture with labels: not drawn yet.)
       },
       false,
       [this](const MenuInput& in) {
         // sub_8207A658: without items, A or X go back.
         if (screen_.items.empty() && (in.pressed & (kA | kX))) {
           if (screen_.back) screen_.back(in);
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
  add_item(0x05, [this](const MenuInput&, int, int) { help(true); });
  add_item(0x04, [this](const MenuInput&, int, int) { leaderboards(true); });
  add_item(0x10, {}, {}, [](int, int) { return false; });
  add_item(0x09, [this](const MenuInput&, int, int) { exit_confirm(); });
}

void Frontend::exit_confirm() {
  open(0, [this](const MenuInput&) { pause(false); }, [this](UiCanvas& c) { draw_game_hud(g_, c.lines); }, false,
       {}, 0x72);
  screen_.tips = [this](TextLayout& l) { tips_menu(l); };
  add_item(0x17, [this](const MenuInput&, int, int) { pause(false); });
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
         // (The controller diagram: a texture, not drawn yet.)
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
            dirty_ = true;
          } else if ((in.pressed & kRight) && !set) {
            profile_.controls |= bit;
            reset_pulse();
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
            if (0.0f < x) reset_pulse();
            const float y = x - 0.05f;  // dat_82000D60
            x = y >= 0.0f ? (y - 1.0f >= 0.0f ? 1.0f : y) : 0.0f;
            dirty_ = true;
          } else if (in.pressed & kRight) {
            if (x < 1.0f) reset_pulse();
            const float y = x + 0.05f;
            x = y >= 0.0f ? (y - 1.0f >= 0.0f ? 1.0f : y) : 0.0f;
            dirty_ = true;
          }
        },
        [this, text, v](TextLayout& l) {
          const int n = int(std::fmaf(profile_.*v, 100.0f, 0.005f));
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
          dirty_ = true;
        }
        if ((in.pressed & kRight) && uint32_t(d + 1) < 2) {
          ++d;
          reset_pulse();
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
         credits_scroll_ = std::fmaf(std::fmaf(v, 400.0f, 40.0f), kStep, credits_scroll_);
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
           ui_panel(c, 0.0f, y, 160.0f, y + 14.0f, P[kUiBackground], 0);
           ui_panel(c, 166.0f, y, 474.0f, y + 14.0f, P[kUiBackground], 0);
           ui_panel(c, 480.0f, y, 640.0f, y + 14.0f, P[kUiBackground], 0);
           font.centre_fit(c.lines, 320.0f, (y + 7.0f) - 4.0f, 8.0f, 8.0f, 300.0f, P[kUiHighlight].data(), t);
           return 20.0f;
         };
         auto two = [&](float y, const std::string& a, const std::string& b) {
           const float xs[5][2] = {{0, 25}, {31, 311}, {317, 323}, {329, 609}, {615, 640}};
           for (const auto& x : xs) ui_panel(c, x[0], y, x[1], y + 14.0f, P[kUiBackground], 0);
           font.centre_fit(c.lines, 171.0f, (y + 7.0f) - 4.0f, 8.0f, 8.0f, 276.0f, P[kUiHighlight].data(), a);
           font.centre_fit(c.lines, 469.0f, (y + 7.0f) - 4.0f, 8.0f, 8.0f, 276.0f, P[kUiHighlight].data(), b);
           return 20.0f;
         };
         auto box = [&](float y, float h, int64_t v, float size, float drop) {
           ui_panel(c, 0.0f, y, 22.0f, y + h, P[kUiBackground], 0);
           ui_panel(c, 28.0f, y, 612.0f, y + h, P[kUiBackground], 0);
           ui_panel(c, 618.0f, y, 640.0f, y + h, P[kUiBackground], 0);
           font.centre(c.lines, 320.0f, std::fmaf(h, 0.5f, y) - drop, size, size, P[kUiNormal].data(), format_number(v));
           return h + 6.0f;
         };
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
           const float ry = std::fmaf(float(k), 14.0f, 148.0f);
           const ScoreEntry& e = T.entry[size_t(2 + k)];
           const bool last = k == 0 && T.entry[0].score > 0 && e.score == T.entry[0].score &&
                             e.multiplier == T.entry[0].multiplier && e.name == T.entry[0].name;
           const Rgba& col = last ? P[kUiHighlight] : P[kUiNormal];
           if (e.name.empty()) {
             font.left(c.lines, 34.0f, ry, 8.0f, 8.0f, P[kUiDisabled].data(), "-");
             font.right(c.lines, 308.0f, ry, 8.0f, 8.0f, P[kUiDisabled].data(), "-");
           } else {
             const std::string num = format_number(e.score);
             font.left_fit(c.lines, 34.0f, ry, 8.0f, 8.0f, std::fmaf(-font.measure(num), 8.0f, 258.0f), col.data(), e.name);
             font.right(c.lines, 308.0f, ry, 8.0f, 8.0f, col.data(), num);
           }
         }
         // The local board's rows around the last game's.
         const LocalBoard& b = boards_[size_t(mode)];
         const int count = int(b.rows.size());
         const int first = std::clamp(last_row_ - 4, 0, std::max(count - 8, 0));
         const float pitch = ((L - 148.0f) + 6.0f) * 0.125f;
         const float off = ((pitch - 6.0f) - 8.0f) * 0.5f;
         float ry = 148.0f;
         for (int j = 0; j < 8; ++j) {
           const int r = first + j;
           const float yy = off + ry;
           ry = ry + pitch;
           if (r >= count) continue;
           const Rgba& col = r == last_row_ ? P[kUiHighlight] : P[kUiNormal];
           const std::string num = format_number(b.rows[size_t(r)].score);
           font.left_fit(c.lines, 332.0f, yy, 8.0f, 8.0f, std::fmaf(-font.measure(num), 8.0f, 252.0f), col.data(),
                         format("%i. %s", r + 1, b.rows[size_t(r)].name.c_str()));
           font.right(c.lines, 600.0f, yy, 8.0f, 8.0f, col.data(), num);
         }
         heading(B - 56.0f, text_(0x68));
         box(B - 36.0f, 36.0f, T.entry[1].score, 12.0f, 6.0f);
       },
       false, {}, 0);
  screen_.accept = [this](const MenuInput&) { after_game(); };
  screen_.tips = [this](TextLayout& l) { tips_continue(l); };
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

void Frontend::board_view(int mode, std::function<void(const MenuInput&)> back) {
  board_mode_ = std::clamp(mode, 0, 5);
  board_first_ = 0;
  open(0, back,
       [this](UiCanvas& c) {
         const Font& font = g_.font;
         const UiPalette& P = palette_;
         ui_bar(c, P, 18.0f, 40.0f);
         ui_panel(c, 0.0f, 46.0f, 22.0f, cursor_, P[kUiBackground], 2);
         ui_panel(c, 618.0f, 46.0f, 640.0f, cursor_, P[kUiBackground], 4);
         static const int kBoardName[6] = {0x3D, 0x3B, 0x3C, 0x3F, 0x3E, 0x40};  // by board: Deadline, Evolved, ...
         static const int kBoardOfMode[6] = {0, 1, 2, 4, 3, 5};
         font.centre_fit(c.lines, 320.0f, 24.0f, 15.0f, 10.0f, 540.0f, P[kUiNormal].data(),
                         format("%s - %s", text_(kBoardName[kBoardOfMode[board_mode_]]).c_str(), text_(0x2B).c_str()));
         const LocalBoard& b = boards_[size_t(board_mode_)];
         const float rh = ((cursor_ - 46.0f) + 6.0f) * 0.1f;
         for (int i = 0; i < 10; ++i) {
           const float y = std::fmaf(float(i), rh, 46.0f);
           ui_panel(c, 28.0f, y, 606.0f, (y + rh) - 6.0f, P[kUiBackground], 0);
           const int r = board_first_ + i;
           if (r >= int(b.rows.size())) continue;
           const Rgba& col = P[kUiNormal];
           font.centre(c.lines, 28.0f + 80.0f, y + 2.0f, 8.0f, 8.0f, col.data(), format("%s.", format_number(r + 1).c_str()));
           font.left_fit(c.lines, 28.0f + 130.0f, y + 2.0f, 10.0f, 10.0f, 300.0f, col.data(), b.rows[size_t(r)].name);
           font.right(c.lines, 28.0f + 570.0f, y + 2.0f, 10.0f, 10.0f, col.data(), format_number(b.rows[size_t(r)].score));
         }
       },
       false,
       [this](const MenuInput& in) {
         if (in.pressed & kBack) {
           if (screen_.back) screen_.back(in);
           return;
         }
         const int count = int(boards_[size_t(board_mode_)].rows.size());
         const int max_first = std::max(count - 10, 0);
         if (in.pressed & kUp) board_first_ = std::clamp(board_first_ - 1, 0, max_first);
         if (in.pressed & kDown) board_first_ = std::clamp(board_first_ + 1, 0, max_first);
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
