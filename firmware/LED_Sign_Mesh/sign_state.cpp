#include "sign_state.h"
#include <string.h>
#include <ctype.h>

const SignState SIGN_DEFAULT = { "HI", 0, 255, 0, false, 100, FX_STATIC, 500, 0 };

struct SymbolEntry { const char* name; char code; };
static const SymbolEntry SYMBOLS[] = {
  { "heart", 1 }, { "star", 2 }, { "check", 3 }, { "cross", 4 }, { "right", 5 }, { "left", 6 },
  { "up", 7 }, { "down", 8 }, { "diamond", 9 }, { "circle", 10 }, { "happy", 13 }, { "sad", 14 },
};

char symbolCode(const char* token) {
  if (!token) return 0;
  for (auto& e : SYMBOLS) if (strcasecmp(e.name, token) == 0) return e.code;
  return 0;
}

const char* symbolName(char code) {
  for (auto& e : SYMBOLS) if (e.code == code) return e.name;
  return nullptr;
}

static const char* const FX_NAMES[] = { "static", "blink", "fade", "cycle" };

const char* fxName(uint8_t fx) { return fx <= FX_CYCLE ? FX_NAMES[fx] : "static"; }

int fxFromName(const char* name) {
  if (!name) return -1;
  for (int i = 0; i <= FX_CYCLE; i++) if (strcasecmp(FX_NAMES[i], name) == 0) return i;
  return -1;
}

uint8_t signCap(const SignState& s) {
  if (s.rainbow || s.effect == FX_CYCLE) return 70;
  int ch = (s.r >= 64) + (s.g >= 64) + (s.b >= 64);
  return ch >= 3 ? 60 : ch == 2 ? 80 : 100;
}

uint8_t signBriEff(const SignState& s) {
  uint8_t bri = s.bri > 100 ? 100 : s.bri;
  return (uint8_t)((uint16_t)bri * signCap(s) / 100);
}

uint8_t signLayout(const SignState& s, char out[MAX_BOARDS]) {
  memset(out, 0, MAX_BOARDS);
  size_t len = strnlen(s.text, SIGN_TEXT_MAX);
  uint8_t o = 0;
  for (size_t i = 0; i < len && o < MAX_BOARDS; ) {
    if (s.text[i] == '{') {
      const char* e = (const char*)memchr(s.text + i, '}', len - i);
      if (e) {
        size_t tlen = (size_t)(e - (s.text + i)) - 1;
        if (tlen >= 1 && tlen < 16) {
          char tok[16]; memcpy(tok, s.text + i + 1, tlen); tok[tlen] = 0;
          char code = symbolCode(tok);
          if (code) { out[o++] = code; i += tlen + 2; continue; }
        }
      }
    }
    out[o++] = (char)toupper((unsigned char)s.text[i++]);   // unknown tokens pass through literally
  }
  return o;
}
