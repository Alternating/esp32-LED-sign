// sign_state.h — the whole user-visible sign configuration (plan §4.4 / step-3 brief item 3).
// Plain POD, memcpy-able: it is copied between cores under a mutex (hub.cpp) and persisted
// as one blob in NVS namespace "sign" (storage.cpp).
#ifndef SIGN_STATE_H
#define SIGN_STATE_H

#include <Arduino.h>
#include "config.h"

#define SIGN_TEXT_MAX   60
#define SIGN_BLOB_VER   1          // bump when SignState's layout changes -> defaults are used

enum SignFx : uint8_t { FX_STATIC = 0, FX_BLINK = 1, FX_FADE = 2, FX_CYCLE = 3 };

struct SignState {
  char     text[SIGN_TEXT_MAX + 1];  // original message, uppercased, {tokens} intact
  uint8_t  r, g, b;                  // base colour
  bool     rainbow;                  // == (effect == FX_CYCLE); kept for the cfg JSON
  uint8_t  bri;                      // requested 0..100 (the cap is applied at Show-build time)
  uint8_t  effect;                   // SignFx
  uint16_t effectPeriodMs;           // 100..2000
  uint8_t  boards;                   // pinned numBoards, 0 = auto (highest owned position)
};

extern const SignState SIGN_DEFAULT;

// v4 colour-aware brightness cap: rainbow 70; channels >= 64 among r,g,b: 3 -> 60, 2 -> 80, else 100
uint8_t signCap(const SignState& s);
uint8_t signBriEff(const SignState& s);                  // bri * cap / 100

// Lay the text out one char per position: {token} -> symbol code at its OUTPUT index,
// everything else uppercased; stops at MAX_BOARDS (step 2 replaces this with word sequencing).
// Returns the number of positions used.
uint8_t signLayout(const SignState& s, char out[MAX_BOARDS]);

char        symbolCode(const char* token);               // "heart" -> 1 ... "sad" -> 14; 0 = unknown
const char* symbolName(char code);                       // 1 -> "heart" ...; nullptr if not a symbol
const char* fxName(uint8_t fx);                          // "static" "blink" "fade" "cycle"
int         fxFromName(const char* name);                // -1 if unknown

#endif
