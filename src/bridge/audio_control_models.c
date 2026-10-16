/* SPDX-License-Identifier: GPL-3.0-only */
#include "game_calls.h"
#include "interface_platform.h"
#include "session_models.h"
#include "game_audio_models.h"
static uint32_t arg(uint32_t *r, unsigned n, unsigned reserve) {
  return BB_U32(r[3] + reserve + 8u + n * 4u);
}
static void *self(uint32_t *r) { return (void *)(uintptr_t)r[6]; }
static void audio_start(uint32_t *r) {
  bb_interface_audio_start_entry(self(r));
}
static void audio_stop(uint32_t *r) { bb_interface_audio_stop_entry(self(r)); }
static void sound_disable(uint32_t *r) {
  bb_interface_sound_disable_entry(self(r));
}
static void sound_enable(uint32_t *r) {
  bb_interface_sound_enable_entry(self(r));
}
static void cd_play(uint32_t *r) {
  uint32_t object = r[6], track = arg(r, 0, BB_CD_COMMAND_BYTES);
  bb_cd_play_c(object, (int32_t)track, r[3] + 4u);
  r[6] = object;
  r[7] = track;
}
static void cd_stop(uint32_t *r) { r[7] = bb_cd_stop_c(r[6], r[3] + 4u); }
static void cd_resume(uint32_t *r) {
  uint32_t object = r[6], track = BB_U32(object + 0xac34u);
  if (track)
    BB_GAME_CALL(0x469fdfu, object, track);
  r[7] = track ? track : object;
  r[6] = object;
}
const struct bb_model_target *bb_audio_control_model_find(void *unused,
                                                          uint32_t id) {
  (void)unused;
  switch (id) {
#define TARGET(i, b, n, bytes)                                                 \
  case i: {                                                                    \
    static const struct bb_model_target t = {i, b, n, bytes, 0};               \
    return &t;                                                                 \
  }
    TARGET(0x401e30u, audio_start, 0, 0)
    TARGET(0x401e60u, audio_stop, 0, 0)
    TARGET(0x401e80u, sound_disable, 0, 0)
    TARGET(0x401ea0u, sound_enable, 0, 0)
    TARGET(0x469fdfu, cd_play, 1, BB_CD_COMMAND_BYTES)
    TARGET(0x46a098u, cd_stop, 0, BB_CD_COMMAND_BYTES)
    TARGET(0x46a12fu, cd_resume, 0, 4)
#undef TARGET
  default:
    return bb_game_audio_model_find(NULL, id);
  }
}
