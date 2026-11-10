// protocol.h — ESP-NOW framing, byte-exact with PROJECT_PLAN.md §4.1
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <Arduino.h>
#include "config.h"

#pragma pack(push, 1)
constexpr uint8_t PROTO_MAGIC = 0xA5, PROTO_VER = 1;
constexpr uint8_t POS_ALL = 0xFF, POS_HUB = 0;

enum MsgType : uint8_t {
  HELLO=1, ASSIGN=2, ACK=3, CLAIM=4, SETPOS=5, SHOW=6, HB=7,
  IDENTIFY=8, RESETORD=9, FORGET=10, PING=11, PONG=12,
  SETMIRROR=13,                     // hub -> node unicast, needsAck: set the per-board mirror flag
  OTA_ENTER=14,                     // hub -> node unicast, needsAck: join the AP and pull /fw.bin (Phase 4)
  OTA_STATUS=15                     // node -> hub unicast: download progress / result (Phase 4)
};

// OtaStatus.state (shared by both ends; the hub's UI states are a separate enum in hub.cpp)
enum OtaWireState : uint8_t { OTA_ST_JOINING = 0, OTA_ST_DOWNLOADING = 1, OTA_ST_VERIFIED = 2, OTA_ST_FAILED = 4 };

// Hdr.flags bits
constexpr uint8_t FLAG_NEEDS_ACK = 0x01;
constexpr uint8_t FLAG_IS_RETRY  = 0x02;
constexpr uint8_t FLAG_PERIODIC  = 0x04;
constexpr uint8_t FLAG_BANDS     = 0x08;   // Show: colour glyphs in horizontal rainbow bands (row-based, same on every board)

struct Hdr {            // 8 bytes
  uint8_t  magic, ver, type;
  uint8_t  flags;       // bit0 needsAck, bit1 isRetry, bit2 periodic (Show sent as heartbeat)
  uint16_t seq;         // per-sender; stamped by radio::send(); receiver drops seq == lastSeq[srcMac]
  uint8_t  srcPos;      // 0 = hub or unassigned
  uint8_t  dstPos;      // 0xFF = all
};

// Phase 4: Hello/Hb gain buildId[4] (first 4 bytes of the app ELF sha256) APPENDED, wire version stays 1.
// Receivers read the base fields when len >= *_V1_LEN and buildId only when len >= sizeof(T); a
// pre-Phase-4 frame has no buildId -> treated as 00000000 ("unknown -> needs update").
struct Hello    { Hdr h; uint8_t nvsPos; uint8_t fw; uint16_t vbat_mV; uint8_t mirror; uint8_t buildId[4]; };  // 17 B (v1: 13)
struct SetMirror{ Hdr h; uint8_t mirror; };                                                 // 9 B, unicast, needsAck
struct Assign   { Hdr h; uint8_t pos; uint8_t channel; uint32_t hubMillis; uint8_t hubPos; }; // 15 B, unicast, needsAck
struct Ack      { Hdr h; uint8_t ackType; uint16_t ackSeq; uint8_t pos; };                  // 12 B
struct SetPos   { Hdr h; uint8_t newPos; };                                                 // 9 B, unicast, needsAck
struct Identify { Hdr h; uint16_t ms; };                                                    // 10 B
struct Hb       { Hdr h; uint16_t vbat_mV; int8_t rssi; uint8_t shownChar; uint8_t state; uint16_t lastShowSeq; uint8_t buildId[4]; }; // 19 B (v1: 15)
struct Ping     { Hdr h; uint32_t hubMillis; };                                             // 12 B (diagnostics)
struct Pong     { Hdr h; uint32_t hubMillis; int8_t rssiAtNode; uint32_t nodeFreeHeap; };   // 17 B (diagnostics)

struct Show {                       // the entire display state; broadcast
  Hdr      h;
  uint32_t hubMillis;               // hub clock at send (time sync)
  uint32_t applyAtHubMs;            // 0 = apply now
  uint16_t showSeq;                 // increments on every content/style change
  uint8_t  r, g, b;                 // base colour
  uint8_t  brightness;              // 0..100
  uint8_t  effect;                  // 0 static (only effect this step)
  uint16_t effectPeriodMs;
  uint16_t wordPeriodMs;
  uint8_t  numBoards;               // active positions (1..10)
  uint8_t  wordIdx, wordCount;
  char     chars[MAX_BOARDS];       // chars[pos-1]; 0 = blank. 1..14 = symbol codes
};                                  // 40 B

struct OtaEnter {                   // hub -> node, unicast, needsAck
  Hdr      h;
  char     ssid[33];                // hub AP SSID (NUL-terminated)
  char     pass[33];                // hub AP password
  uint32_t ip;                      // hub AP IPv4 (IPAddress as uint32)
  uint16_t port;                    // OTA_PORT
  uint8_t  buildId[4];              // what the node will be running afterwards
  uint32_t size;                    // exact image length served at /fw.bin (Content-Length)
  uint8_t  bssid[6];                // hub softAP MAC: directed association on ESPNOW_CHANNEL, no channel scan
};                                  // 94 B

struct OtaStatus {                  // node -> hub, unicast
  Hdr      h;
  uint8_t  pct;                     // 0..100
  uint8_t  state;                   // OtaWireState
  uint8_t  buildId[4];              // the node's RUNNING buildId (old image until it reboots)
  char     msg[24];                 // failure reason, NUL-terminated
};                                  // 38 B
#pragma pack(pop)

constexpr size_t HELLO_V1_LEN = 13, HB_V1_LEN = 15;

static_assert(sizeof(Hdr) == 8,   "Hdr must be 8 bytes");
static_assert(sizeof(Show) == 40, "Show must be 40 bytes");
static_assert(sizeof(Show) <= MAX_FRAME, "Show must fit the radio frame buffer");
static_assert(sizeof(Show) <= 250, "fits ESP-NOW v1");
static_assert(sizeof(Hello) == 17 && sizeof(Hb) == 19, "Hello/Hb = v1 layout + buildId[4]");
static_assert(sizeof(OtaEnter) == 94 && sizeof(OtaStatus) == 38, "OTA frame layout");
static_assert(sizeof(Hello) <= MAX_FRAME && sizeof(Hb) <= MAX_FRAME && sizeof(Assign) <= MAX_FRAME &&
              sizeof(Ack) <= MAX_FRAME && sizeof(SetPos) <= MAX_FRAME && sizeof(SetMirror) <= MAX_FRAME &&
              sizeof(Identify) <= MAX_FRAME && sizeof(Ping) <= MAX_FRAME && sizeof(Pong) <= MAX_FRAME &&
              sizeof(OtaEnter) <= MAX_FRAME && sizeof(OtaStatus) <= MAX_FRAME, "every frame fits MAX_FRAME");
static_assert(MAX_FRAME <= 250, "MAX_FRAME fits ESP-NOW v1");

inline Hdr makeHdr(MsgType t, uint8_t srcPos, uint8_t dstPos, uint8_t flags = 0) {
  Hdr h; h.magic = PROTO_MAGIC; h.ver = PROTO_VER; h.type = t; h.flags = flags;
  h.seq = 0; h.srcPos = srcPos; h.dstPos = dstPos; return h;
}

#endif
