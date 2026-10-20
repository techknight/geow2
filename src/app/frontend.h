#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "app/game.h"
#include "audio/events.h"
#include "game/profile.h"
#include "game/ui.h"
#include "game/ui_text.h"
#include "net/scores.h"

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

// The online scores (app/online.h) as the menus see them. A port addition
// with no counterpart in the original (DIFFERENCES.md "Online scores").
class OnlineMenu {
 public:
  struct Pairing {
    enum State { kIdle, kStarting, kWaiting, kPaired, kFailed } state = kIdle;
    std::string code, url, error;
  };
  virtual ~OnlineMenu() = default;
  virtual bool paired() const = 0;
  virtual std::string server() const = 0;  // the address as set, "" when none
  virtual std::string player() const = 0;  // the player's name on the server (UTF-8)
  virtual std::string group() const = 0;
  virtual std::string status() const = 0;  // the last sync's outcome
  // Returns why the address can't be used, or "" when it was taken.
  virtual std::string set_server(const std::string& typed) = 0;
  virtual void start_pairing() = 0;
  virtual Pairing pairing() const = 0;
  virtual void cancel_pairing() = 0;
  virtual void unpair() = 0;
  virtual void sync_now() = 0;
  virtual std::string player_id() const = 0;
  // The group's boards from the last sync: per mode, each player's best.
  virtual std::array<std::vector<GroupRow>, 6> boards() const = 0;
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
    // Every finished single-player game, placed on the board or not (the
    // score log behind the boards and the online scores).
    std::function<void(int mode, int64_t score, int multiplier, const std::string& name)> scored;
    std::function<void()> quit;          // "Return to arcade"
  };
  Frontend(Game& g, const UiText& text, Profile& profile, std::array<LocalBoard, 6>& boards, Hooks hooks);

  // sub_820334A8: the boot screen (the intro, then the main menu).
  void boot();
  // The in-game screen (the HUD) for a game started without the menus
  // (the development captures).
  void watch_game() {
    hud_screen();
    fade_ = 1.0f;
  }
  // One frame: the menu input and the state's handler (sub_82033E18).
  void frame(const std::array<PadState, 4>& pads);
  // sub_8207E228: the system UI (the guide) is showing, which pauses a
  // game. The port's stand-in is a window that is minimised, hidden or
  // without the focus.
  void set_system_ui(bool shown) { system_ui_ = shown; }
  // The UI layer (sub_820924A0) into `c`.
  void draw(UiCanvas& c) const;
  // The player's name (gamertags stand-in: the name on the score server
  // when paired, else text 0x82 "Player" and 1). UTF-8.
  std::string player_name() const;

  // The online scores, when the game has them (null: no Online scores
  // item).
  void set_online(OnlineMenu* online) { online_ = online; }
  // Text entry (the Online scores screen's server address): while it is
  // on, the keyboard types instead of acting as a pad.
  enum class TextKey { kBackspace, kEnter, kEscape };
  bool text_entry() const { return editing_; }
  void type(const std::string& utf8);
  void text_key(TextKey key);
  // For captures (--online-preview): the Online scores screen at once,
  // typing an address ("editing") or pairing ("waiting", "failed").
  void preview_online(const std::string& state);
  // The text the pairing screen shows as a QR code (texture kTexQr, which
  // the caller makes from it: qr_image()); empty when none is shown.
  const std::string& qr_text() const { return qr_text_; }

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
    bool goes_back = false;  // its action is the screen's back callback (sound 0x17)
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
  void sound(audio::Event e) const;        // sub_820787F8

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
  void achievements_screen();              // stands in for XamShowAchievementsUI
  void online_screen(bool reset);          // the port's online scores (no original)
  void pairing_screen();
  void finish_editing(bool keep);
  // The group's board for a mode as the friends board shows it (each
  // player's best, best first), with this profile's best counted for the
  // player even before the server has it; `own` is the player's row, -1
  // without one. Empty when not paired.
  std::vector<GroupRow> group_board(int mode, int& own) const;

  // The multiplayer setup (menu 6, sub_820863B0) and its parts.
  void lobby(bool reset);
  void lobby_open(int pad);                       // sub_82083AE0
  void lobby_update(const MenuInput& in);         // sub_820846E0
  bool lobby_navigate(uint32_t bits);             // sub_82084588
  int lobby_search(int col, int row, int dir) const;  // sub_82083518
  void lobby_roles();                             // sub_82082F10
  void lobby_frame(UiCanvas& c) const;            // sub_82084990
  void lobby_players(UiCanvas& c) const;          // sub_82083C28
  void lobby_tile(UiCanvas& c, int index, int text, bool selected, bool disabled) const;  // sub_82085C28
  void lobby_teams(TextLayout& l, int preset, bool copilots, bool joined_only) const;    // sub_82084FC0
  void lobby_back();                              // sub_820832A0
  int joined() const;                             // the joined pads
  int partner(int pad) const;                     // sub_82082BC8
  int one_team_preset() const;                    // sub_82082CD0
  int team_count() const;                         // sub_82082B00
  // The gamertag stand-in of a pad ("%s %i", text 0x82 and the pad).
  std::string pad_name(int pad) const;
  // The results bodies: co-op (sub_82089218) and versus (sub_82088978).
  void results_coop(UiCanvas& c) const;
  void results_versus(UiCanvas& c) const;

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
  void draw_intermission(UiCanvas& c) const;  // sub_82039AA0
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
  int board_filter_ = 2;  // dat_821EB5AC: 0 Friends, 1 My score, 2 Overall (the local board)
  float board_scroll_ = 0.0f;  // dat_821EA114
  int board_hold_ = 0;         // dat_821EA118
  int achievement_first_ = 0;
  // The online scores.
  OnlineMenu* online_ = nullptr;
  bool editing_ = false;
  std::string edit_;            // the address being typed (UTF-8)
  std::string online_message_;  // why the last address was refused
  bool online_paired_ = false;  // as the Online scores screen was built
  mutable std::string qr_text_; // the QR code being shown (qr_text())
  // The title art's pulse phases (BA24 logo, BA20 star), advanced once per
  // frame the title block is drawn.
  float logo_phase_ = 0.0f, star_phase_ = 0.0f;
  mutable bool title_drawn_ = false;
  // The pads of the last frame, and the lobby's per-pad held bits and
  // their pressed edges (lobby record +0xC / +0x10); the copilot setting
  // (dat_821E9FE8), the team preset (dat_821E9FEC), the copilot swap of
  // teams 0 and 1 (dat_821E9FE5 / FE4) and the join prompt's pulse
  // (dat_821E9FE0).
  std::array<PadState, 4> pads_{};
  bool system_ui_ = false;  // set_system_ui
  std::array<uint32_t, 4> lobby_held_{}, lobby_pressed_{};
  bool copilots_ = false;
  int preset_ = 0;
  std::array<bool, 2> swap_{};
  float lobby_phase_ = 0.0f;
  // The intro (dat_821D39DD phase, E8 time, EC fade-in, E4 scale, E0 speed).
  int intro_phase_ = 0;
  float intro_time_ = 0.0f, intro_fade_ = 0.0f, intro_scale_ = 1.0f, intro_speed_ = 0.0f;
};

}  // namespace geow2
