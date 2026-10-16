/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#ifndef BUMPKINS_STARTUP_H
#define BUMPKINS_STARTUP_H
#include <stdint.h>
extern uint32_t bb_startup_skip_intro;
uint32_t bb_game_clock_sample(void);
int32_t BB_STDCALL bb_game_main_entry(void *, void *,
                                                    const char *, int32_t);
uint32_t bb_game_window_create_entry(void *, int32_t);
#endif
