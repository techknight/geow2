#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "app/game.h"
#include "game/profile.h"
#include "game/ui.h"
#include "game/ui_text.h"

namespace geow2 {

// One pad as the game reads it (sub_82037EA0's axes): 0/1 left stick (y
// up negative), 2/3 right stick, 4 D-pad x, 5 D-pad down - up, 8/9
// triggers, 10/11 bumpers, 0xC A, 0xD B, 0xE X, 0xF Y, 0x10 START, 0x11
// BACK (buttons 0 or 1).
struct PadState {
  bool connected = false;
  std::array<float, 18> axis{};
};

// The menu input of a frame (sub_82033E18's context): the pad holding A or
// START, held bits, pressed bits (axis-locked), raw pressed bits and the
// analog sums of axes 1, 3, 8, 9.
struct MenuInput {
  int pad = -1;
  uint32_t held = 0, pressed = 0, raw = 0;
  float analog[4] = {0, 0, 0, 0};
};

enum MenuBit : uint32_t {
  kStart = 0x1,
  kA = 0x2,
  kUp = 0x4,
  kDown = 0x8,
  kLeft = 0x10,
  kRight = 0x20,
  kBack = 0x40,  // B or BACK
  kLb = 0x80,
  kRb = 0x100,
  kLt = 0x200,
  kRt = 0x400,
  kX = 0x800,
  kY = 0x1000,
};

// The local score boards that stand in for Xbox LIVE's per-mode
// leaderboards (DIFFERENCES.md): the best scores played on this machine.
struct LocalBoard {
  struct Row {
    int64_t score = 0;
    std::string name;
  };
  std::vector<Row> rows;  // best first
  // A finished game's score; returns its row (or -1 when it does not
  // place).
  int add(int64_t score, const std::string& name);
  static constexpr size_t kRows = 100;
};

// The front end: the menu screens (screen record sub_8208FFF0, items
// sub_820900A8, the generic handler sub_82090180 and the UI layer draw
// sub_820924A0) and the game's flow (sub_82033E18's dispatch: the
// playing, pause and ending states).
class Frontend {
 public:
  struct Hooks {
    std::function<void()> save_profile;  // the profile block changed
    std::function<void()> save_boards;   // the local boards changed
    std::function<void()> quit;          // "Return to arcade"
  };
  Frontend(Game& g, const UiText& text, Profile& profile, std::array<LocalBoard, 6>& boards, Hooks hooks);

  // sub_820334A8: the boot screen (the intro is drawn with textures not
  // yet ported, so the main menu comes at once).
  void boot();
  // One frame: the menu input and the state's handler (sub_82033E18).
  void frame(const std::array<PadState, 4>& pads);
  // The UI layer (sub_820924A0) into `c`.
  void draw(UiCanvas& c) const;
  // The player's name (gamertags stand-in: text 0x82 "Player" and 1).
  std::string player_name() const;

 private:
  using Action = std::function<void(const MenuInput&, int index, int text)>;
  using Label = std::function<void(TextLayout&)>;
  using Enabled = std::function<bool(int index, int text)>;
  struct Item {
    int text = 0;
    int description = 0;
    Action action;
    Label label;
    Enabled enabled;
    bool adjust = false;
    Label describe;
  };
  // The screen record (0x821EBA20..): its callbacks and items.
  struct Screen {
    int id = 0;                                    // BA80
    std::function<void(const MenuInput&)> back;    // BA48
    std::function<void(const MenuInput&)> accept;  // BA4C
    std::function<void(UiCanvas&)> draw;           // BA50
    std::function<void(const MenuInput&)> update;  // BA54
    int title = 0;                                 // BA38
    Label tips;                                    // BA44
    std::function<void(UiCanvas&, int index, int text, bool selected, bool disabled)> item_draw;  // BA40
    float width = 0.0f;                            // BA30
    bool descriptions = false;                     // BA2E
    bool busy = false;                             // BA2D
    bool hide_strip = false;                       // BA2C
    std::vector<Item> items;                       // BA84
  };

  // sub_8208FFF0.
  void open(int id, std::function<void(const MenuInput&)> back, std::function<void(UiCanvas&)> draw, bool reset,
            std::function<void(const MenuInput&)> update, int title);
  void add_item(int text, Action action, Label label = {}, Enabled enabled = {}, bool adjust = false);
  int& selection();  // BA58[BA80]
  int selection_value() const;
  // sub_82090180.
  void default_handler(const MenuInput& in);
  void step_menu(const MenuInput& in);  // sub_82090CD8
  bool item_enabled(int i) const;
  void reset_pulse() { pulse_ = 0.0f; }  // sub_8208FFD8

