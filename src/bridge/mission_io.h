/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#ifndef BUMPKINS_MISSION_IO_H
#define BUMPKINS_MISSION_IO_H
#include <stdint.h>

uint32_t bb_mission_save_entry(const char *, const char *);
void bb_mission_refresh_entry(void);
uint32_t bb_mission_read_entry(void *, uint32_t, uint32_t, void *);
uint32_t bb_mission_decode_entry(void *, uint32_t, uint32_t, void *);
uint32_t bb_mission_load_entry(const char *, uint32_t);
void BB_THISCALL bb_panel_write_entry(void *, void *);
void BB_THISCALL bb_panel_read_entry(void *, void *);
#endif
