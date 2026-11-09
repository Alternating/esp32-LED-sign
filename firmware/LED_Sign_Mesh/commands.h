// commands.h — JSON command -> action, shared by every transport and the serial console
// (step-3 brief item 3). Pure glue: validates fields, talks only to the hub:: API.
//
//   bool ok = commands::handle(cmd, reply, isHub);   // caller writes reply["ok"] = ok
//
// On false, reply["err"] is one of "bad_cmd" (unknown cmd), "bad_arg" (missing/invalid
// field), "hub_only" (role is NODE); OTA commands add "busy", "no_image", "offline",
// "up_to_date", "queued", "hub_use_ota_upload". Both documents must live in the calling handler.
#ifndef COMMANDS_H
#define COMMANDS_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"

namespace commands {

bool handle(JsonDocument& cmd, JsonDocument& reply, bool isHub);

} // namespace commands

#endif
