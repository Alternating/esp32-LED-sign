#include "commands.h"
#include "hub.h"
#include "storage.h"
#include "sign_state.h"
#include "sequencer.h"
#include <string.h>
#include <ctype.h>

namespace commands {

static bool fail(JsonDocument& reply, const char* err) { reply["err"] = err; return false; }

static const char* str(JsonDocument& cmd, const char* key) {
  return cmd[key].is<const char*>() ? cmd[key].as<const char*>() : nullptr;
}

static bool parseHex(const char* hex, uint8_t& r, uint8_t& g, uint8_t& b) {
  if (!hex) return false;
  if (*hex == '#') hex++;
  if (strlen(hex) != 6) return false;
  for (int i = 0; i < 6; i++) if (!isxdigit((unsigned char)hex[i])) return false;
  uint32_t v = strtoul(hex, nullptr, 16);
  r = (v >> 16) & 0xFF; g = (v >> 8) & 0xFF; b = v & 0xFF;
  return true;
}

static void echoSign(const SignState& s, JsonDocument& reply) {
  char hex[7]; snprintf(hex, sizeof(hex), "%02X%02X%02X", s.r, s.g, s.b);
  reply["color"] = hex;
  reply["rainbow"] = s.rainbow;
  reply["bri"] = s.bri;
  reply["briEff"] = signBriEff(s);
  reply["fx"] = fxName(s.effect);
  reply["speed"] = s.effectPeriodMs;
}

// Word groups of `s` for the effective board count (local Sequencer: predicts what tick() applies)
static void putGroups(const SignState& s, JsonDocument& reply) {
  uint8_t boards = s.boards ? s.boards : hub::numBoardsEffective();
  sequencer::Sequencer sq;
  sq.rebuild(s, boards);
  JsonArray groups = reply["groups"].to<JsonArray>();
  char gs[sequencer::GROUP_STR_MAX];
  for (uint8_t i = 0; i < sq.groupCount(); i++) { sq.groupString(i, gs, sizeof(gs)); groups.add(gs); }
  reply["groupCount"] = sq.groupCount();
  reply["truncated"] = sq.truncated();
}

// ---- msg: uppercase outside {tokens}, <= 60 DISPLAY units ({happy} = 1); reply predicts the word groups ----
static bool cmdMsg(JsonDocument& cmd, JsonDocument& reply) {
  const char* txt = str(cmd, "text");
  if (!txt) return fail(reply, "bad_arg");
  SignState s = hub::sign();
  bool truncated = false;
  uint8_t units = signClip(txt, s.text, &truncated);
  char chars[MAX_BOARDS];
  uint8_t used = signLayout(s, chars);
  hub::applySign(s);
  reply["text"] = s.text;
  reply["chars"] = used;                 // raw one-per-position count (UI char counter)
  reply["units"] = units;                // message length in display units (limit SIGN_UNITS_MAX)
  reply["boards"] = hub::numBoardsEffective();
  reply["mode"] = modeName(s.mode);
  putGroups(s, reply);
  if (truncated) reply["note"] = "truncated to 60 characters (a symbol counts as one)";
  return true;
}

// ---- color: hex "RRGGBB" (optional '#') or rainbow:true ----
static bool cmdColor(JsonDocument& cmd, JsonDocument& reply) {
  SignState s = hub::sign();
  if (cmd["rainbow"].is<bool>() && cmd["rainbow"].as<bool>()) {
    s.rainbow = true; s.effect = FX_CYCLE;
  } else {
    uint8_t r, g, b;
    if (!parseHex(str(cmd, "hex"), r, g, b)) return fail(reply, "bad_arg");
    s.r = r; s.g = g; s.b = b;
    s.rainbow = false;
    if (s.effect == FX_CYCLE) s.effect = FX_STATIC;
  }
  hub::applySign(s);
  echoSign(s, reply);
  return true;
}

// ---- bri: v 0..100 (requested; cap applied at Show time, effective value echoed) ----
static bool cmdBri(JsonDocument& cmd, JsonDocument& reply) {
  if (!cmd["v"].is<int>()) return fail(reply, "bad_arg");
  int v = cmd["v"].as<int>();
  if (v < 0 || v > 100) return fail(reply, "bad_arg");
  SignState s = hub::sign();
  s.bri = (uint8_t)v;
  hub::applySign(s);
  echoSign(s, reply);
  reply["cap"] = signCap(s);
  return true;
}

// ---- fx: name static|blink|fade|cycle, speed 100..2000 (optional, keeps current) ----
static bool cmdFx(JsonDocument& cmd, JsonDocument& reply) {
  int fx = fxFromName(str(cmd, "name"));
  if (fx < 0) return fail(reply, "bad_arg");
  SignState s = hub::sign();
  if (!cmd["speed"].isNull()) {
    if (!cmd["speed"].is<int>()) return fail(reply, "bad_arg");
    int sp = cmd["speed"].as<int>();
    if (sp < 100 || sp > 2000) return fail(reply, "bad_arg");
    s.effectPeriodMs = (uint16_t)sp;
  }
  s.effect = (uint8_t)fx;
  s.rainbow = (fx == FX_CYCLE);
  hub::applySign(s);
  echoSign(s, reply);
  return true;
}

// ---- boards: n 0..10 (0 = auto) ----
static bool cmdBoards(JsonDocument& cmd, JsonDocument& reply) {
  if (!cmd["n"].is<int>()) return fail(reply, "bad_arg");
  int n = cmd["n"].as<int>();
  if (n < 0 || n > MAX_BOARDS) return fail(reply, "bad_arg");
  SignState s = hub::sign();
  s.boards = (uint8_t)n;
  hub::applySign(s);
  reply["boards"] = n ? n : hub::numBoardsEffective();
  reply["boardsAuto"] = (n == 0);
  return true;
}

// ---- wspeed: ms 500..10000 (word period; takes effect from the next advance, index kept) ----
static bool cmdWspeed(JsonDocument& cmd, JsonDocument& reply) {
  if (!cmd["ms"].is<int>()) return fail(reply, "bad_arg");
  int ms = cmd["ms"].as<int>();
  if (ms < 500 || ms > 10000) return fail(reply, "bad_arg");
  SignState s = hub::sign();
  s.wordPeriodMs = (uint16_t)ms;
  hub::applySign(s);
  reply["wspeed"] = ms;
  return true;
}

// ---- mode: "words" | "raw" (regroups from group 0) ----
static bool cmdMode(JsonDocument& cmd, JsonDocument& reply) {
  int m = modeFromName(str(cmd, "mode"));
  if (m < 0) return fail(reply, "bad_arg");
  SignState s = hub::sign();
  s.mode = (uint8_t)m;
  hub::applySign(s);
  reply["mode"] = modeName(s.mode);
  putGroups(s, reply);
  return true;
}

// ---- identify: pos 1..10 or 255, ms optional ----
static bool cmdIdentify(JsonDocument& cmd, JsonDocument& reply) {
  if (!cmd["pos"].is<int>()) return fail(reply, "bad_arg");
  int pos = cmd["pos"].as<int>();
  if (!(pos == 255 || (pos >= 1 && pos <= MAX_BOARDS))) return fail(reply, "bad_arg");
  int ms = cmd["ms"].is<int>() ? cmd["ms"].as<int>() : IDENTIFY_MS;
  if (ms < 200 || ms > 10000) return fail(reply, "bad_arg");
  if (!hub::identifyPos((uint8_t)pos, (uint16_t)ms)) return fail(reply, "bad_arg");
  reply["pos"] = pos; reply["ms"] = ms;
  return true;
}

// ---- forget: pos 2..10 ----
static bool cmdForget(JsonDocument& cmd, JsonDocument& reply) {
  if (!cmd["pos"].is<int>()) return fail(reply, "bad_arg");
  int pos = cmd["pos"].as<int>();
  if (pos < 2 || pos > MAX_BOARDS) return fail(reply, "bad_arg");
  if (!hub::forgetPos((uint8_t)pos)) return fail(reply, "bad_arg");
  reply["pos"] = pos;
  return true;
}

// ---- mirror: pos 1..MAX_BOARDS, v true/false  (fixes a board whose data line enters from the other side) ----
static bool cmdMirror(JsonDocument& cmd, JsonDocument& reply) {
  if (!cmd["pos"].is<int>() || !cmd["v"].is<bool>()) return fail(reply, "bad_arg");
  int pos = cmd["pos"].as<int>();
  if (pos < 1 || pos > MAX_BOARDS) return fail(reply, "bad_arg");
  if (!hub::setMirrorPos((uint8_t)pos, cmd["v"].as<bool>())) return fail(reply, "bad_arg");
  reply["pos"] = pos; reply["mirror"] = cmd["v"].as<bool>();
  return true;
}

// ---- name: v 1..24 chars of [A-Za-z0-9_-]; persisted, applies next boot ----
static bool cmdName(JsonDocument& cmd, JsonDocument& reply) {
  const char* v = str(cmd, "v");
  if (!v) return fail(reply, "bad_arg");
  size_t n = strlen(v);
  if (n < 1 || n > 24) return fail(reply, "bad_arg");
  for (size_t i = 0; i < n; i++) {
    unsigned char c = (unsigned char)v[i];
    if (!(isalnum(c) || c == '-' || c == '_')) return fail(reply, "bad_arg");
  }
  storage::saveSsid(v);
  reply["name"] = v;
  reply["note"] = "applies next boot";
  return true;
}

// ---- ota_nodes: queue every online node whose buildId != the hub's (Phase 4) ----
static bool cmdOtaNodes(JsonDocument&, JsonDocument& reply) {
  uint8_t queued, skipped; const char* err;
  if (!hub::otaNodes(queued, skipped, err)) return fail(reply, err);
  reply["queued"] = queued;
  reply["skipped"] = skipped;
  return true;
}

// ---- ota_node: pos 2..10 (pos 1 is the hub itself: upload to POST /ota instead) ----
static bool cmdOtaNode(JsonDocument& cmd, JsonDocument& reply) {
  if (!cmd["pos"].is<int>()) return fail(reply, "bad_arg");
  int pos = cmd["pos"].as<int>();
  if (pos == 1) return fail(reply, "hub_use_ota_upload");
  if (pos < 2 || pos > MAX_BOARDS) return fail(reply, "bad_arg");
  const char* err;
  if (!hub::otaNode((uint8_t)pos, err)) return fail(reply, err);
  reply["pos"] = pos;
  reply["queued"] = 1;
  return true;
}

bool handle(JsonDocument& cmd, JsonDocument& reply, bool isHub) {
  const char* c = str(cmd, "cmd");
  if (!c) return fail(reply, "bad_cmd");
  if (!isHub) return fail(reply, "hub_only");

  if (!strcmp(c, "get"))         { hub::buildState(reply.to<JsonObject>()); return true; }
  if (!strcmp(c, "msg"))         return cmdMsg(cmd, reply);
  if (!strcmp(c, "color"))       return cmdColor(cmd, reply);
  if (!strcmp(c, "bri"))         return cmdBri(cmd, reply);
  if (!strcmp(c, "fx"))          return cmdFx(cmd, reply);
  if (!strcmp(c, "boards"))      return cmdBoards(cmd, reply);
  if (!strcmp(c, "wspeed"))      return cmdWspeed(cmd, reply);
  if (!strcmp(c, "mode"))        return cmdMode(cmd, reply);
  if (!strcmp(c, "identify"))    return cmdIdentify(cmd, reply);
  if (!strcmp(c, "reset_order")) { hub::onResetOrd(); return true; }
  if (!strcmp(c, "forget"))      return cmdForget(cmd, reply);
  if (!strcmp(c, "mirror"))      return cmdMirror(cmd, reply);
  if (!strcmp(c, "name"))        return cmdName(cmd, reply);
  if (!strcmp(c, "save"))        { hub::saveNow(); return true; }
  if (!strcmp(c, "ota_nodes"))   return cmdOtaNodes(cmd, reply);
  if (!strcmp(c, "ota_node"))    return cmdOtaNode(cmd, reply);
  if (!strcmp(c, "ota_cancel"))  { reply["dropped"] = hub::otaCancel(); return true; }
  return fail(reply, "bad_cmd");
}

} // namespace commands
