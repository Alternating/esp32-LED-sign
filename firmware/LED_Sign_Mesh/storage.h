// storage.h — Preferences wrapper. Namespaces: "sys" (role, mirror, transport, AP ssid),
// "tbl" (hub MAC->pos table + nextPos), "node" (pos), "sign" (SignState blob).
#ifndef STORAGE_H
#define STORAGE_H

#include <Arduino.h>
#include "config.h"
#include "sign_state.h"

namespace storage {

struct TableEntry {     // hub table row; pos 0 = empty slot
  uint8_t mac[6];
  uint8_t pos;
};

void begin();

Role    loadRole();
void    saveRole(Role role);
bool    loadMirror();
void    saveMirror(bool mirror);
uint8_t loadTransport();
void    saveTransport(uint8_t transport);

// Hub table: MAX_BOARDS entries as one blob + nextPos
void loadTable(TableEntry* entries, uint8_t& nextPos);
void saveTable(const TableEntry* entries, uint8_t nextPos);
void clearTable();

// Node position (0 = none)
uint8_t loadNodePos();
void    saveNodePos(uint8_t pos);

// Sign configuration: one blob prefixed with SIGN_BLOB_VER; wrong size/version -> SIGN_DEFAULT
void loadSign(SignState& s);
void saveSign(const SignState& s);

// AP SSID (namespace "sys"); out gets def when nothing is stored. Returns true if stored.
bool loadSsid(char* out, size_t n, const char* def);
void saveSsid(const char* ssid);

} // namespace storage

#endif
