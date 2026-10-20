#include "net/scores.h"

#include <algorithm>
#include <cctype>

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

}  // namespace

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
  // Plain http only for this machine (a test server).
  if (s.rfind("http://", 0) == 0) {
    const std::string h = s.substr(host);
    if (h.rfind("localhost", 0) != 0 && h.rfind("127.0.0.1", 0) != 0) return {};
  }
  return s;
}

std::string sync_body(const std::vector<ScoreEvent>& events, const OnlineProgress& progress, int64_t since) {
  json::Array list;
  for (const ScoreEvent& e : events)
    list.push_back(json::Object{{"id", e.id},
                                {"mode", e.mode},
                                {"score", e.score},
                                {"multiplier", e.multiplier},
                                {"time", e.time},
                                {"device", e.device},
                                {"name", e.name}});
  json::Array p;
  for (int32_t v : progress.progress) p.push_back(int64_t(v));
  return json::dump(json::Object{
      {"events", std::move(list)},
      {"progress", json::Object{{"unlocked", progress.unlocked},
                                {"millions", progress.millions},
                                {"progress", std::move(p)},
                                {"achievements", progress.achievements}}},
      {"since", since}});
}

SyncReply parse_sync(int status, const std::string& body) {
  SyncReply r;
  r.status = status;
  const auto v = status == 200 ? json::parse(body) : std::nullopt;
  if (!v || !v->is_object()) {
    r.error = status == 200 ? "the server's answer could not be read" : failure(status, body, {});
    return r;
  }
  r.group = (*v)["group"].string();
  r.player_id = (*v)["player"]["id"].string();
  r.player_name = (*v)["player"]["name"].string();
  for (const json::Value& e : (*v)["events"].array()) {
    ScoreEvent ev;
    ev.id = e["id"].string();
    ev.mode = int(e["mode"].integer(-1));
    ev.score = e["score"].integer();
    ev.multiplier = int(e["multiplier"].integer(1));
    ev.time = e["time"].integer();
    ev.device = e["device"].string();
    ev.name = clean_name(e["name"].string());
    if (ev.id.size() == 32 && ev.mode >= 0 && ev.mode < 6 && ev.score > 0) r.events.push_back(std::move(ev));
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
                                     int(row["multiplier"].integer(1)), row["time"].integer()});
  return r;
}

PairStart parse_pair_start(int status, const std::string& body) {
  PairStart s;
  const auto v = status == 200 ? json::parse(body) : std::nullopt;
  if (v) {
    s.code = (*v)["code"].string();
    s.url = (*v)["url"].string();
    s.poll = (*v)["poll"].string();
  }
  if (s.code.empty() || s.poll.empty()) s.error = failure(status, body, {});
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
  PairStart s = parse_pair_start(h.status, h.body);
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
