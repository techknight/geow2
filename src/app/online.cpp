#include "app/online.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <functional>
#include <mutex>
#include <sstream>
#include <thread>

#include "extract/bytes.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace geow2 {

namespace fs = std::filesystem;

// Everything the network thread touches. The thread holds a reference of
// its own, so it can finish a request after the Online is gone.
struct Online::Core {
  std::mutex m;
  std::condition_variable cv;
  std::deque<std::function<void(Core&)>> jobs;
  bool stop = false;
  bool syncing = false;
  size_t sent_from = 0, sent_count = 0;  // the log's games the running sync sent
  std::optional<SyncReply> reply;  // a finished sync, for update()
  std::string error;  // why the last sync failed, "" when it succeeded
  std::array<std::vector<GroupRow>, 6> boards;
  PairingView pairing;
  uint64_t pairing_id = 0;         // a newer pairing (or a cancel) stops the old one's polling
  std::optional<PairPoll> paired;  // a finished pairing, for update()
  std::string pairing_server;

  void post(std::function<void(Core&)> job) {
    {
      std::lock_guard lock(m);
      jobs.push_back(std::move(job));
    }
    cv.notify_all();
  }

  static void run(const std::shared_ptr<Core>& self) {
    Core& c = *self;
    for (;;) {
      std::function<void(Core&)> job;
      {
        std::unique_lock lock(c.m);
        c.cv.wait(lock, [&] { return c.stop || !c.jobs.empty(); });
        if (c.stop) return;
        job = std::move(c.jobs.front());
        c.jobs.pop_front();
      }
      job(c);
    }
  }
};

Online::Online(fs::path profile_dir) : dir_(std::move(profile_dir)), core_(std::make_shared<Core>()) {
  std::ifstream f(dir_ / "online.txt");
  for (std::string line; std::getline(f, line);) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const size_t sp = line.find(' ');
    if (sp == std::string::npos) continue;
    const std::string key = line.substr(0, sp), value = line.substr(sp + 1);
    if (key == "server") state_.server = value;
    else if (key == "token") state_.token = value;
    else if (key == "player_id") state_.player_id = value;
    else if (key == "player_name") state_.player_name = value;
    else if (key == "group") state_.group = value;
    else if (key == "cursor") state_.cursor = std::atoll(value.c_str());
    else if (key == "sent") state_.sent = size_t(std::atoll(value.c_str()));
    else if (key == "last_sync") state_.last_sync = std::atoll(value.c_str());
  }
  // The last sync's boards, when they are this pairing's.
  if (paired()) {
    std::ifstream b(dir_ / "group-boards.txt", std::ios::binary);
    std::ostringstream text;
    text << b.rdbuf();
    core_->boards = parse_group_boards(text.str(), state_.server, state_.player_id);
  }
  std::thread([core = core_] { Core::run(core); }).detach();
}

Online::~Online() {
  {
    std::lock_guard lock(core_->m);
    core_->stop = true;
    ++core_->pairing_id;
  }
  core_->cv.notify_all();
}

void Online::save() const {
  std::ostringstream s;
  s << "server " << state_.server << "\ntoken " << state_.token << "\nplayer_id " << state_.player_id
    << "\nplayer_name " << state_.player_name << "\ngroup " << state_.group << "\ncursor " << state_.cursor << "\nsent "
    << state_.sent << "\nlast_sync " << state_.last_sync << "\n";
  const std::string text = s.str();
  // (It holds the device's token.)
  replace_file(dir_ / "online.txt", {reinterpret_cast<const uint8_t*>(text.data()), text.size()}, true);
}

void Online::save_boards(const std::array<std::vector<GroupRow>, 6>& boards) const {
  const std::string text = serialize_group_boards(boards, state_.server, state_.player_id);
  replace_file(dir_ / "group-boards.txt", {reinterpret_cast<const uint8_t*>(text.data()), text.size()});
}

void Online::forget_boards() {
  {
    std::lock_guard lock(core_->m);
    core_->boards = {};
  }
  std::error_code ec;
  fs::remove(dir_ / "group-boards.txt", ec);
}

