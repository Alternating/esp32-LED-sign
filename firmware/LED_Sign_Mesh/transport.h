// transport.h — abstract transport interface (plan §5.1 / step-3 brief item 1).
// A transport carries JSON commands from a phone to commands::handle() and (later) events
// back. Phase 1: transport_web (HTTP + captive portal). Phase 3 adds transport_ble; the
// sketch only holds a `Transport*`.
//
// begin() runs in setup() BEFORE the net task exists; poll() runs on the core-0 net task
// every ~5 ms and must be short and non-blocking; sendEvent() may be called from either core.
#ifndef TRANSPORT_H
#define TRANSPORT_H

#include <Arduino.h>
#include <ArduinoJson.h>

typedef bool (*CmdHandler)(JsonDocument& cmd, JsonDocument& reply);   // returns ok; sets reply["err"] on false

struct Transport {
  virtual ~Transport() {}
  virtual void begin() = 0;
  virtual void poll() = 0;
  virtual void sendEvent(const char* json) = 0;     // HTTP: no-op (the phone polls /state)
  virtual void setCommandHandler(CmdHandler fn) = 0;
};

#endif
