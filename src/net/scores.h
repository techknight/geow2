#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "game/score_log.h"

namespace geow2 {

// The score server's API (geow2-scores, github.com/techknight/geow2-scores):
// pairing a device to a player, and syncing its games and progress. Each
// call blocks; the game makes them from its online thread (app/online.h).

// "scores.example.com" or "https://scores.example.com/" as the base URL
// "https://scores.example.com" (http only for a server on this machine).
// Empty when it can't be one.
std::string server_base(const std::string& typed);

struct PairStart {
  std::string code;  // "ABCD-EFGH", shown in the game
  std::string url;   // the page that confirms it (the QR code)
  std::string poll;  // the game's secret for asking whether it is confirmed
  std::string error;
};

struct PairPoll {
  enum Status { kWaiting, kPaired, kExpired, kFailed } status = kFailed;
  std::string token;  // the device's token, when paired
  std::string player_id, player_name;
  std::string error;
};

// The progress the server merges: the profile block's unlocked modes,
// million bits and unlock progress per mode index, and the achievements.
struct OnlineProgress {
  uint32_t unlocked = 2;
  uint32_t millions = 0;
  std::array<int32_t, 6> progress{};
  uint32_t achievements = 0;
};

struct GroupRow {
  std::string player, name;
  int64_t score = 0;
  int multiplier = 1;
  int64_t time = 0;
};

struct SyncReply {
  int status = 0;           // HTTP status; 0 when there was no answer
  std::string error;        // empty on success
  std::string group;
  std::string player_id, player_name;
  int64_t taken = -1;              // how many of the games sent the server dealt with (-1: a server that doesn't say)
  std::vector<ScoreEvent> events;  // the player's games from the server since the cursor sent
  int64_t cursor = 0;
  bool more = false;               // more events wait: sync again with the new cursor
  OnlineProgress progress;
  std::array<std::vector<GroupRow>, 6> boards;  // per mode: each player's best, best first
};

// The request and reply bodies (separate from the transport for tests).
std::string sync_body(const std::vector<ScoreEvent>& events, const OnlineProgress& progress, int64_t since);
SyncReply parse_sync(int status, const std::string& body);
// `base`: the server asked; the link must be on it (else its /pair/ page).
PairStart parse_pair_start(int status, const std::string& body, const std::string& base);
PairPoll parse_pair_poll(int status, const std::string& body);

PairStart pair_start(const std::string& base, const std::string& device_name);
PairPoll pair_poll(const std::string& base, const std::string& poll);
SyncReply sync(const std::string& base, const std::string& token, const std::vector<ScoreEvent>& events,
               const OnlineProgress& progress, int64_t since);

}  // namespace geow2