namespace {
// Games sent a sync (a long history goes up over several), and the most
// syncs that follow one another at once (while the server has more, or
// games are left to send).
constexpr size_t kBatch = 200;
constexpr int kMaxFollowUps = 100;
}  // namespace

void Online::sync(const ScoreLog& log, const OnlineProgress& progress) {
  if (!paired()) return;
  const auto& all = log.events();
  const size_t from = std::min(state_.sent, all.size());
  const size_t count = std::min(all.size() - from, kBatch);
  {
    std::lock_guard lock(core_->m);
    if (core_->syncing) return;
    core_->syncing = true;
    core_->sent_from = from;
    core_->sent_count = count;
  }
  std::vector<ScoreEvent> unsent(all.begin() + std::ptrdiff_t(from), all.begin() + std::ptrdiff_t(from + count));
  core_->post([server = state_.server, token = state_.token, unsent = std::move(unsent), progress,
               since = state_.cursor](Core& c) {
    SyncReply r = geow2::sync(server, token, unsent, progress, since);
    std::lock_guard lock(c.m);
    c.reply = std::move(r);
  });
}

bool Online::syncing() const {
  std::lock_guard lock(core_->m);
  return core_->syncing;
}

std::optional<Online::Applied> Online::update(ScoreLog& log, OnlineProgress& progress, uint32_t& achievements) {
  std::optional<SyncReply> reply;
  std::optional<PairPoll> paired;
  std::string server;
  size_t from, count;
  {
    std::lock_guard lock(core_->m);
    reply.swap(core_->reply);
    paired.swap(core_->paired);
    server = core_->pairing_server;
    from = core_->sent_from;
    count = core_->sent_count;
    if (reply) {
      core_->syncing = false;
      if (reply->error.empty()) core_->boards = reply->boards;
      core_->error = reply->error;
    }
    if (paired) core_->error.clear();
  }
  if (paired) {
    // A new pairing: a fresh start with this server (everything in the
    // log goes up on the first sync).
    forget_boards();
    state_ = State{server, paired->token, paired->player_id, paired->player_name, {}, 0, 0};
    save();
    sync(log, progress);
    return Applied{};
  }
  if (!reply) return std::nullopt;
  if (!reply->error.empty()) {
    if (reply->status == 401) {
      // Unpaired on the server: the token is forgotten.
      state_.token.clear();
      save();
      forget_boards();
    }
    return std::nullopt;
  }
  Applied a;
  // The games the server dealt with (all those sent, from a server that
  // doesn't say); the rest go next time.
  const size_t taken = reply->taken < 0 ? count : size_t(std::min<int64_t>(reply->taken, int64_t(count)));
  const size_t before_merge = log.events().size();
  a.new_games = log.merge(reply->events);
  state_.sent = from + taken;
  // With every game of this log on the server, the ones just merged came
  // from it: they aren't sent back.
  if (state_.sent == before_merge) state_.sent = log.events().size();
  const OnlineProgress before = progress;
  progress.unlocked |= reply->progress.unlocked;
  progress.millions |= reply->progress.millions;
  for (size_t i = 0; i < 6; ++i) progress.progress[i] = std::max(progress.progress[i], reply->progress.progress[i]);
  a.progress = progress.unlocked != before.unlocked || progress.millions != before.millions ||
               progress.progress != before.progress;
  a.new_achievements = reply->progress.achievements & ~achievements;
  achievements |= reply->progress.achievements;
  state_.cursor = reply->cursor;
  state_.last_sync = int64_t(std::time(nullptr));
  if (!reply->group.empty()) state_.group = reply->group;
  if (!reply->player_name.empty()) state_.player_name = reply->player_name;
  save();
  save_boards(reply->boards);
  // At once again while the server has more for us or games are left to
  // send (a bounded number of times, should a server always say "more").
  if ((reply->more || state_.sent < log.events().size()) && follow_ups_ < kMaxFollowUps) {
    ++follow_ups_;
    sync(log, progress);
  } else {
    follow_ups_ = 0;
  }
  return a;
}

