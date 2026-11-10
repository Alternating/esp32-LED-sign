#include "sign_state.h"
#include <string.h>
#include <ctype.h>

const SignState SIGN_DEFAULT = { "HI", 0, 255, 0, false, 100, FX_STATIC, 500, 0, 2000, SEQ_WORDS };

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

static const char* const MODE_NAMES[] = { "words", "raw" };

const char* modeName(uint8_t mode) { return mode <= SEQ_RAW ? MODE_NAMES[mode] : "words"; }

int modeFromName(const char* name) {
  if (!name) return -1;
  for (int i = 0; i <= SEQ_RAW; i++) if (strcasecmp(MODE_NAMES[i], name) == 0) return i;
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

// length in bytes of a known {token} starting at text[i] (0 = not a known token)
static size_t knownTokenLen(const char* text, size_t i, size_t len) {
  if (text[i] != '{') return 0;
  const char* e = (const char*)memchr(text + i, '}', len - i);
  if (!e) return 0;
  size_t tlen = (size_t)(e - (text + i)) - 1;
  if (tlen < 1 || tlen >= 16) return 0;
  char tok[16]; memcpy(tok, text + i + 1, tlen); tok[tlen] = 0;
  return symbolCode(tok) ? tlen + 2 : 0;
}

uint8_t signClip(const char* in, char out[SIGN_TEXT_MAX + 1], bool* truncated) {
  size_t len = strlen(in), i = 0, o = 0;
  uint8_t units = 0;
  while (i < len && units < SIGN_UNITS_MAX) {
    size_t tl = knownTokenLen(in, i, len);
    size_t ul = tl ? tl : 1;
    if (o + ul > SIGN_TEXT_MAX) break;                  // never split a token across the byte cap
    if (tl) { memcpy(out + o, in + i, tl); }            // tokens stay as typed ({happy})
    else    { out[o] = (char)toupper((unsigned char)in[i]); }
    o += ul; i += ul; units++;
  }
  out[o] = 0;
  if (truncated) *truncated = i < len;
  return units;
}

uint8_t signUnits(const char* text, char out[SIGN_UNITS_MAX]) {
  size_t len = strnlen(text, SIGN_TEXT_MAX);
  uint8_t o = 0;
  for (size_t i = 0; i < len && o < SIGN_UNITS_MAX; ) {
    if (text[i] == '{') {
      const char* e = (const char*)memchr(text + i, '}', len - i);
      if (e) {
        size_t tlen = (size_t)(e - (text + i)) - 1;
        if (tlen >= 1 && tlen < 16) {
          char tok[16]; memcpy(tok, text + i + 1, tlen); tok[tlen] = 0;
          char code = symbolCode(tok);
          if (code) { out[o++] = code; i += tlen + 2; continue; }
        }
      }
    }
    out[o++] = (char)toupper((unsigned char)text[i++]);   // unknown tokens pass through literally
  }
  return o;
}

uint8_t signLayout(const SignState& s, char out[MAX_BOARDS]) {
  char units[SIGN_UNITS_MAX];
  uint8_t n = signUnits(s.text, units);
  if (n > MAX_BOARDS) n = MAX_BOARDS;
  memset(out, 0, MAX_BOARDS);
  memcpy(out, units, n);
  return n;
}
