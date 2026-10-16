/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BUMPKINS_MIDI_H
#define BUMPKINS_MIDI_H
#include <stdint.h>
#include "abi.h"
#define BB_MIDI_M BB_THISCALL __attribute__((noinline))
uint8_t bb_midi_read_byte_entry(void *);
uint16_t bb_midi_read_word_entry(void *);
uint32_t bb_midi_read_three_entry(void *);
uint32_t bb_midi_read_dword_entry(void *);
uint32_t bb_midi_read_variable_entry(void *);
void *BB_MIDI_M bb_midi_track_construct_entry(void *);
void BB_MIDI_M bb_midi_track_append_entry(void *, uint32_t, uint8_t, uint8_t,
                                          uint8_t, uint8_t);
void BB_MIDI_M bb_midi_track_program_entry(void *, uint8_t);
void BB_MIDI_M bb_midi_track_release_entry(void *);
void BB_MIDI_M bb_midi_track_begin_entry(void *);
void BB_MIDI_M bb_midi_track_rewind_entry(void *);
void BB_MIDI_M bb_midi_track_event_entry(void *, const void *);
void BB_MIDI_M bb_midi_track_tick_entry(void *, uint32_t);
void *BB_MIDI_M bb_midi_construct_entry(void *);
uint32_t BB_MIDI_M bb_midi_load_entry(void *, const char *);
uint32_t BB_MIDI_M bb_midi_read_entry(void *, void *);
void BB_MIDI_M bb_midi_release_entry(void *);
#endif
