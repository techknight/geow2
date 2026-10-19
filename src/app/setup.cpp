// The setup window. It is the port's own (the original has no such step):
// it draws with SDL's 2D renderer and its built-in 8x8 font, since the
// game's font and textures are in the data it is about to make, and it
// takes a gamepad, the keyboard, the mouse or touch, so that it works in a
// Steam Deck's Game Mode as well as on a desktop. The file chooser is the
// system's (in a Flatpak, the portal's).

#include "app/setup.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "app/png.h"
#include "extract/audio.h"
#include "extract/unpack.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace geow2 {
namespace {

namespace fs = std::filesystem;

// The layout is drawn on a fixed 1280x720 canvas, letterboxed to the window.
constexpr float kWidth = 1280.0f, kHeight = 720.0f, kMargin = 80.0f;
constexpr float kGlyph = float(SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE);

// The website's colours (the icon's grid blue and magenta on near-black).
constexpr SDL_Color kBackground{3, 4, 11, 255}, kGrid{16, 24, 58, 255}, kInk{228, 233, 255, 255},
    kDim{152, 161, 204, 255}, kGlow{255, 40, 200, 255}, kBlue{40, 100, 255, 255}, kWarn{255, 210, 63, 255};

enum class Stage { Intro, Choosing, Working, Failed };
enum class Action { ChooseFile, ChooseFolder, Quit, Back };

struct Button {
  std::string label;
  Action action;
  SDL_FRect rect{};
};

// Written by the file chooser's callback and the worker thread.
struct Shared {
  std::mutex m;
  bool dialog_done = false;
  std::optional<fs::path> chosen;
  std::atomic<double> progress{0.0};
  std::atomic<bool> cancel{false};
  std::atomic<bool> worker_done{false};
  std::string line;    // the extractor's latest log line
  std::string failed;  // why the folder was not made, empty on success
};

struct Cancelled {};

#ifdef _WIN32
// A command through cmd.exe without a console window (the game is a window
// program: std::system would flash one for each ffmpeg run).
int run_hidden(const std::string& command) {
  std::string line = "cmd.exe /d /s /c \"" + command + "\"";
  STARTUPINFOA si{};
  si.cb = sizeof si;
  PROCESS_INFORMATION pi{};
  if (!CreateProcessA(nullptr, line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
    return -1;
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return int(code);
}
#endif

// The C runtime's environment, which the extractor reads with std::getenv.
void set_env(const char* name, const std::string& value) {
#ifdef _WIN32
  _putenv_s(name, value.c_str());
#else
  if (value.empty())
    unsetenv(name);
  else
    setenv(name, value.c_str(), 1);
#endif
}

ExtractHooks base_hooks() {
  ExtractHooks h;
#ifdef _WIN32
  h.run = run_hidden;
#endif
  return h;
}

// FFmpeg: GEOW2_FFMPEG or PATH, else one next to the program, else where
// Homebrew puts it (a Mac app started from the Finder gets a PATH without
// it). Sets GEOW2_FFMPEG to the one found.
bool find_ffmpeg() {
  const ExtractHooks hooks = base_hooks();
  if (ffmpeg_available(hooks)) return true;
  const fs::path base = fs::path(SDL_GetBasePath() ? SDL_GetBasePath() : "");
#ifdef _WIN32
  const fs::path candidates[] = {base / "ffmpeg.exe"};
#else
  const fs::path candidates[] = {base / "ffmpeg", "/opt/homebrew/bin/ffmpeg", "/usr/local/bin/ffmpeg"};
#endif
  for (const fs::path& c : candidates) {
    std::error_code ec;
    if (!fs::is_regular_file(c, ec)) continue;
    set_env("GEOW2_FFMPEG", c.string());
    if (ffmpeg_available(hooks)) return true;
  }
  set_env("GEOW2_FFMPEG", "");
  return false;
}

void on_dialog(void* userdata, const char* const* files, int) {
  auto* s = static_cast<Shared*>(userdata);
  std::lock_guard lock(s->m);
  s->dialog_done = true;
  if (files && files[0]) s->chosen = fs::path(std::u8string(reinterpret_cast<const char8_t*>(files[0])));
}

// Makes the folder beside `data_dir` and moves it into place only once it is
// complete, so a failed or cancelled run never leaves half a data folder.
void make_data(Shared& s, fs::path source, fs::path data_dir) {
  const fs::path partial = data_dir.parent_path() / (data_dir.filename().string() + ".partial");
  std::error_code ec;
  fs::remove_all(partial, ec);
  ExtractHooks hooks = base_hooks();
  hooks.log = [&s](const std::string& line) {
    std::lock_guard lock(s.m);
    s.line = line;
  };
  hooks.progress = [&s](double f) {
    if (s.cancel) throw Cancelled{};
    s.progress = f;
  };
  std::string failed;
  try {
    if (const int bad = unpack(source, partial, hooks)) {
      failed = "This is not the supported release: " + std::to_string(bad) +
               " file(s) are missing or different. geow2 works only with Geometry Wars: Retro Evolved 2 for "
               "Xbox Live Arcade, title ID 584108FF, version 0x102.";
    } else {
      if (fs::exists(data_dir)) {
        const fs::path previous = data_dir.parent_path() / (data_dir.filename().string() + ".previous");
        fs::remove_all(previous, ec);
        fs::rename(data_dir, previous);
      }
      fs::rename(partial, data_dir);
    }
  } catch (const Cancelled&) {
    failed = "Cancelled.";
  } catch (const std::exception& e) {
    failed = std::string("The data folder could not be made: ") + e.what();
  }
  if (!failed.empty()) fs::remove_all(partial, ec);
  std::lock_guard lock(s.m);
  s.failed = failed;
  s.worker_done = true;
}

// Words of `text` in lines of at most `chars` characters.
std::vector<std::string> wrap(const std::string& text, size_t chars) {
  std::vector<std::string> lines;
  std::istringstream in(text);
  std::string word, line;
  while (in >> word) {
    while (word.size() > chars) {  // a long path: broken anywhere
      if (!line.empty()) lines.push_back(std::exchange(line, {}));
      lines.push_back(word.substr(0, chars));
      word.erase(0, chars);
    }
    if (!line.empty() && line.size() + 1 + word.size() > chars) lines.push_back(std::exchange(line, {}));
    line += (line.empty() ? "" : " ") + word;
  }
  if (!line.empty()) lines.push_back(line);
  return lines;
}

class SetupWindow {
 public:
  SetupWindow(SDL_Window* w, SDL_Renderer* r) : window_(w), r_(r) {}

  void colour(SDL_Color c) { SDL_SetRenderDrawColor(r_, c.r, c.g, c.b, c.a); }

  // Text at `scale` times the 8x8 font; returns the y below it.
  float text(float x, float y, float scale, const std::string& s, SDL_Color c, float width = kWidth - 2 * kMargin) {
    colour(c);
    SDL_SetRenderScale(r_, scale, scale);
    for (const std::string& line : wrap(s, size_t(width / (kGlyph * scale)))) {
      SDL_RenderDebugText(r_, x / scale, y / scale, line.c_str());
      y += kGlyph * scale * 1.6f;
    }
    SDL_SetRenderScale(r_, 1.0f, 1.0f);
    return y;
  }

  void background() {
    colour(kBackground);
    SDL_RenderClear(r_);
    colour(kGrid);
    for (float x = 0; x <= kWidth; x += 40) SDL_RenderLine(r_, x, 0, x, kHeight);
    for (float y = 0; y <= kHeight; y += 40) SDL_RenderLine(r_, 0, y, kWidth, y);
  }

  void buttons(std::vector<Button>& list, size_t focus, float y) {
    for (size_t i = 0; i < list.size(); ++i) {
      Button& b = list[i];
      b.rect = {kMargin, y, 560.0f, 52.0f};
      const bool on = i == focus;
      colour(on ? SDL_Color{60, 10, 50, 255} : kBackground);
      SDL_RenderFillRect(r_, &b.rect);
      colour(on ? kGlow : kBlue);
      SDL_RenderRect(r_, &b.rect);
      if (on) {
        const SDL_FRect inner{b.rect.x + 2, b.rect.y + 2, b.rect.w - 4, b.rect.h - 4};
        SDL_RenderRect(r_, &inner);
      }
      text(b.rect.x + 20, b.rect.y + 18, 2.0f, b.label, on ? SDL_Color{255, 255, 255, 255} : kInk);
      y += 64.0f;
    }
  }

  void bar(float y, double fraction) {
    const SDL_FRect frame{kMargin, y, kWidth - 2 * kMargin, 28.0f};
    colour(kBlue);
    SDL_RenderRect(r_, &frame);
    const SDL_FRect fill{frame.x + 4, frame.y + 4, float((frame.w - 8) * std::clamp(fraction, 0.0, 1.0)), frame.h - 8};
    colour(kGlow);
    SDL_RenderFillRect(r_, &fill);
  }

  void present() { SDL_RenderPresent(r_); }
  SDL_Window* window() const { return window_; }
  SDL_Renderer* renderer() const { return r_; }

 private:
  SDL_Window* window_;
  SDL_Renderer* r_;
};

}  // namespace

bool run_setup(const fs::path& data_dir, const SetupDev& dev) {
  SDL_Window* window = SDL_CreateWindow("geow2", 1280, 720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, nullptr) : nullptr;
  if (!renderer) {
    SDL_Log("setup: cannot open its window: %s", SDL_GetError());
    if (window) SDL_DestroyWindow(window);
    return false;
  }
  SDL_SetRenderLogicalPresentation(renderer, int(kWidth), int(kHeight), SDL_LOGICAL_PRESENTATION_LETTERBOX);
  SDL_SetRenderVSync(renderer, 1);
  SetupWindow ui(window, renderer);
  std::vector<SDL_Gamepad*> pads;

  const bool ffmpeg = find_ffmpeg();
  // On the heap: a file chooser still open when the player quits may answer
  // later, and its callback must still find this.
  auto* shared_ptr = new Shared;
  Shared& shared = *shared_ptr;
  std::thread worker;
  Stage stage = Stage::Intro;
  std::string failure;
  size_t focus = 0;
  bool done = false, made = false, stick_held = false;

  auto intro_buttons = [] {
    return std::vector<Button>{{"Choose the package file", Action::ChooseFile},
                               {"Choose an extracted folder", Action::ChooseFolder},
                               {"Quit", Action::Quit}};
  };
  std::vector<Button> buttons = intro_buttons();
  if (!dev.source.empty()) {
    std::lock_guard lock(shared.m);
    shared.dialog_done = true;
    shared.chosen = dev.source;
    stage = Stage::Choosing;
    buttons.clear();
  }

  auto act = [&](Action a) {
    switch (a) {
      case Action::ChooseFile:
      case Action::ChooseFolder: {
        {
          std::lock_guard lock(shared.m);
          shared.dialog_done = false;
          shared.chosen.reset();
        }
        stage = Stage::Choosing;
        buttons.clear();
        if (a == Action::ChooseFile)
          SDL_ShowOpenFileDialog(on_dialog, &shared, window, nullptr, 0, nullptr, false);
        else
          SDL_ShowOpenFolderDialog(on_dialog, &shared, window, nullptr, false);
        break;
      }
      case Action::Back:
        stage = Stage::Intro;
        buttons = intro_buttons();
        focus = 0;
        break;
      case Action::Quit:
        done = true;
        break;
    }
  };
  auto activate = [&] {
    if (focus < buttons.size()) act(buttons[focus].action);
  };
  auto move = [&](int d) {
    const size_t n = buttons.size();
    if (n) focus = (focus + (d < 0 ? n - 1 : 1)) % n;
  };
  auto quit_requested = [&] {
    if (stage == Stage::Working)
      shared.cancel = true;  // the worker stops at its next step
    else
      done = true;
  };

  while (!done) {
    SDL_Event e;
    if (SDL_WaitEventTimeout(&e, 16)) {
      do {
        SDL_ConvertEventToRenderCoordinates(renderer, &e);
        switch (e.type) {
          case SDL_EVENT_QUIT:
          case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            quit_requested();
            break;
          case SDL_EVENT_GAMEPAD_ADDED:
            if (SDL_Gamepad* g = SDL_OpenGamepad(e.gdevice.which)) pads.push_back(g);
            break;
          case SDL_EVENT_KEY_DOWN:
            if (e.key.key == SDLK_UP || (e.key.key == SDLK_TAB && (e.key.mod & SDL_KMOD_SHIFT))) move(-1);
            else if (e.key.key == SDLK_DOWN || e.key.key == SDLK_TAB) move(1);
            else if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER || e.key.key == SDLK_SPACE) activate();
            else if (e.key.key == SDLK_ESCAPE && stage == Stage::Failed) act(Action::Back);
            else if (e.key.key == SDLK_ESCAPE) quit_requested();
            break;
          case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            if (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_UP) move(-1);
            else if (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) move(1);
            else if (e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH || e.gbutton.button == SDL_GAMEPAD_BUTTON_START) activate();
            else if (e.gbutton.button == SDL_GAMEPAD_BUTTON_EAST && stage == Stage::Failed) act(Action::Back);
            break;
          case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) {
              const bool held = std::abs(int(e.gaxis.value)) > 20000;
              if (held && !stick_held) move(e.gaxis.value < 0 ? -1 : 1);
              stick_held = held;
            }
            break;
          case SDL_EVENT_MOUSE_MOTION:
            for (size_t i = 0; i < buttons.size(); ++i) {
              const SDL_FPoint p{e.motion.x, e.motion.y};
              if (SDL_PointInRectFloat(&p, &buttons[i].rect)) focus = i;
            }
            break;
          case SDL_EVENT_MOUSE_BUTTON_UP:
          case SDL_EVENT_FINGER_UP: {
            const SDL_FPoint p = e.type == SDL_EVENT_FINGER_UP ? SDL_FPoint{e.tfinger.x, e.tfinger.y}
                                                                  : SDL_FPoint{e.button.x, e.button.y};
            for (size_t i = 0; i < buttons.size(); ++i)
              if (SDL_PointInRectFloat(&p, &buttons[i].rect)) {
                focus = i;
                activate();
                break;
              }
            break;
          }
          default:
            break;
        }
      } while (!done && SDL_PollEvent(&e));
    }
    if (done) break;

    // The file chooser answered: start the extractor, or go back.
    if (stage == Stage::Choosing) {
      std::optional<fs::path> chosen;
      bool answered;
      {
        std::lock_guard lock(shared.m);
        answered = shared.dialog_done;
        chosen = shared.chosen;
      }
      if (answered) {
        if (chosen) {
          stage = Stage::Working;
          shared.progress = 0.0;
          shared.worker_done = false;
          shared.cancel = false;
          worker = std::thread(make_data, std::ref(shared), *chosen, data_dir);
        } else {
          act(Action::Back);
        }
      }
    }
    if (stage == Stage::Working && shared.worker_done) {
      worker.join();
      std::lock_guard lock(shared.m);
      if (shared.failed.empty()) {
        made = done = true;
      } else if (shared.cancel) {
        done = true;
      } else {
        failure = shared.failed;
        stage = Stage::Failed;
        buttons = {{"Choose again", Action::Back}, {"Quit", Action::Quit}};
        focus = 0;
      }
    }
    if (done) break;

    ui.background();
    float y = 56.0f;
    y = ui.text(kMargin, y, 6.0f, "geow2", kGlow);
    y = ui.text(kMargin, y - 8.0f, 2.0f, "Setup", kDim) + 16.0f;
    switch (stage) {
      case Stage::Intro:
        y = ui.text(kMargin, y, 2.0f,
                    "geow2 needs the data from your own copy of Geometry Wars: Retro Evolved 2 for Xbox 360 "
                    "(Xbox Live Arcade, title ID 584108FF).", kInk) + 16.0f;
        y = ui.text(kMargin, y, 2.0f,
                    "Choose the package file (the file in 584108FF/000D0000/) or a folder holding the files "
                    "extracted from it. geow2 checks every file and makes its data folder from it, once.", kInk) + 16.0f;
        if (!ffmpeg)
          y = ui.text(kMargin, y, 2.0f,
                      "FFmpeg was not found: without it the music and most sounds will be missing. Install FFmpeg "
                      "(ffmpeg.org) and start geow2 again, or continue without them.", kWarn) + 16.0f;
        y = ui.text(kMargin, y, 1.5f, "The data folder will be " + data_dir.string(), kDim);
        ui.buttons(buttons, focus, std::max(y + 24.0f, 440.0f));
        break;
      case Stage::Choosing:
        ui.text(kMargin, y, 2.0f, "Waiting for the file chooser...", kInk);
        break;
      case Stage::Working: {
        y = ui.text(kMargin, y, 2.0f, shared.cancel ? "Stopping..." : "Making the data folder...", kInk) + 8.0f;
        y = ui.text(kMargin, y, 1.5f, data_dir.string(), kDim) + 16.0f;
        ui.bar(y, shared.progress);
        std::string line;
        {
          std::lock_guard lock(shared.m);
          line = shared.line;
        }
        ui.text(kMargin, y + 48.0f, 1.5f, line, kDim);
        break;
      }
      case Stage::Failed:
        y = ui.text(kMargin, y, 2.0f, failure, kWarn) + 16.0f;
        ui.buttons(buttons, focus, std::max(y + 16.0f, 440.0f));
        break;
    }
    if (!dev.preview.empty()) {
      if (SDL_Surface* shot = SDL_RenderReadPixels(renderer, nullptr)) {
        if (SDL_Surface* rgba = SDL_ConvertSurface(shot, SDL_PIXELFORMAT_RGBA32)) {
          write_png(dev.preview, uint32_t(rgba->w), uint32_t(rgba->h),
                    {static_cast<const uint8_t*>(rgba->pixels), size_t(rgba->pitch) * size_t(rgba->h)});
          SDL_DestroySurface(rgba);
        }
        SDL_DestroySurface(shot);
      }
      done = true;
      break;
    }
    ui.present();
  }

  if (worker.joinable()) {
    shared.cancel = true;
    worker.join();
  }
  bool dialog_open;
  {
    std::lock_guard lock(shared.m);
    dialog_open = stage == Stage::Choosing && !shared.dialog_done;
  }
  if (!dialog_open) delete shared_ptr;  // else left for the chooser's answer (the game is quitting)
  for (SDL_Gamepad* g : pads) SDL_CloseGamepad(g);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  return made;
}

}  // namespace geow2
