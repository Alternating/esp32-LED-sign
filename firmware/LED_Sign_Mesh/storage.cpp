#include "storage.h"
#include <Preferences.h>
#include <string.h>

namespace storage {

static const char* NS_SYS  = "sys";
static const char* NS_TBL  = "tbl";
static const char* NS_NODE = "node";
static const char* NS_SIGN = "sign";

// All namespaces are opened read-write (begin(ns, false)): a read-only open of a
// namespace that does not exist yet fails, and puts on a read-only handle are no-ops.

void begin() {
  Preferences p;
  p.begin(NS_SYS, false); p.end();     // make sure the namespace exists
}

Role loadRole() {
  Preferences p; p.begin(NS_SYS, false);
  uint8_t r = p.getUChar("role", (uint8_t)Role::NODE);
  p.end();
  return r == (uint8_t)Role::HUB ? Role::HUB : Role::NODE;
}

void saveRole(Role role) {
  Preferences p; p.begin(NS_SYS, false);
  p.putUChar("role", (uint8_t)role);
  p.end();
}

bool loadMirror() {
  Preferences p; p.begin(NS_SYS, false);
  bool m = p.getBool("mirror", false);
  p.end();
  return m;
}

void saveMirror(bool mirror) {
  Preferences p; p.begin(NS_SYS, false);
  p.putBool("mirror", mirror);
  p.end();
}

uint8_t loadTransport() {
  Preferences p; p.begin(NS_SYS, false);
  uint8_t t = p.getUChar("transport", 0);
  p.end();
  return t;
}

void saveTransport(uint8_t transport) {
  Preferences p; p.begin(NS_SYS, false);
  p.putUChar("transport", transport);
  p.end();
}

void loadTable(TableEntry* entries, uint8_t& nextPos) {
  const size_t want = sizeof(TableEntry) * MAX_BOARDS;
  memset(entries, 0, want);
  Preferences p; p.begin(NS_TBL, false);
  size_t got = p.getBytesLength("table");
  if (got == want) p.getBytes("table", entries, want);
  else if (got != 0) Serial.printf("[STORAGE] table blob size %u != %u, ignoring\n", (unsigned)got, (unsigned)want);
  nextPos = p.getUChar("nextPos", 2);
  p.end();
  if (nextPos < 2) nextPos = 2;
}

void saveTable(const TableEntry* entries, uint8_t nextPos) {
  Preferences p; p.begin(NS_TBL, false);
  p.putBytes("table", entries, sizeof(TableEntry) * MAX_BOARDS);
  p.putUChar("nextPos", nextPos);
  p.end();
}

void clearTable() {
  Preferences p; p.begin(NS_TBL, false);
  p.clear();
  p.end();
}

uint8_t loadNodePos() {
  Preferences p; p.begin(NS_NODE, false);
  uint8_t pos = p.getUChar("pos", 0);
  p.end();
  return pos <= MAX_BOARDS ? pos : 0;
}

void saveNodePos(uint8_t pos) {
  Preferences p; p.begin(NS_NODE, false);
  p.putUChar("pos", pos);
  p.end();
}

void loadSign(SignState& s) {
  uint8_t buf[1 + sizeof(SignState)];
  Preferences p; p.begin(NS_SIGN, false);
  size_t got = p.getBytesLength("sign");
  bool ok = (got == sizeof(buf)) && p.getBytes("sign", buf, sizeof(buf)) == sizeof(buf) && buf[0] == SIGN_BLOB_VER;
  p.end();
  if (ok) {
    memcpy(&s, buf + 1, sizeof(SignState));
    s.text[SIGN_TEXT_MAX] = 0;
    if (s.bri > 100) s.bri = 100;
    if (s.effect > FX_CYCLE) s.effect = FX_STATIC;
    if (s.effectPeriodMs < 100 || s.effectPeriodMs > 2000) s.effectPeriodMs = 500;
    if (s.boards > MAX_BOARDS) s.boards = 0;
    s.rainbow = (s.effect == FX_CYCLE);
  } else {
    if (got != 0) Serial.printf("[STORAGE] sign blob size %u / version mismatch, using defaults\n", (unsigned)got);
    s = SIGN_DEFAULT;
  }
}

void saveSign(const SignState& s) {
  uint8_t buf[1 + sizeof(SignState)];
  buf[0] = SIGN_BLOB_VER;
  memcpy(buf + 1, &s, sizeof(SignState));
  Preferences p; p.begin(NS_SIGN, false);
  p.putBytes("sign", buf, sizeof(buf));
  p.end();
}

bool loadSsid(char* out, size_t n, const char* def) {
  out[0] = 0;
  Preferences p; p.begin(NS_SYS, false);
  size_t got = p.getString("ssid", out, n);
  p.end();
  if (got == 0 || out[0] == 0) { strncpy(out, def, n - 1); out[n - 1] = 0; return false; }
  return true;
}

void saveSsid(const char* ssid) {
  Preferences p; p.begin(NS_SYS, false);
  p.putString("ssid", ssid);
  p.end();
}

} // namespace storage
