// ota.h — Phase 4 OTA mechanics, role-independent (brief docs/build-briefs/PHASE4_OTA_hub_hosted_updates.md,
// review docs/reviews/PHASE4_REVIEW.md §R1/R2/R5). JSON-free: the hub queue and /state.ota live in hub.cpp,
// the node's OTA state lives in node.cpp; this file holds the transfer and rollback machinery.
//
// Threading:
//   begin(), tick(), printStatus()       core 1 (setup/loop)
//   fw server task                       core 0, prio 1, 8 kB stack (hub; serves GET /fw.bin on OTA_PORT)
//   upload*()                            core 0 (WebServer handlers on the net task)
//   node OTA task                        core 0, prio 1, 8 kB stack (node; join AP, GET /fw.bin, Update)
//   accessors                            any core (volatiles / immutable after begin())
// The OTA tasks never touch display:: and never call esp_now_send (status frames are sent from node::tick()).
//
// Rollback: built-in IDF rollback (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y in core 3.3.12). ota.cpp overrides
// the Arduino core's weak verifyRollbackLater() so the core does NOT auto-confirm a freshly written image;
// tick(healthy) confirms it after OTA_VERIFY_MS of healthy operation. An unconfirmed image is rolled back by
// the bootloader on the NEXT reset (one reset — power cycle, panic, WDT — not three).
#ifndef OTA_H
#define OTA_H

#include <Arduino.h>
#include "config.h"
#include "protocol.h"

namespace ota {

void           begin(bool hub);               // buildId, slot, rollback state, boot log; hub: image length
const uint8_t* buildId();                     // 4 bytes = first 4 bytes of esp_app_desc_t.app_elf_sha256
void           buildIdHex(const uint8_t* id, char out9[9]);   // "1a2b3c4d"
const char*    versionStr();                  // FW_VERSION_STR
const char*    gitStr();                      // BUILD_GIT (build_id.h, written by tools/make_images.sh) or "dev"
const char*    slotName();                    // running partition label ("app0" / "app1")
uint32_t       imageLen();                    // exact running image length (hub; 0 = unknown -> no node OTA)
bool           pendingVerify();               // running image still ESP_OTA_IMG_PENDING_VERIFY
uint32_t       verifyLeftMs();                // ms until the confirm window opens (0 when open/confirmed)
void           tick(bool healthy);            // core 1: confirm the image once; deferred reboot
void           requestReboot(uint32_t inMs);  // any core: ESP.restart() from tick() after inMs
void           printStatus();                 // one line for the serial 's' dump

// ---- hub: firmware server (GET /fw.bin on OTA_PORT) ----
bool     startFwServer();                     // call after the AP is up; false if no image length
bool     fwServing();
uint32_t fwServed();                          // bytes of the current / last transfer
uint32_t fwTransfers();

// ---- hub: POST /ota upload (WebServer callbacks, core 0) ----
void uploadBegin(const char* rejectReason);   // nullptr = accept; else every chunk is dropped and the reply carries it
void uploadWrite(uint8_t* buf, size_t n);
void uploadEnd(size_t total);
void uploadAbort();                           // client went away
bool uploadActive();
bool uploadResult(uint32_t& bytes, const char*& err);   // after END: true = new image committed

// ---- node: pull the hub's image ----
enum NodeResult : uint8_t { NODE_IDLE = 0, NODE_RUNNING = 1, NODE_OK = 2, NODE_FAILED = 3 };
bool        nodeStart(const OtaEnter& e);     // spawns the task; false if one is already running
NodeResult  nodeResult();
uint8_t     nodePct();
uint8_t     nodePhase();                      // OTA_ST_JOINING / OTA_ST_DOWNLOADING
const char* nodeMsg();                        // failure reason
void        nodeReset();                      // back to NODE_IDLE once the caller has consumed the result

} // namespace ota

#endif
