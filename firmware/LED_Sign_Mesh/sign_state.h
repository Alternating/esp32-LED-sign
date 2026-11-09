// sign_state.h — the whole user-visible sign configuration (plan §4.4 / step-3 brief item 3,
// step-2 brief item 1). Plain POD, memcpy-able: it is copied between cores under a mutex
// (hub.cpp) and persisted as one blob in NVS namespace "sign" (storage.cpp).
//
// LAYOUT IS THE ON-FLASH BLOB LAYOUT. Appending fields keeps the v1 prefix byte-identical,
// which is what makes the v1 -> v2 migration in storage.cpp a plain field copy. Any change
// here needs SIGN_BLOB_VER++ and a new frozen SignStateVn + migration branch in storage.cpp.
#ifndef SIGN_STATE_H
#define SIGN_STATE_H

#include <Arduino.h>
#include "config.h"

#define SIGN_TEXT_MAX   60
#define SIGN_BLOB_VER   2          // 1 = step 3 (no wordPeriodMs/mode), 2 = step 2 sequencer fields

enum SignFx   : uint8_t { FX_STATIC = 0, FX_BLINK = 1, FX_FADE = 2, FX_CYCLE = 3 };
enum SignMode : uint8_t { SEQ_WORDS = 0, SEQ_RAW = 1 };   // raw = one char per position (step-3 behaviour)

struct SignState {
  char     text[SIGN_TEXT_MAX + 1];  // original message, uppercased outside {tokens}, tokens intact
  uint8_t  r, g, b;                  // base colour
  bool     rainbow;                  // == (effect == FX_CYCLE); kept for the cfg JSON
  uint8_t  bri;                      // requested 0..100 (the cap is applied at Show-build time)
  uint8_t  effect;                   // SignFx
  uint16_t effectPeriodMs;           // 100..2000
  uint8_t  boards;                   // pinned numBoards, 0 = auto (highest owned position)
  uint16_t wordPeriodMs;             // 500..10000, default 2000 (v2)
  uint8_t  mode;                     // SignMode (v2)
};
static_assert(sizeof(SignState) == 76, "SignState layout is the NVS blob layout; add a SignStateVn migration if this changes");

extern const SignState SIGN_DEFAULT;

// v4 colour-aware brightness cap: rainbow 70; channels >= 64 among r,g,b: 3 -> 60, 2 -> 80, else 100
uint8_t signCap(const SignState& s);
uint8_t signBriEff(const SignState& s);                  // bri * cap / 100

// Tokenise the text into display units, one byte per unit: {token} -> symbol code (1..14),
// everything else uppercased (spaces stay ' '). Unknown {xyz} passes through literally.
// out holds at most SIGN_TEXT_MAX units; returns the unit count. Shared by signLayout() and
// the word sequencer so the raw layout, the groups and the web preview agree.
uint8_t signUnits(const char* text, char out[SIGN_TEXT_MAX]);

// Lay the text out one char per position (SEQ_RAW): the first MAX_BOARDS units, zero-filled.
// Returns the number of positions used.
uint8_t signLayout(const SignState& s, char out[MAX_BOARDS]);

char        symbolCode(const char* token);               // "heart" -> 1 ... "sad" -> 14; 0 = unknown
const char* symbolName(char code);                       // 1 -> "heart" ...; nullptr if not a symbol
const char* fxName(uint8_t fx);                          // "static" "blink" "fade" "cycle"
int         fxFromName(const char* name);                // -1 if unknown
const char* modeName(uint8_t mode);                      // "words" "raw"
int         modeFromName(const char* name);              // -1 if unknown (strcasecmp)

#endif
