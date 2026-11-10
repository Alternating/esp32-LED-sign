#include "sequencer.h"
#include <string.h>

namespace sequencer {

void Sequencer::push(const char* units, uint8_t n) {
  if (count_ >= SEQ_MAX_GROUPS) { truncated_ = true; return; }
  if (n > MAX_BOARDS) n = MAX_BOARDS;                       // cannot happen (callers cap at numBoards), defensive
  memset(groups_[count_], 0, MAX_BOARDS);
  memcpy(groups_[count_], units, n);
  len_[count_] = n;
  count_++;
}

// Invariant: every word is consumed exactly once by the loop below. A word longer than nb is
// emitted as chunks and never enters `cur`; a word <= nb always fits an empty `cur`, so the
// "flush then start a new group" branch always makes progress.
void Sequencer::rebuild(const SignState& s, uint8_t numBoards) {
  memset(groups_, 0, sizeof(groups_));
  memset(len_, 0, sizeof(len_));
  count_ = 0; cur_ = 0; truncated_ = false;
  uint8_t nb = numBoards < 1 ? 1 : (numBoards > MAX_BOARDS ? MAX_BOARDS : numBoards);

  char units[SIGN_UNITS_MAX];
  uint8_t n = signUnits(s.text, units);

  if (s.mode == SEQ_RAW) {                                  // one group: the first nb units
    push(units, n < nb ? n : nb);
    return;
  }

  char cur[MAX_BOARDS]; uint8_t cl = 0;                     // group being packed
  uint8_t i = 0;
  while (i < n) {
    if (units[i] == ' ') { i++; continue; }                 // separator (runs collapse)
    uint8_t ws = i;
    while (i < n && units[i] != ' ') i++;
    uint8_t wl = (uint8_t)(i - ws);                         // word = units[ws .. ws+wl)

    if (wl > nb) {                                          // chunk: flush, then nb-unit pieces, each alone
      if (cl) { push(cur, cl); cl = 0; }
      for (uint8_t off = 0; off < wl; off = (uint8_t)(off + nb)) {
        uint8_t len = (uint8_t)(wl - off); if (len > nb) len = nb;
        push(units + ws + off, len);
      }
      continue;
    }
    uint8_t need = (uint8_t)(wl + (cl ? 1 : 0));
    if ((uint8_t)(cl + need) <= nb) {                       // pack with one space
      if (cl) cur[cl++] = ' ';
      memcpy(cur + cl, units + ws, wl); cl = (uint8_t)(cl + wl);
    } else {                                                // does not fit: flush, start new group
      push(cur, cl);
      memcpy(cur, units + ws, wl); cl = wl;
    }
  }
  if (cl) push(cur, cl);
  if (count_ == 0) push(cur, 0);                            // empty / blank text -> one blank group
}

uint8_t Sequencer::group(uint8_t i, char out[MAX_BOARDS]) const {
  memset(out, 0, MAX_BOARDS);
  if (i >= count_) return 0;
  memcpy(out, groups_[i], len_[i]);
  return len_[i];
}

size_t Sequencer::groupString(uint8_t i, char* out, size_t n) const {
  size_t o = 0;
  if (n == 0) return 0;
  out[0] = 0;
  if (i >= count_) return 0;
  for (uint8_t j = 0; j < len_[i]; j++) {
    char c = groups_[i][j];
    const char* sym = symbolName(c);
    if (sym) {
      size_t sl = strlen(sym);
      if (o + sl + 2 >= n) break;
      out[o++] = '{'; memcpy(out + o, sym, sl); o += sl; out[o++] = '}';
    } else if (c >= 32) {
      if (o + 1 >= n) break;
      out[o++] = c;
    }                                                       // other control codes: skipped
  }
  out[o] = 0;
  return o;
}

} // namespace sequencer
