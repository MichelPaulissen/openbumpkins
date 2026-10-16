/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BUMPKINS_SESSION_CONTROLS_H
#define BUMPKINS_SESSION_CONTROLS_H
#include <stdint.h>
void bb_session_credits_entry(void);
void bb_session_music_start_entry(uint32_t);
void bb_session_music_stop_entry(void);
void bb_session_music_tick_entry(void);
void bb_session_info_entry(void);
uint16_t bb_session_wait_entry(uint32_t);
void bb_session_pause_tick_entry(void);
void bb_session_outro_entry(void);
void bb_session_disc_prompt_entry(void);
uint32_t bb_session_disc_present_entry(void);
void bb_session_disc_check_entry(void);
void bb_session_audio_tick_entry(void);
uint32_t bb_session_escape_entry(void);
void bb_session_settings_entry(void);
#endif
