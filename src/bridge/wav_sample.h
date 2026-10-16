/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BUMPKINS_WAV_SAMPLE_H
#define BUMPKINS_WAV_SAMPLE_H
#include <stdint.h>
#include "abi.h"
#define BB_WAV_M BB_THISCALL __attribute__((noinline))
void BB_WAV_M bb_wav_device_initialize_entry(void *, void *);
void BB_WAV_M bb_wav_device_attach_entry(void *, void *, void *);
void *BB_WAV_M bb_wav_acquire_entry(void *, const char *);
void BB_WAV_M bb_wav_remove_entry(void *, void *);
void BB_WAV_M bb_wav_play_entry(void *, void *);
uint32_t BB_WAV_M bb_wav_load_entry(void *, const char *);
void *BB_WAV_M bb_wav_lookup_entry(void *, const char *);
void *BB_WAV_M bb_wav_construct_entry(void *);
void BB_WAV_M bb_wav_buffers_entry(void *);
void BB_WAV_M bb_wav_fill_entry(void *);
void BB_WAV_M bb_wav_trigger_entry(void *);
void BB_WAV_M bb_wav_root_note_entry(void *, uint8_t);
void BB_WAV_M bb_wav_note_range_entry(void *, uint8_t, uint8_t);
int32_t BB_WAV_M bb_wav_voice_entry(void *);
void BB_WAV_M bb_wav_note_entry(void *, uint8_t, uint8_t);
#endif
