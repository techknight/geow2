#include "app/online.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
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
  size_t snapshot = 0;             // the log's size when the running sync was sent
  std::optional<SyncReply> reply;  // a finished sync, for update()
  std::string status;
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
    << state_.sent << "\n";
  const std::string text = s.str();
  replace_file(dir_ / "online.txt", {reinterpret_cast<const uint8_t*>(text.data()), text.size()});
}

void Online::sync(const ScoreLog& log, const OnlineProgress& progress) {
  if (!paired()) return;
  {
    std::lock_guard lock(core_->m);
    if (core_->syncing) return;
    core_->syncing = true;
    core_->snapshot = log.events().size();
  }
  const auto& all = log.events();
  std::vector<ScoreEvent> unsent(all.begin() + std::ptrdiff_t(std::min(state_.sent, all.size())), all.end());
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
  size_t snapshot;
  {
    std::lock_guard lock(core_->m);
    reply.swap(core_->reply);
    paired.swap(core_->paired);
    server = core_->pairing_server;
    snapshot = core_->snapshot;
    if (reply) {
      core_->syncing = false;
      if (reply->error.empty()) {
        core_->boards = reply->boards;
        core_->status = "Synced.";
      } else {
        core_->status = reply->error;
      }
    }
    if (paired) core_->status = "Paired as " + paired->player_name + ".";
  }
  if (paired) {
    // A new pairing: a fresh start with this server (everything in the
    // log goes up on the first sync).
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
    }
    return std::nullopt;
  }
  Applied a;
  a.new_games = log.merge(reply->events);
  const OnlineProgress before = progress;
  progress.unlocked |= reply->progress.unlocked;
  progress.millions |= reply->progress.millions;
  for (size_t i = 0; i < 6; ++i) progress.progress[i] = std::max(progress.progress[i], reply->progress.progress[i]);
  a.progress = progress.unlocked != before.unlocked || progress.millions != before.millions ||
               progress.progress != before.progress;
  a.new_achievements = reply->progress.achievements & ~achievements;
  achievements |= reply->progress.achievements;
  state_.cursor = reply->cursor;
  state_.sent = snapshot;
  if (!reply->group.empty()) state_.group = reply->group;
  if (!reply->player_name.empty()) state_.player_name = reply->player_name;
  save();
  if (reply->more) sync(log, progress);
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
    core_->boards = {};
    core_->status.clear();
  }
  save();
}

void Online::set_server(const std::string& base) {
  if (base == state_.server) return;
  // A different server: any pairing with the old one is forgotten.
  state_ = State{};
  state_.server = base;
  save();
}

std::string Online::status() const {
  std::lock_guard lock(core_->m);
  return core_->syncing ? "Syncing..." : core_->status;
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
