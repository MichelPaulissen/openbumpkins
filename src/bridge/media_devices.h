/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#ifndef BUMPKINS_MEDIA_DEVICES_H
#define BUMPKINS_MEDIA_DEVICES_H
#include <stdint.h>
/* Output slots retain the game's packed four-byte object identities, including
 * when a portable C caller supplies native storage for the slot. */
uint32_t BB_STDCALL bb_media_draw_create_entry(const void *,
                                                             void **, void *);
uint32_t BB_STDCALL bb_media_sound_create_entry(const void *,
                                                              void **, void *);
void bb_media_sound_initialize_entry(void *);
uint32_t bb_media_sound_device_entry(void **, void *);
void bb_media_sound_shutdown_entry(void);
uint32_t bb_media_sound_release_entry(void *);
#endif
