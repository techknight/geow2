#include "net/scores.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <sstream>

#include "net/http.h"
#include "net/json.h"

namespace geow2 {
namespace {

std::string trim(std::string s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
  size_t i = 0;
  while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
  return s.substr(i);
}

// The server's own words when it sent some ({"error": ...}), else the
// transport's, else the status.
std::string failure(int status, const std::string& body, const std::string& transport) {
  if (status == 0) return transport.empty() ? "no answer from the server" : transport;
  if (auto v = json::parse(body)) {
    const std::string e = (*v)["error"].string();
    if (!e.empty()) return e;
  }
  return "the server answered " + std::to_string(status);
}

// A whole number field of group-boards.txt, or `fallback` when it isn't one.
int64_t number(const std::string& s, int64_t fallback) {
  int64_t n = 0;
  const auto r = std::from_chars(s.data(), s.data() + s.size(), n);
  return r.ec == std::errc() && r.ptr == s.data() + s.size() ? n : fallback;
}

}  // namespace

std::string serialize_group_boards(const std::array<std::vector<GroupRow>, 6>& boards, const std::string& server,
                                   const std::string& player) {
  std::ostringstream out;
  out << "server " << clean_name(server) << "\nplayer " << clean_name(player) << '\n';
  for (int m = 0; m < 6; ++m)
    for (const GroupRow& r : boards[size_t(m)])
      out << m << '\t' << r.score << '\t' << r.multiplier << '\t' << r.time << '\t' << clean_name(r.player) << '\t'
          << clean_name(r.name) << '\n';
  return out.str();
}

std::array<std::vector<GroupRow>, 6> parse_group_boards(const std::string& text, const std::string& server,
                                                        const std::string& player) {
  std::array<std::vector<GroupRow>, 6> boards;
  std::istringstream in(text);
  std::string line;
  auto header = [&](const std::string& key, const std::string& want) {
    if (!std::getline(in, line)) return false;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return !want.empty() && line == key + ' ' + want;
  };
  if (!header("server", server) || !header("player", player)) return {};
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::vector<std::string> f;
    std::istringstream s(line);
    for (std::string part; std::getline(s, part, '\t');) f.push_back(part);
    if (f.size() < 5) continue;
    const int64_t mode = number(f[0], -1);
    GroupRow r;
    r.score = number(f[1], -1);
    r.multiplier = int(std::clamp<int64_t>(number(f[2], 1), 0, 1000000));
    r.time = number(f[3], 0);
    r.player = f[4];
    r.name = f.size() > 5 ? f[5] : "";
    if (mode < 0 || mode > 5 || r.score < 0) continue;
    boards[size_t(mode)].push_back(std::move(r));
  }
  return boards;
}

std::string server_base(const std::string& typed) {
  std::string s = trim(typed);
  if (s.empty() || s.find_first_of(" \t\"<>\\") != std::string::npos) return {};
  if (s.find("://") == std::string::npos) s = "https://" + s;
  if (s.rfind("https://", 0) != 0 && s.rfind("http://", 0) != 0) return {};
  // The scheme and host (and port); any path, query or trailing slash goes.
  const size_t host = s.find("://") + 3;
  const size_t end = s.find_first_of("/?#", host);
  if (end != std::string::npos) s.resize(end);
  if (s.size() <= host) return {};
  // Plain http only for this machine (a test server): the host exactly,
  // with or without a port ("localhost.example.com" is not this machine).
  if (s.rfind("http://", 0) == 0) {
    const std::string hostport = s.substr(host);
    const std::string h = hostport.substr(0, hostport.find(':'));
    if (h != "localhost" && h != "127.0.0.1") return {};
  }
  return s;
}

std::string sync_body(const std::vector<ScoreEvent>& events, const OnlineProgress& progress, int64_t since) {
  // (Built field by field: GCC 13 warns of uninitialized variants in
  // nested braced lists of json::Value.)
  json::Array list;
  for (const ScoreEvent& e : events) {
    json::Object o;
    o["id"] = e.id;
    o["mode"] = e.mode;
    o["score"] = e.score;
    o["multiplier"] = e.multiplier;
    o["time"] = e.time;
    o["device"] = e.device;
    o["name"] = e.name;
    list.emplace_back(std::move(o));
  }
  json::Array per_mode;
  for (int32_t v : progress.progress) per_mode.emplace_back(int64_t(v));
  json::Object p;
  p["unlocked"] = progress.unlocked;
  p["millions"] = progress.millions;
  p["progress"] = std::move(per_mode);
  p["achievements"] = progress.achievements;
  json::Object body;
  body["events"] = std::move(list);
  body["progress"] = std::move(p);
  body["since"] = since;
  return json::dump(body);
}

