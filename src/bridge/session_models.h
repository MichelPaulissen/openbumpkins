/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_SESSION_MODELS_H
#define BB_SESSION_MODELS_H
#include "model_runtime.h"
enum {
  BB_SESSION_MESSAGE_BYTES = 28,
  BB_SESSION_INFO_BYTES = 0x2104,
  BB_SESSION_OUTRO_BYTES = 0x1104,
  BB_SESSION_DISC_BYTES = 0xc10,
  BB_SESSION_SETTINGS_BYTES = 0x210,
  BB_CD_COMMAND_BYTES = 0x804
};
struct text_frame;
struct disc_frame;
uint16_t bb_session_wait_c(uint32_t, uint32_t);
uint32_t bb_session_escape_c(uint32_t);
void bb_session_info_c(struct text_frame *);
void bb_session_outro_c(struct text_frame *);
uint32_t bb_session_disc_present_c(struct disc_frame *);
void bb_session_settings_c(uint32_t);
void bb_cd_play_c(uint32_t, int32_t, uint32_t);
uint32_t bb_cd_stop_c(uint32_t, uint32_t);
const struct bb_model_target *bb_session_model_find(void *, uint32_t);
const struct bb_model_target *bb_audio_control_model_find(void *, uint32_t);
#endif
