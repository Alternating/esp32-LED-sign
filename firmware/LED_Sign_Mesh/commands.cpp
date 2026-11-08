#include "commands.h"
#include "hub.h"
#include "storage.h"
#include "sign_state.h"
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

// ---- msg: uppercase outside {tokens}, <= 60 chars, laid out one char per position ----
static bool cmdMsg(JsonDocument& cmd, JsonDocument& reply) {
  const char* txt = str(cmd, "text");
  if (!txt) return fail(reply, "bad_arg");
  SignState s = hub::sign();
  size_t n = strlen(txt);
  bool truncated = n > SIGN_TEXT_MAX;
  if (truncated) n = SIGN_TEXT_MAX;
  bool inTok = false;
  for (size_t i = 0; i < n; i++) {
    char c = txt[i];
    if (c == '{') inTok = true; else if (c == '}') inTok = false;
    s.text[i] = inTok ? c : (char)toupper((unsigned char)c);
  }
  s.text[n] = 0;
  char chars[MAX_BOARDS];
  uint8_t used = signLayout(s, chars);
  hub::applySign(s);
  reply["text"] = s.text;
  reply["chars"] = used;
  reply["boards"] = hub::numBoardsEffective();
  if (truncated) reply["note"] = "truncated to 60 chars";
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
  if (!strcmp(c, "identify"))    return cmdIdentify(cmd, reply);
  if (!strcmp(c, "reset_order")) { hub::onResetOrd(); return true; }
  if (!strcmp(c, "forget"))      return cmdForget(cmd, reply);
  if (!strcmp(c, "name"))        return cmdName(cmd, reply);
  if (!strcmp(c, "save"))        { hub::saveNow(); return true; }
  return fail(reply, "bad_cmd");
}

} // namespace commands