  // The flow.
  void request_game(int mode);    // sub_8208D410 → sub_820334F8
  void playing(const MenuInput& in);   // sub_820336D8
  void end_game(int reason);      // sub_820335B0
  void finish();                  // sub_820338D8

  // Screens.
  void main_menu(bool reset);              // sub_8208FD00
  void quit_confirm();                     // sub_8208FEB0
  void mode_select(bool reset);            // sub_8208ED00
  void mode_select_update(const MenuInput& in);  // sub_8208D918
  void mode_tile(UiCanvas& c, int index, int text, bool selected) const;  // sub_8208DD58
  void rules(int index, std::function<void(const MenuInput&)> back, int item_text, Action start);  // sub_8207BF88
  void pause(bool reset);                  // sub_8208F468
  void exit_confirm();                     // sub_8208F380
  void hud_screen();                       // the in-game screen: the HUD only
  void help(bool reset);                   // sub_82087560
  void controls();                         // sub_82087160
  void settings();                         // sub_82087458
  void credits();                          // sub_8207A4E0
  void how_to_play(bool reset);            // sub_8207C178
  void results();                          // sub_82089878
  void leaderboards(bool reset);           // local boards (Xbox LIVE's in the original)
  void board_view(int mode, std::function<void(const MenuInput&)> back);
  void after_game();                       // sub_8208EF90

  // Drawing helpers.
  void draw_title_block(UiCanvas& c) const;  // sub_8208F718 / sub_8208F9B8
  // sub_82091630: the pad picture with the mode's controls labelled.
  void controller_diagram(UiCanvas& c, float x, float y, float size, bool move, bool fire, bool bomb) const;
  // sub_82061FC0: a model part flat in UI units at (x, y), scaled.
  void flat_icon(UiCanvas& c, float x, float y, float sx, float sy, const ModelGeometry& m, const Rgba& colour) const;
  // sub_8207A798: a rules-screen icon, drawn with the icon camera.
  void rules_icon(UiCanvas& c, float x, float y, float rot, float scale, Vec2 offset, const ModelGeometry& m, Vec4 up,
                  const Rgba& colour) const;
  void intro(const MenuInput& in);           // sub_8207E9F0
  void draw_intro(UiCanvas& c) const;        // sub_8207E628
  void draw_header(UiCanvas& c, int text) const;  // the bar at 18..40 and its title
  void tips_menu(TextLayout& l) const;      // sub_8208F2F8
  void tips_back(TextLayout& l) const;      // sub_8208F258
  void tips_adjust(TextLayout& l) const;    // sub_82086740
  void tips_continue(TextLayout& l) const;  // sub_8208F6B8
  void adjust_label(TextLayout& l, int name, const std::string& value, bool left, bool right) const;  // sub_82090A98

  Game& g_;
  const UiText& text_;
  Profile& profile_;
  std::array<LocalBoard, 6>& boards_;
  Hooks hooks_;
  UiPalette palette_ = UiPalette::defaults();

  Screen screen_;
  std::array<int, 16> selections_{};  // BA58
  float clock_ = 0.0f;   // BA28
  float pulse_ = 0.0f;   // BA34: the selection pulse phase
  float fade_ = 0.0f;    // dat_821993D0
  mutable float cursor_ = 0.0f;  // BA3C: the top of the menu block, as last drawn
  uint32_t prev_held_ = 0;  // dat_821993E8
  int lock_ud_ = 0, lock_lr_ = 0;  // dat_821993E0 / dat_821993E4
  int menu_pad_ = -1;     // dat_821993DC: the pad the menus follow (-1: any)
  int game_pad_ = 0;      // dat_821993EC
  bool dirty_ = false;    // dat_821EA094: settings changed
  float tile_phase_ = 0.0f;  // dat_821EB62C
  float credits_scroll_ = 0.0f, credits_length_ = 0.0f;
  int last_row_ = -1;     // the last game's row on its local board
  int board_mode_ = 0;
  int board_first_ = 0;
  float board_scroll_ = 0.0f;  // dat_821EA114
  int board_hold_ = 0;         // dat_821EA118
  // The title art's pulse phases (BA24 logo, BA20 star), advanced once per
  // frame the title block is drawn.
  float logo_phase_ = 0.0f, star_phase_ = 0.0f;
  mutable bool title_drawn_ = false;
  // The intro (dat_821D39DD phase, E8 time, EC fade-in, E4 scale, E0 speed).
  int intro_phase_ = 0;
  float intro_time_ = 0.0f, intro_fade_ = 0.0f, intro_scale_ = 1.0f, intro_speed_ = 0.0f;
};

}  // namespace geow2
