/* SPDX-License-Identifier: GPL-3.0-only */
#include "session_models.h"
#include "game_calls.h"
#include "interface_platform.h"
#include "session_controls.h"
static uint32_t arg(uint32_t *r, unsigned n, unsigned reserve) {
  return BB_U32(r[3] + reserve + 8u + n * 4u);
}
static void credits(uint32_t *r) {
  (void)r;
  bb_session_credits_entry();
}
static void music_start(uint32_t *r) {
  bb_session_music_start_entry(arg(r, 0, 0));
}
static void music_stop(uint32_t *r) {
  (void)r;
  bb_session_music_stop_entry();
}
static void music_tick(uint32_t *r) {
  (void)r;
  bb_session_music_tick_entry();
}
static void audio_tick(uint32_t *r) {
  (void)r;
  bb_session_audio_tick_entry();
}
static void pause_tick(uint32_t *r) {
  (void)r;
  bb_session_pause_tick_entry();
}
static void disc_prompt(uint32_t *r) {
  (void)r;
  bb_session_disc_prompt_entry();
}
static void disc_check(uint32_t *r) {
  (void)r;
  bb_session_disc_check_entry();
}
static void wait_key(uint32_t *r) {
  r[7] = bb_session_wait_c(arg(r, 0, BB_SESSION_MESSAGE_BYTES), r[3] + 4u);
}
static void escape(uint32_t *r) { r[7] = bb_session_escape_c(r[3] + 4u); }
static void info(uint32_t *r) {
  bb_session_info_c(BB_SPAN(r[3] + 4u, BB_SESSION_INFO_BYTES));
}
static void outro(uint32_t *r) {
  bb_session_outro_c(BB_SPAN(r[3] + 4u, BB_SESSION_OUTRO_BYTES));
}
static void disc_present(uint32_t *r) {
  r[7] = bb_session_disc_present_c(BB_SPAN(r[3] + 4u, BB_SESSION_DISC_BYTES));
}
static void settings(uint32_t *r) { bb_session_settings_c(r[3] + 4u); }
const struct bb_model_target *bb_session_model_find(void *unused, uint32_t id) {
  (void)unused;
  switch (id) {
#define TARGET(i, b, n, bytes)                                                 \
  case i: {                                                                    \
    static const struct bb_model_target t = {i, b, n, bytes, 0};               \
    return &t;                                                                 \
  }
    TARGET(0x42df40u, credits, 0, 0)
    TARGET(0x42e000u, music_start, 1, 0)
    TARGET(0x42e060u, music_stop, 0, 0)
    TARGET(0x42e0a0u, music_tick, 0, 0)
    TARGET(0x42e110u, info, 0, BB_SESSION_INFO_BYTES)
    TARGET(0x42e3a0u, wait_key, 1, BB_SESSION_MESSAGE_BYTES)
    TARGET(0x42e460u, pause_tick, 0, 0)
    TARGET(0x42e620u, outro, 0, BB_SESSION_OUTRO_BYTES)
    TARGET(0x42e7a0u, disc_prompt, 0, 0)
    TARGET(0x42e890u, disc_present, 0, BB_SESSION_DISC_BYTES)
    TARGET(0x42ea30u, disc_check, 0, 0)
    TARGET(0x42ea40u, audio_tick, 0, 0)
    TARGET(0x42eb80u, escape, 0, BB_SESSION_MESSAGE_BYTES)
    TARGET(0x4064b0u, settings, 0, BB_SESSION_SETTINGS_BYTES)
#undef TARGET
  default:
    return NULL;
  }
}
