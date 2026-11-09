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
  SETMIRROR=13                      // hub -> node unicast, needsAck: set the per-board mirror flag
};

// Hdr.flags bits
constexpr uint8_t FLAG_NEEDS_ACK = 0x01;
constexpr uint8_t FLAG_IS_RETRY  = 0x02;
constexpr uint8_t FLAG_PERIODIC  = 0x04;

struct Hdr {            // 8 bytes
  uint8_t  magic, ver, type;
  uint8_t  flags;       // bit0 needsAck, bit1 isRetry, bit2 periodic (Show sent as heartbeat)
  uint16_t seq;         // per-sender; stamped by radio::send(); receiver drops seq == lastSeq[srcMac]
  uint8_t  srcPos;      // 0 = hub or unassigned
  uint8_t  dstPos;      // 0xFF = all
};

struct Hello    { Hdr h; uint8_t nvsPos; uint8_t fw; uint16_t vbat_mV; uint8_t mirror; };  // 13 B
struct SetMirror{ Hdr h; uint8_t mirror; };                                                 // 9 B, unicast, needsAck
struct Assign   { Hdr h; uint8_t pos; uint8_t channel; uint32_t hubMillis; uint8_t hubPos; }; // 15 B, unicast, needsAck
struct Ack      { Hdr h; uint8_t ackType; uint16_t ackSeq; uint8_t pos; };                  // 12 B
struct SetPos   { Hdr h; uint8_t newPos; };                                                 // 9 B, unicast, needsAck
struct Identify { Hdr h; uint16_t ms; };                                                    // 10 B
struct Hb       { Hdr h; uint16_t vbat_mV; int8_t rssi; uint8_t shownChar; uint8_t state; uint16_t lastShowSeq; }; // 15 B
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
#pragma pack(pop)

static_assert(sizeof(Hdr) == 8,   "Hdr must be 8 bytes");
static_assert(sizeof(Show) == 40, "Show must be 40 bytes");
static_assert(sizeof(Show) <= MAX_FRAME, "Show must fit the radio frame buffer");
static_assert(sizeof(Show) <= 250, "fits ESP-NOW v1");

inline Hdr makeHdr(MsgType t, uint8_t srcPos, uint8_t dstPos, uint8_t flags = 0) {
  Hdr h; h.magic = PROTO_MAGIC; h.ver = PROTO_VER; h.type = t; h.flags = flags;
  h.seq = 0; h.srcPos = srcPos; h.dstPos = dstPos; return h;
}

#endif
