#pragma once

#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "net/scores.h"

namespace geow2 {

// Online scores (a port addition, DIFFERENCES.md): this device's pairing
// with a group's score server and the background sync of its games and
// progress. The network calls run on a thread of their own; their results
// are applied on the game's thread by update(), between frames.
class Online {
 public:
  // online.txt in the profile folder: the server, the device's token, the
  // player and the sync's position.
  struct State {
    std::string server, token, player_id, player_name, group;
    int64_t cursor = 0;  // the server's events seen
    size_t sent = 0;     // the log's events up to here are on the server
    int64_t last_sync = 0;  // when a sync last succeeded, seconds since 1970 (0: never)
  };

  enum class Pairing { kIdle, kStarting, kWaiting, kPaired, kFailed };
  struct PairingView {
    Pairing state = Pairing::kIdle;
    std::string code, url, error;
  };

  // What a finished sync changed, for the caller to save and redraw.
  struct Applied {
    size_t new_games = 0;
    bool progress = false;
    uint32_t new_achievements = 0;
  };

  explicit Online(std::filesystem::path profile_dir);
  // Leaves a request in flight to finish on its own (quitting never
  // waits for the network).
  ~Online();
  Online(const Online&) = delete;
  Online& operator=(const Online&) = delete;

  const State& state() const { return state_; }
  bool paired() const { return !state_.token.empty(); }

  // Starts a sync of what the log and the progress hold now, unless one
  // is running or the device is not paired.
  void sync(const ScoreLog& log, const OnlineProgress& progress);
  // The game's thread, each frame: a finished sync's games, progress and
  // achievements into `log`, `progress` and `achievements`; another sync
  // follows when the server has more. Empty when nothing finished.
  std::optional<Applied> update(ScoreLog& log, OnlineProgress& progress, uint32_t& achievements);
  // Whether a sync is running.
  bool syncing() const;

  // Pairing: a code to confirm on a signed-in phone, polled until done.
  void start_pairing(const std::string& server, const std::string& device_name);
  void cancel_pairing();
  PairingView pairing() const;
  // Forgets the server and the token (the server keeps the games).
  void unpair();
  // The server to pair with (a base from server_base()), kept until it is
  // changed.
  void set_server(const std::string& base);

  // For the Online scores screen: "syncing...", "last synced at
  // 2026-10-19 23:25" (local time), "sync failed: <why>" or "not synced
  // yet".
  std::string status() const;
  // Why the last sync failed, or "" when it succeeded.
  std::string error() const;
  // The group's boards from the last sync: per mode, each player's best.
  std::array<std::vector<GroupRow>, 6> group_boards() const;

  // This machine's name for the server's device list.
  static std::string device_name();

 private:
  struct Core;  // shared with the network thread
  void save() const;

  std::filesystem::path dir_;
  State state_;
  std::shared_ptr<Core> core_;
};

}  // namespace geow2
