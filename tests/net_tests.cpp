// Unit tests for the online scores' JSON and the score server's messages
// (no network).

#include <cstdio>
#include <string>

#include "game/score_log.h"
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
  expect(server_base("http://localhost.example.com").empty() && server_base("http://127.0.0.1.example.com:80").empty(),
         "server: no plain http to names that only start like this machine");
  expect(server_base("http://127.0.0.1:8787") == "http://127.0.0.1:8787", "server: 127.0.0.1 with a port");
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

  expect(r.taken == -1, "sync reply: a server that doesn't say what it took");

  // A hostile or broken server: control bytes (icons and size switches in
  // the game's font) cleaned from everything drawn, numbers out of range,
  // ids that aren't 32 hex digits.
  const SyncReply odd = parse_sync(200, R"({
    "group": "G\u0004roup", "taken": 3, "player": {"id": "p1", "name": "A\u0007ce\u0001"},
    "events": [{"id": "0123456789ABCDEF0123456789ABCDEF", "mode": 0, "score": 5},
               {"id": "0123456789abcdef0123456789abcdef", "mode": 0, "score": 1e300, "device": "d\tx", "name": "n\u001e"},
               {"id": "fedcba9876543210fedcba9876543210", "mode": 1, "score": 7, "multiplier": -5, "name": "B\nob"}],
    "boards": [[{"player": "p1", "name": "Z\u0002", "score": 2, "multiplier": 1e30}], [], [], [], [], []]})");
  expect(odd.error.empty() && odd.group == "G roup" && odd.player_name == "A ce " && odd.taken == 3,
         "sync reply: group and player names cleaned; taken");
  expect(odd.events.size() == 1 && odd.events[0].name == "B ob" && odd.events[0].multiplier == 0,
         "sync reply: upper-case ids, huge scores refused; names cleaned; multiplier clamped");
  expect(odd.boards[0].size() == 1 && odd.boards[0][0].name == "Z " && odd.boards[0][0].multiplier == 1,
         "sync reply: board names cleaned; a huge multiplier falls back");

  const SyncReply denied = parse_sync(401, R"({"error": "This device is not paired"})");
  expect(denied.status == 401 && denied.error == "This device is not paired", "sync reply: the server's error");

  const PairStart s =
      parse_pair_start(200, R"({"code": "ABCD-EFGH", "url": "https://x/pair/ABCDEFGH", "poll": "secret"})", "https://x");
  expect(s.error.empty() && s.code == "ABCD-EFGH" && s.poll == "secret" && s.url == "https://x/pair/ABCDEFGH",
         "pair start");
  // A link elsewhere, or too long for a QR code, becomes the server's own
  // page; a code that isn't one is refused.
  const PairStart elsewhere =
      parse_pair_start(200, R"({"code": "ABCD-EFGH", "url": "https://evil.example/x", "poll": "p"})", "https://x");
  expect(elsewhere.error.empty() && elsewhere.url == "https://x/pair/ABCDEFGH", "pair start: link on the server only");
  const PairStart longer = parse_pair_start(
      200, "{\"code\": \"ABCD-EFGH\", \"url\": \"https://x/" + std::string(5000, 'a') + "\", \"poll\": \"p\"}", "https://x");
  expect(longer.url == "https://x/pair/ABCDEFGH", "pair start: a link too long for a QR code");
  expect(!parse_pair_start(200, R"({"code": "AB\u0001CD", "url": "", "poll": "p"})", "https://x").error.empty(),
         "pair start: odd code refused");
  expect(parse_pair_poll(200, R"({"status": "waiting"})").status == PairPoll::kWaiting, "pair poll: waiting");
  const PairPoll paired = parse_pair_poll(200, R"({"status": "paired", "token": "t", "player": {"id": "p", "name": "Ace"}})");
  expect(paired.status == PairPoll::kPaired && paired.token == "t" && paired.player_name == "Ace", "pair poll: paired");
  expect(parse_pair_poll(500, "oops").status == PairPoll::kFailed, "pair poll: failure");
}

static void test_score_log() {
  // Numbers that don't parse or overflow, and names with control bytes.
  const ScoreLog log = ScoreLog::parse(
      "0123456789abcdef0123456789abcdef\t0\t99999999999999999999\t1\t5\td\tx\n"
      "fedcba9876543210fedcba9876543210\t1\t12x\t1\t5\td\tx\n"
      "00000000000000000000000000000001\t2\t300\t4\t5\tdev\tA\x04" "ce\n");
  expect(log.events().size() == 1 && log.events()[0].score == 300, "score log: bad numbers skip the row");
  expect(log.events()[0].name == "A ce", "score log: control bytes in names become spaces");
  ScoreLog merged;
  merged.add(ScoreEvent{"00000000000000000000000000000002", 0, 9, 1, 0, "d\te\nv", "n"});
  expect(merged.serialize() == "00000000000000000000000000000002\t0\t9\t1\t0\td e v\tn\n",
         "score log: a device with tabs keeps the log's fields apart");
}

static void test_group_boards_file() {
  std::array<std::vector<GroupRow>, 6> boards;
  boards[0] = {GroupRow{"aa", "Ace", 900, 3, 1700000000}, GroupRow{"bb", "B\tob\n", 0, 1, 0}};
  boards[5] = {GroupRow{"cc", "", 12, 1, 5}};
  const std::string text = serialize_group_boards(boards, "https://s.example", "me");
  const auto back = parse_group_boards(text, "https://s.example", "me");
  expect(back[0].size() == 2 && back[0][0].name == "Ace" && back[0][0].score == 900 && back[0][0].multiplier == 3 &&
             back[0][0].time == 1700000000 && back[0][0].player == "aa",
         "group boards file: rows come back in order");
  expect(back[0][1].name == "B ob " && back[0][1].score == 0, "group boards file: control bytes in names become spaces");
  expect(back[5].size() == 1 && back[5][0].name.empty() && back[1].empty(), "group boards file: empty names and modes");
  expect(parse_group_boards(text, "https://other.example", "me")[0].empty(), "group boards file: another server's are ignored");
  expect(parse_group_boards(text, "https://s.example", "someone")[0].empty(), "group boards file: another player's are ignored");
  expect(parse_group_boards(text, "", "")[0].empty(), "group boards file: ignored when not paired");
  const auto bad = parse_group_boards(
      "server https://s.example\r\nplayer me\r\n7\t5\t1\t0\tp\tx\n0\t-4\t1\t0\tp\tx\n0\tlots\t1\t0\tp\tx\n0\t5\t1\n"
      "1\t50\t1\t0\tp\tGood\r\n",
      "https://s.example", "me");
  expect(bad[0].empty() && bad[1].size() == 1 && bad[1][0].name == "Good", "group boards file: bad rows are skipped");
  expect(parse_group_boards("", "https://s.example", "me")[0].empty(), "group boards file: none yet");
}

int main() {
  test_json();
  test_server_base();
  test_messages();
  test_score_log();
  test_group_boards_file();
  if (failures) std::printf("%d failure(s)\n", failures);
  else std::printf("net tests passed\n");
  return failures ? 1 : 0;
}
