/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#ifndef BUMPKINS_BOOTSTRAP_RESOURCES_H
#define BUMPKINS_BOOTSTRAP_RESOURCES_H
#include <stdint.h>
void *BB_THISCALL bb_shared_construct_entry(void *);
void BB_THISCALL bb_shared_initialize_entry(void *, uint32_t);
uint32_t BB_THISCALL bb_shared_start_entry(void *, uint32_t);
void BB_THISCALL bb_shared_reset_entry(void *);
uint32_t BB_THISCALL
bb_shared_create_entry(void *, uint32_t, const char *, const char *);
void BB_THISCALL bb_shared_stop_entry(void *);
void BB_THISCALL bb_shared_configure_entry(void *, const char *,
                                                         const char *);
void BB_THISCALL bb_shared_clear_entry(void *);
void BB_THISCALL bb_shared_net_start_entry(void *);
void *BB_THISCALL bb_audio_manager_construct_entry(void *);
uint32_t BB_THISCALL bb_audio_manager_walk_entry(void *);
void BB_THISCALL bb_audio_manager_initialize_entry(void *,
                                                                 uint32_t);
void BB_THISCALL bb_audio_manager_shutdown_entry(void *);
void *BB_THISCALL
bb_audio_resource_create_entry(void *, const char *, uint32_t);
void *BB_THISCALL bb_audio_resource_find_entry(void *,
                                                             const char *);
void *BB_THISCALL bb_audio_asset_construct_entry(void *, void *);
uint32_t BB_THISCALL
bb_audio_asset_load_entry(void *, const char *, uint32_t);
void *BB_THISCALL bb_audio_pcm_create_entry(void *, void *,
                                                          const char *);
uint32_t bb_audio_wav_load_entry(uint32_t, const char *, void *, void *,
                                 void *);
uint32_t bb_audio_buffer_fill_entry(void *, const void *, uint32_t);
uint32_t bb_audio_wave_find_entry(const void *, void *, void *, void *);
void BB_STDCALL
bb_object_array_construct_entry(void *, uint32_t, uint32_t,
                                void(BB_THISCALL *)(void *));
void bb_fatal_teardown_entry(uint32_t);
void BB_THISCALL bb_display_flush_entry(void *);
void BB_THISCALL bb_display_present_wait_entry(void *);
#endif
