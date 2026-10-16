/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BUMPKINS_COORDINATOR_H
#define BUMPKINS_COORDINATOR_H
#include <stdint.h>
void bb_frame_update_entry(void);
void bb_interface_tick_entry(void);
void bb_simulation_tick_entry(void);
void bb_screen_to_map_entry(int32_t, int32_t, int32_t *, int32_t *, uint32_t);
void bb_camera_update_entry(void);
void bb_mission_intro_entry(void);
void bb_mission_begin_entry(int32_t);
void bb_mission_export_entry(int32_t);
void bb_world_refresh_entry(void);
void bb_clock_text_entry(uint32_t, char *);
#endif
