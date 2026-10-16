/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#ifndef BUMPKINS_COMMAND_BUILDERS_H
#define BUMPKINS_COMMAND_BUILDERS_H
#include <stdint.h>
void BB_THISCALL bb_economy_set_entry(void *, uint32_t, double);
uint32_t BB_THISCALL bb_command_walk_entry(void *, uint32_t,
                                                         uint32_t);
uint32_t BB_THISCALL bb_command_target_entry(void *, uint32_t);
uint32_t BB_THISCALL bb_command_build_entry(void *, uint32_t,
                                                          uint32_t, uint32_t,
                                                          uint32_t, uint32_t);
uint32_t BB_THISCALL bb_command_terrain_entry(void *, uint32_t,
                                                            uint32_t, uint32_t);
uint32_t BB_THISCALL bb_command_chat_entry(void *, const char *);
uint32_t BB_THISCALL
bb_command_relationship_entry(void *, uint32_t, uint32_t);
uint32_t BB_THISCALL bb_command_pause_entry(void *);
uint32_t BB_THISCALL bb_command_resume_entry(void *);
uint32_t BB_THISCALL bb_command_sync_request_entry(void *);
uint32_t BB_THISCALL bb_command_city_name_entry(void *, uint32_t,
                                                              const char *);
uint32_t BB_THISCALL bb_command_activity_entry(void *, uint32_t,
                                                             uint32_t);
uint32_t BB_THISCALL bb_command_spell_entry(void *, uint32_t,
                                                          uint32_t, uint32_t);
uint32_t BB_THISCALL bb_command_stat_entry(void *, uint32_t,
                                                         uint32_t);
#endif
