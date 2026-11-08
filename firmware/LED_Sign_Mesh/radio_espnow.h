// radio_espnow.h — ESP-NOW init (per role), RX ring buffer, gated send queue with
// app-level ACK retries, per-MAC dedupe. Init order and callback signatures follow
// firmware/bench/espnow_ping/espnow_ping.ino (verified on hardware).
//
// Threading: the ESP-NOW RX callback ONLY copies into a ring buffer; the TX callback
// ONLY sets flags. radio::pump() (core-0 net task) drains the ring, dispatches to the
// registered handler and is the ONLY place that calls esp_now_send(). radio::send()
// may be called from any core (it just enqueues).
#ifndef RADIO_ESPNOW_H
#define RADIO_ESPNOW_H

#include <Arduino.h>
#include "config.h"

namespace radio {

typedef void (*RxHandler)(const uint8_t* srcMac, const uint8_t* buf, uint8_t len, int8_t rssi);

extern const uint8_t BCAST[6];

bool beginHub();                 // WIFI_AP_STA + softAP pinned to ESPNOW_CHANNEL
bool beginNode();                // WIFI_STA + esp_wifi_set_channel
void setHandler(RxHandler fn);

// Stamps h.seq = ++txSeq and the needsAck flag into buf, then queues a copy.
// needsAck items are retried ACK_RETRY times every ACK_RETRY_MS until ackReceived().
bool send(const uint8_t* mac, void* buf, uint8_t len, bool needsAck);
void ackReceived(const uint8_t* mac, uint16_t ackSeq);
void addPeer(const uint8_t* mac);   // idempotent
void pump();                        // call every ~5 ms from the net task only

bool           channelOk();
const uint8_t* myMac();
void           macToStr(const uint8_t* mac, char* out18);   // "AA:BB:CC:DD:EE:FF"

// Counters for the status dump
uint32_t txCount();
uint32_t txFailCount();
uint32_t rxCount();
uint32_t rxDropCount();   // ring overflow + dedupe drops

} // namespace radio

#endif