SyncReply parse_sync(int status, const std::string& body) {
  SyncReply r;
  r.status = status;
  const auto v = status == 200 ? json::parse(body) : std::nullopt;
  if (!v || !v->is_object()) {
    r.error = status == 200 ? "the server's answer could not be read" : failure(status, body, {});
    return r;
  }
  // Everything from the server that the game draws or writes to scores.log
  // is cleaned of control characters, whatever the server checked.
  r.group = clean_name((*v)["group"].string());
  r.player_id = (*v)["player"]["id"].string();
  r.player_name = clean_name((*v)["player"]["name"].string());
  r.taken = (*v)["taken"].integer(-1);
  for (const json::Value& e : (*v)["events"].array()) {
    ScoreEvent ev;
    ev.id = e["id"].string();
    ev.mode = int(e["mode"].integer(-1));
    ev.score = e["score"].integer();
    ev.multiplier = int(std::clamp<int64_t>(e["multiplier"].integer(1), 0, 1000000));
    ev.time = e["time"].integer();
    ev.device = clean_name(e["device"].string());
    ev.name = clean_name(e["name"].string());
    const bool hex = std::all_of(ev.id.begin(), ev.id.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
    if (ev.id.size() == 32 && hex && ev.mode >= 0 && ev.mode < 6 && ev.score > 0) r.events.push_back(std::move(ev));
  }
  r.cursor = (*v)["cursor"].integer();
  r.more = (*v)["more"].boolean();
  const json::Value& p = (*v)["progress"];
  r.progress.unlocked = uint32_t(p["unlocked"].integer(2));
  r.progress.millions = uint32_t(p["millions"].integer());
  for (size_t i = 0; i < 6; ++i) r.progress.progress[i] = int32_t(std::clamp<int64_t>(p["progress"][i].integer(), 0, 400));
  r.progress.achievements = uint32_t(p["achievements"].integer());
  for (size_t m = 0; m < 6; ++m)
    for (const json::Value& row : (*v)["boards"][m].array())
      r.boards[m].push_back(GroupRow{row["player"].string(), clean_name(row["name"].string()), row["score"].integer(),
                                     int(std::clamp<int64_t>(row["multiplier"].integer(1), 0, 1000000)),
                                     row["time"].integer()});
  return r;
}

PairStart parse_pair_start(int status, const std::string& body, const std::string& base) {
  PairStart s;
  const auto v = status == 200 ? json::parse(body) : std::nullopt;
  if (v) {
    s.code = (*v)["code"].string();
    s.url = (*v)["url"].string();
    s.poll = (*v)["poll"].string();
  }
  if (s.code.empty() || s.poll.empty()) {
    s.error = failure(status, body, {});
    return s;
  }
  // The code is shown and the link becomes the QR code: a short code of
  // letters, digits and dashes, and a link on this server short enough for
  // a QR code, else the server's own pairing page for the code.
  const bool code_ok = s.code.size() <= 16 && std::all_of(s.code.begin(), s.code.end(), [](char c) {
                         return std::isalnum(static_cast<unsigned char>(c)) || c == '-';
                       });
  if (!code_ok) {
    s.error = "the server's answer could not be read";
    return s;
  }
  if (s.url.size() > 400 || s.url.rfind(base + "/", 0) != 0) {
    std::string plain;
    for (char c : s.code)
      if (c != '-') plain += c;
    s.url = base + "/pair/" + plain;
  }
  return s;
}

PairPoll parse_pair_poll(int status, const std::string& body) {
  PairPoll p;
  const auto v = status == 200 ? json::parse(body) : std::nullopt;
  if (!v) {
    p.error = failure(status, body, {});
    return p;
  }
  const std::string st = (*v)["status"].string();
  if (st == "waiting") {
    p.status = PairPoll::kWaiting;
  } else if (st == "expired") {
    p.status = PairPoll::kExpired;
  } else if (st == "paired" && !(*v)["token"].string().empty()) {
    p.status = PairPoll::kPaired;
    p.token = (*v)["token"].string();
    p.player_id = (*v)["player"]["id"].string();
    p.player_name = clean_name((*v)["player"]["name"].string());
  } else {
    p.error = "the server's answer could not be read";
  }
  return p;
}

PairStart pair_start(const std::string& base, const std::string& device_name) {
  const HttpResponse h = http_request(base + "/api/pair/start", json::dump(json::Object{{"device_name", device_name}}));
  PairStart s = parse_pair_start(h.status, h.body, base);
  if (h.status == 0) s.error = failure(0, {}, h.error);
  return s;
}

PairPoll pair_poll(const std::string& base, const std::string& poll) {
  const HttpResponse h = http_request(base + "/api/pair/poll", json::dump(json::Object{{"poll", poll}}));
  PairPoll p = parse_pair_poll(h.status, h.body);
  if (h.status == 0) p.error = failure(0, {}, h.error);
  return p;
}

SyncReply sync(const std::string& base, const std::string& token, const std::vector<ScoreEvent>& events,
               const OnlineProgress& progress, int64_t since) {
  const HttpResponse h = http_request(base + "/api/sync", sync_body(events, progress, since), token);
  SyncReply r = parse_sync(h.status, h.body);
  if (h.status == 0) r.error = failure(0, {}, h.error);
  return r;
}

}  // namespace geow2
