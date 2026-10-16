/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#ifndef BUMPKINS_SELECTION_H
#define BUMPKINS_SELECTION_H
#include <stdint.h>
void bb_selection_action_entry(void);
uint32_t BB_THISCALL bb_selection_help_entry(void *, int32_t,
                                                           int32_t, char *);
void BB_THISCALL bb_tooltip_tick_entry(void *);
void BB_THISCALL bb_tooltip_visibility_entry(void *, uint32_t);
void BB_THISCALL bb_tooltip_reset_entry(void *);
#endif
