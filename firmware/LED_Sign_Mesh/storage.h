// storage.h — Preferences wrapper. Namespaces: "sys" (role, mirror, transport),
// "tbl" (hub MAC->pos table + nextPos), "node" (pos).
#ifndef STORAGE_H
#define STORAGE_H

#include <Arduino.h>
#include "config.h"

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

} // namespace storage

#endif