void Online::start_pairing(const std::string& server, const std::string& device_name) {
  uint64_t id;
  {
    std::lock_guard lock(core_->m);
    id = ++core_->pairing_id;
    core_->pairing = PairingView{Pairing::kStarting, {}, {}, {}};
    core_->pairing_server = server;
  }
  core_->cv.notify_all();  // an older pairing's polling stops
  core_->post([id, server, device_name](Core& c) {
    const PairStart s = pair_start(server, device_name);
    {
      std::lock_guard lock(c.m);
      if (id != c.pairing_id) return;
      if (!s.error.empty()) {
        c.pairing = PairingView{Pairing::kFailed, {}, {}, s.error};
        return;
      }
      c.pairing = PairingView{Pairing::kWaiting, s.code, s.url, {}};
    }
    // Asked every two seconds until confirmed, expired or cancelled.
    for (;;) {
      {
        std::unique_lock lock(c.m);
        if (c.cv.wait_for(lock, std::chrono::seconds(2), [&] { return c.stop || id != c.pairing_id; })) return;
      }
      const PairPoll p = pair_poll(server, s.poll);
      std::lock_guard lock(c.m);
      if (id != c.pairing_id) return;
      if (p.status == PairPoll::kPaired) {
        c.pairing.state = Pairing::kPaired;
        c.paired = p;
        return;
      }
      if (p.status == PairPoll::kExpired) {
        c.pairing = PairingView{Pairing::kFailed, {}, {}, "The code expired. Start again for a new one."};
        return;
      }
      if (p.status == PairPoll::kFailed) {
        c.pairing = PairingView{Pairing::kFailed, {}, {}, p.error};
        return;
      }
    }
  });
}

void Online::cancel_pairing() {
  {
    std::lock_guard lock(core_->m);
    ++core_->pairing_id;
    core_->pairing = PairingView{};
  }
  core_->cv.notify_all();
}

Online::PairingView Online::pairing() const {
  std::lock_guard lock(core_->m);
  return core_->pairing;
}

void Online::unpair() {
  cancel_pairing();
  state_ = State{state_.server, {}, {}, {}, {}, 0, 0};  // the address stays, for pairing again
  {
    std::lock_guard lock(core_->m);
    core_->error.clear();
  }
  forget_boards();
  save();
}

void Online::set_server(const std::string& base) {
  if (base == state_.server) return;
  // A different server: any pairing with the old one is forgotten.
  state_ = State{};
  state_.server = base;
  forget_boards();
  save();
}

std::string Online::status() const {
  std::lock_guard lock(core_->m);
  if (core_->syncing) return "syncing...";
  if (!core_->error.empty()) return "sync failed: " + core_->error;
  if (state_.last_sync <= 0) return "not synced yet";
  const std::time_t t = std::time_t(state_.last_sync);
  std::tm local{};
#ifdef _WIN32
  localtime_s(&local, &t);
#else
  localtime_r(&t, &local);
#endif
  char when[32];
  std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M", &local);
  return std::string("last synced at ") + when;
}

std::string Online::error() const {
  std::lock_guard lock(core_->m);
  return core_->error;
}

std::array<std::vector<GroupRow>, 6> Online::group_boards() const {
  std::lock_guard lock(core_->m);
  return core_->boards;
}

std::string Online::device_name() {
  if (const char* deck = std::getenv("SteamDeck"); deck && *deck == '1') return "Steam Deck";
#ifdef _WIN32
  char name[MAX_COMPUTERNAME_LENGTH + 1] = {};
  DWORD n = sizeof(name);
  if (GetComputerNameA(name, &n) && n) return name;
  return "Windows PC";
#else
  char name[256] = {};
  if (gethostname(name, sizeof(name) - 1) == 0 && name[0]) return name;
#ifdef __APPLE__
  return "Mac";
#else
  return "Linux PC";
#endif
#endif
}

}  // namespace geow2
