// Unit tests for the online scores' JSON and the score server's messages
// (no network).

#include <cstdio>
#include <string>

#include "net/json.h"
#include "net/scores.h"

using namespace geow2;

static int failures = 0;

static void expect(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

static void test_json() {
  const auto v = json::parse(R"( {"a": [1, -2, 3.5, true, null], "s": "tab\there é 😀", "big": 9007199254740993} )");
  expect(v.has_value(), "json: parses");
  expect((*v)["a"][0].integer() == 1 && (*v)["a"][1].integer() == -2, "json: whole numbers");
  expect((*v)["a"][3].boolean() && (*v)["a"][4].is_null() && (*v)["missing"].is_null(), "json: true, null, absent");
  expect((*v)["s"].string() == "tab\there \xC3\xA9 \xF0\x9F\x98\x80", "json: escapes, \\u and surrogate pairs to UTF-8");
  expect((*v)["big"].integer() == 9007199254740993LL, "json: whole numbers past 2^53 stay exact");
  expect(!json::parse("{\"a\": }") && !json::parse("[1,]") && !json::parse("\"open") && !json::parse("1 2"),
         "json: malformed input refused");
  std::string deep(100, '[');
  expect(!json::parse(deep), "json: nesting is limited");
  const json::Value back = *json::parse(json::dump(*v));
  expect(json::dump(back) == json::dump(*v), "json: dump and parse round-trip");
  expect(json::dump(json::Object{{"q", "a\"b\\c\n"}}) == R"({"q":"a\"b\\c\n"})", "json: strings escaped");
}

static void test_server_base() {
  expect(server_base("scores.example.com") == "https://scores.example.com", "server: https added");
  expect(server_base(" https://scores.example.com/pair/ABCD?x=1 ") == "https://scores.example.com", "server: path dropped");
  expect(server_base("http://localhost:8787/") == "http://localhost:8787", "server: http for this machine");
  expect(server_base("http://scores.example.com").empty(), "server: no plain http elsewhere");
  expect(server_base("ftp://x").empty() && server_base("").empty() && server_base("a b").empty(), "server: not addresses");
}

static void test_messages() {
  ScoreEvent e{"0123456789abcdef0123456789abcdef", 5, 123456, 7, 1760000000, "dev", "Pilot"};
  OnlineProgress p;
  p.unlocked = 7;
  p.progress = {400, 12, 0, 0, 0, 0};
  p.achievements = 0x11;
  const auto body = json::parse(sync_body({e}, p, 42));
  expect(body && (*body)["since"].integer() == 42, "sync body: cursor");
  expect((*body)["events"][0]["score"].integer() == 123456 && (*body)["events"][0]["mode"].integer() == 5, "sync body: event");
  expect((*body)["progress"]["progress"][0].integer() == 400 && (*body)["progress"]["achievements"].integer() == 0x11,
         "sync body: progress");

  const SyncReply r = parse_sync(200, R"({
    "group": "Friday Grid", "player": {"id": "p1", "name": "Ace"},
    "events": [{"id": "0123456789abcdef0123456789abcdef", "mode": 0, "score": 900, "multiplier": 3, "time": 5,
                "device": "d", "name": "Ace"}, {"id": "short", "mode": 0, "score": 1}],
    "cursor": 17, "more": false,
    "progress": {"unlocked": 7, "millions": 1, "progress": [400, 300, 999, 0, 0, 0], "achievements": 17},
    "boards": [[{"player": "p1", "name": "Ace", "score": 900, "multiplier": 3, "time": 5}], [], [], [], [], []]})");
  expect(r.error.empty() && r.group == "Friday Grid" && r.player_name == "Ace", "sync reply: who");
  expect(r.events.size() == 1 && r.events[0].score == 900, "sync reply: valid events only");
  expect(r.cursor == 17 && r.progress.unlocked == 7 && r.progress.progress[1] == 300, "sync reply: cursor, progress");
  expect(r.progress.progress[2] == 400, "sync reply: progress clamped to the unlock threshold");
  expect(r.boards[0].size() == 1 && r.boards[0][0].name == "Ace", "sync reply: boards");

  const SyncReply denied = parse_sync(401, R"({"error": "This device is not paired"})");
  expect(denied.status == 401 && denied.error == "This device is not paired", "sync reply: the server's error");

  const PairStart s = parse_pair_start(200, R"({"code": "ABCD-EFGH", "url": "https://x/pair/ABCDEFGH", "poll": "secret"})");
  expect(s.error.empty() && s.code == "ABCD-EFGH" && s.poll == "secret", "pair start");
  expect(parse_pair_poll(200, R"({"status": "waiting"})").status == PairPoll::kWaiting, "pair poll: waiting");
  const PairPoll paired = parse_pair_poll(200, R"({"status": "paired", "token": "t", "player": {"id": "p", "name": "Ace"}})");
  expect(paired.status == PairPoll::kPaired && paired.token == "t" && paired.player_name == "Ace", "pair poll: paired");
  expect(parse_pair_poll(500, "oops").status == PairPoll::kFailed, "pair poll: failure");
}

int main() {
  test_json();
  test_server_base();
  test_messages();
  if (failures) std::printf("%d failure(s)\n", failures);
  else std::printf("net tests passed\n");
  return failures ? 1 : 0;
}
