// Minimal Arduino.h shim for host-side unit tests (tools/seq_test). Only what sign_state.cpp
// and sequencer.cpp need: fixed-width ints and libc string/ctype functions.
#ifndef ARDUINO_SHIM_H
#define ARDUINO_SHIM_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#endif
