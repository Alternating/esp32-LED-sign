// transport_web.h — HTTP (core WebServer, port 80) + DNSServer captive portal on the hub's
// SoftAP (step-3 brief item 2). Never touches Wi-Fi mode or the AP: radio::beginHub() owns
// that (channel 6). Routes: GET / (gzipped index.html from PROGMEM), POST /cmd (JSON ->
// commands), GET /state (hub::buildState), captive-portal probes -> 302 http://192.168.4.1/.
#ifndef TRANSPORT_WEB_H
#define TRANSPORT_WEB_H

#include <Arduino.h>
#include "config.h"
#include "transport.h"

#define CMD_MAX_BODY 512

namespace transport_web {

Transport& instance();

// Stats for the serial console ('w' / 's'); safe to read from any core
uint32_t reqCount();
uint32_t lastClientIp();     // IPv4 as uint32 (IPAddress(lastClientIp()).toString()), 0 = none yet
uint8_t  apClients();        // WiFi.softAPgetStationNum(), refreshed once a second by poll()

} // namespace transport_web

#endif
