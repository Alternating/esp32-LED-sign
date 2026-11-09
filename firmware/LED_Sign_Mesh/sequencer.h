// sequencer.h — word-group layout of the sign text (step-2 brief item 2; legacy
// parseMessageIntoWords() ported and extended with chunking). Pure: no Arduino calls, no heap,
// so tools/seq_test/seq_test.cpp compiles it on the host. The hub owns one instance (core 1);
// commands/buildState build a short-lived local one to predict/report groups (any core).
//
// Grouping rule (identical to groupsFor() in web/index.html):
//   units  = signUnits(text): one byte per display position, {token} = ONE unit
//   words  = runs of non-space units (runs of spaces are one separator, leading/trailing ignored)
//   a word longer than numBoards is cut into numBoards-unit chunks; each chunk is its own group
//   and the tail chunk does NOT pack with the following word ("TEAM GO"@3 -> TEA / M / GO)
//   otherwise consecutive words are greedily packed with one space while length <= numBoards
//   ("WALK THE DOG"@8 -> "WALK THE" / DOG; @4 -> WALK / THE / DOG)
//   > SEQ_MAX_GROUPS groups -> the rest is dropped and truncated() is true
//   empty / blank text -> one blank group (len 0, chars all 0)
//   SEQ_RAW -> one group = the first numBoards units (step-3 behaviour)
#ifndef SEQUENCER_H
#define SEQUENCER_H

#include "sign_state.h"

namespace sequencer {

constexpr uint8_t SEQ_MAX_GROUPS = 20;
constexpr size_t  GROUP_STR_MAX  = MAX_BOARDS * 9 + 1;      // "{diamond}" x MAX_BOARDS + NUL

class Sequencer {
public:
  void     rebuild(const SignState& s, uint8_t numBoards);  // numBoards clamped to 1..MAX_BOARDS; resets current()
  uint8_t  groupCount() const { return count_; }
  bool     truncated()  const { return truncated_; }
  uint8_t  group(uint8_t i, char out[MAX_BOARDS]) const;    // zero-filled copy; returns len (0 = blank group)
  size_t   groupString(uint8_t i, char* out, size_t n) const; // display string, {tokens} restored, codes < 32 skipped
  uint8_t  current() const { return cur_; }
  uint8_t  advance() { if (count_) cur_ = (uint8_t)((cur_ + 1) % count_); return cur_; }
  void     reset() { cur_ = 0; }

private:
  void     push(const char* units, uint8_t n);              // appends a group; sets truncated_ when full
  char     groups_[SEQ_MAX_GROUPS][MAX_BOARDS];
  uint8_t  len_[SEQ_MAX_GROUPS];
  uint8_t  count_ = 0, cur_ = 0;
  bool     truncated_ = false;
};

} // namespace sequencer

#endif
