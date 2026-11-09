// Host-side unit test of the word sequencer (step-2 brief item 2, review R3 trace table).
// Build & run (from the repo root):
//   g++ -std=gnu++17 -Wall -Wextra -I tools/seq_test/shim -I firmware/LED_Sign_Mesh tools/seq_test/seq_test.cpp -o seq_test && ./seq_test
#include "../../firmware/LED_Sign_Mesh/sign_state.cpp"
#include "../../firmware/LED_Sign_Mesh/sequencer.cpp"
#include <stdio.h>
#include <stdlib.h>

static int fails = 0, cases = 0;

static void expect(const char* text, uint8_t mode, uint8_t boards, const char* const* want, int wantN, bool wantTrunc = false) {
  SignState s = SIGN_DEFAULT;
  strncpy(s.text, text, SIGN_TEXT_MAX); s.text[SIGN_TEXT_MAX] = 0;
  s.mode = mode;
  sequencer::Sequencer sq;
  sq.rebuild(s, boards);
  cases++;
  bool ok = sq.groupCount() == wantN && sq.truncated() == wantTrunc;
  char buf[sequencer::GROUP_STR_MAX];
  for (int i = 0; ok && i < wantN; i++) {
    sq.groupString((uint8_t)i, buf, sizeof(buf));
    if (strcmp(buf, want[i]) != 0) ok = false;
    char raw[MAX_BOARDS];
    uint8_t len = sq.group((uint8_t)i, raw);
    uint8_t nb = boards < 1 ? 1 : (boards > MAX_BOARDS ? MAX_BOARDS : boards);
    if (len > nb) ok = false;                                            // every group fits the (clamped) boards
    for (int j = len; j < MAX_BOARDS; j++) if (raw[j] != 0) ok = false;  // zero-filled tail
  }
  printf("%s \"%s\" mode=%s boards=%u -> %u group(s)%s:", ok ? "PASS" : "FAIL", text, modeName(mode), boards,
         sq.groupCount(), sq.truncated() ? " (truncated)" : "");
  for (uint8_t i = 0; i < sq.groupCount(); i++) { sq.groupString(i, buf, sizeof(buf)); printf(" [%s]", buf); }
  if (!ok) { printf("   expected:"); for (int i = 0; i < wantN; i++) printf(" [%s]", want[i]); fails++; }
  printf("\n");
}

#define EXPECT(text, mode, boards, ...) do { static const char* const w[] = { __VA_ARGS__ }; \
  expect(text, mode, boards, w, (int)(sizeof(w) / sizeof(w[0]))); } while (0)

int main() {
  // The five traces from the review table
  EXPECT("WALK THE DOG", SEQ_WORDS, 4, "WALK", "THE", "DOG");
  EXPECT("WALK THE DOG", SEQ_WORDS, 8, "WALK THE", "DOG");
  EXPECT("GO TEAM",      SEQ_WORDS, 2, "GO", "TE", "AM");
  EXPECT("PARTY",        SEQ_WORDS, 2, "PA", "RT", "Y");
  EXPECT("A {heart} B",  SEQ_WORDS, 2, "A", "{heart}", "B");
  // Orchestrator's tail-chunk rule: the tail chunk never packs with the next word
  EXPECT("TEAM GO",      SEQ_WORDS, 3, "TEA", "M", "GO");
  EXPECT("PARTY",        SEQ_WORDS, 4, "PART", "Y");
  // Raw mode: one group of the first numBoards units
  EXPECT("GO TEAM",      SEQ_RAW,   2, "GO");
  EXPECT("GO TEAM",      SEQ_RAW,   10, "GO TEAM");
  EXPECT("A{heart}B",    SEQ_RAW,   2, "A{heart}");
  // Empty / blank text -> one blank group
  EXPECT("",             SEQ_WORDS, 4, "");
  EXPECT("   ",          SEQ_WORDS, 4, "");
  EXPECT("",             SEQ_RAW,   4, "");
  // Tokens: 3 boards fit "A {heart}"; unknown token passes through literally; lowercase in text is uppercased
  EXPECT("A {heart} B",  SEQ_WORDS, 3, "A {heart}", "B");
  EXPECT("hi {xyz}",     SEQ_WORDS, 10, "HI {XYZ}");
  EXPECT("{heart}{star}{check} ok", SEQ_WORDS, 4, "{heart}{star}{check}", "OK");
  // Separators: runs of spaces, leading/trailing spaces
  EXPECT("  GO   TEAM  ", SEQ_WORDS, 7, "GO TEAM");
  EXPECT("HI",           SEQ_WORDS, 2, "HI");
  // Truncation to SEQ_MAX_GROUPS (30 one-letter words on 1 board -> 20 groups, truncated)
  {
    static const char* const w[] = { "A","A","A","A","A","A","A","A","A","A","A","A","A","A","A","A","A","A","A","A" };
    expect("A A A A A A A A A A A A A A A A A A A A A A A A A A A A A A", SEQ_WORDS, 1, w, 20, true);
  }
  // numBoards clamp (0 -> 1, 99 -> 10)
  EXPECT("GO",           SEQ_WORDS, 0, "G", "O");
  EXPECT("ABCDEFGHIJKLM", SEQ_WORDS, 99, "ABCDEFGHIJ", "KLM");
  // current()/advance()/reset()
  {
    SignState s = SIGN_DEFAULT; strcpy(s.text, "GO TEAM");
    sequencer::Sequencer sq; sq.rebuild(s, 2);
    cases++;
    bool ok = sq.current() == 0 && sq.advance() == 1 && sq.advance() == 2 && sq.advance() == 0;
    sq.reset(); ok = ok && sq.current() == 0;
    sequencer::Sequencer empty; ok = ok && empty.advance() == 0;          // no divide by zero before rebuild
    printf("%s advance/reset cycle\n", ok ? "PASS" : "FAIL"); if (!ok) fails++;
  }
  // signLayout (raw layout used by commands' `chars`) agrees with the raw group
  {
    SignState s = SIGN_DEFAULT; strcpy(s.text, "A {heart} B"); s.mode = SEQ_RAW;
    char lay[MAX_BOARDS]; uint8_t n = signLayout(s, lay);
    sequencer::Sequencer sq; sq.rebuild(s, 10); char g[MAX_BOARDS]; uint8_t gl = sq.group(0, g);
    cases++;
    bool ok = n == 5 && gl == 5 && memcmp(lay, g, MAX_BOARDS) == 0 && lay[2] == 1;
    printf("%s signLayout == raw group (%u units)\n", ok ? "PASS" : "FAIL", n); if (!ok) fails++;
  }
  printf("%d/%d passed\n", cases - fails, cases);
  return fails ? 1 : 0;
}
