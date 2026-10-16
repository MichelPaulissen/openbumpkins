/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#ifndef BUMPKINS_INSTRUMENTS_H
#define BUMPKINS_INSTRUMENTS_H
#include <stdint.h>
#include "abi.h"
#define BB_INSTRUMENT_M BB_THISCALL __attribute__((noinline))
void BB_INSTRUMENT_M bb_instrument_song_start_entry(void *);
void BB_INSTRUMENT_M bb_instrument_song_rewind_entry(void *);
void BB_INSTRUMENT_M bb_instrument_song_tick_entry(void *, uint32_t);
void BB_INSTRUMENT_M bb_instrument_device_initialize_entry(void *, void *);
void BB_INSTRUMENT_M bb_instrument_device_attach_entry(void *, void *, void *,
                                                       void *);
uint32_t BB_INSTRUMENT_M bb_instrument_runtime_initialize_entry(void *);
void BB_INSTRUMENT_M bb_instrument_runtime_release_entry(void *);
void BB_INSTRUMENT_M bb_instrument_gain_set_entry(void *, double);
long double BB_INSTRUMENT_M bb_instrument_gain_get_entry(void *);
void BB_INSTRUMENT_M bb_instrument_song_load_entry(void *, const char *);
uint32_t BB_INSTRUMENT_M bb_instrument_bank_read_entry(void *, void *);
uint32_t BB_INSTRUMENT_M bb_instrument_bank_load_entry(void *, const char *);
uint32_t BB_INSTRUMENT_M bb_instrument_bank_write_entry(void *, void *);
uint32_t BB_INSTRUMENT_M bb_instrument_bank_save_entry(void *, const char *);
uint32_t BB_INSTRUMENT_M bb_instrument_bank_import_entry(void *, const char *);
uint32_t BB_INSTRUMENT_M bb_instrument_bank_export_entry(void *, const char *);
uint32_t BB_STDCALL bb_instrument_worker_entry(void *);
void BB_INSTRUMENT_M bb_instrument_program_append_entry(void *, void *);
void BB_INSTRUMENT_M bb_instrument_note_on_entry(void *, uint8_t, uint8_t,
                                                 uint8_t);
void BB_INSTRUMENT_M bb_instrument_note_off_entry(void *, uint8_t, uint8_t,
                                                  uint8_t);
void BB_INSTRUMENT_M bb_instrument_midi_open_entry(void *);
void BB_INSTRUMENT_M bb_instrument_midi_close_entry(void *);
#endif
