/* SPDX-License-Identifier: GPL-3.0-only */
#include "bootstrap_models.h"
#include "game_calls.h"
static void *self(uint32_t *r) { return (void *)(uintptr_t)r[6]; }
static void *ptr(uint32_t p) { return (void *)(uintptr_t)p; }
static uint32_t arg(uint32_t *r, unsigned n, unsigned reserve) {
  return BB_U32(r[3] + reserve + 8u + n * 4u);
}
static void construct(uint32_t *r) {
  r[7] = bb_address_encode(bb_shared_construct_entry(self(r)));
}
static void initialize(uint32_t *r) {
  bb_shared_initialize_entry(self(r), arg(r, 0, 0));
}
static void start(uint32_t *r) {
  r[7] = bb_shared_start_entry(self(r), arg(r, 0, 0));
}
static void reset(uint32_t *r) { bb_shared_reset_entry(self(r)); }
static void create(uint32_t *r) {
  r[7] = bb_shared_create_entry(self(r), arg(r, 0, 0), ptr(arg(r, 1, 0)),
                                ptr(arg(r, 2, 0)));
}
static void stop(uint32_t *r) { bb_shared_stop_entry(self(r)); }
static void configure(uint32_t *r) {
  bb_shared_configure_entry(self(r), ptr(arg(r, 0, 0)), ptr(arg(r, 1, 0)));
}
static void clear(uint32_t *r) { bb_shared_clear_entry(self(r)); }
static void net_start(uint32_t *r) { bb_shared_net_start_entry(self(r)); }
static void manager_construct(uint32_t *r) {
  r[7] = bb_address_encode(bb_audio_manager_construct_entry(self(r)));
}
static void manager_walk(uint32_t *r) {
  r[7] = bb_audio_manager_walk_entry(self(r));
}
static void manager_initialize(uint32_t *r) {
  bb_audio_manager_initialize_entry(self(r), arg(r, 0, 0));
}
static void manager_shutdown(uint32_t *r) {
  bb_audio_manager_shutdown_entry(self(r));
}
static void resource_create(uint32_t *r) {
  r[7] = bb_address_encode(
      bb_audio_resource_create_entry(self(r), ptr(arg(r, 0, 0)), arg(r, 1, 0)));
}
static void resource_find(uint32_t *r) {
  r[7] = bb_address_encode(
      bb_audio_resource_find_c(self(r), ptr(arg(r, 0, 64)), r[3] + 4u));
}
static void asset_construct(uint32_t *r) {
  r[7] = bb_address_encode(
      bb_audio_asset_construct_entry(self(r), ptr(arg(r, 0, 0))));
}
static void asset_load(uint32_t *r) {
  r[7] = bb_audio_asset_load_c(self(r), ptr(arg(r, 0, 64)), arg(r, 1, 64),
                               r[3] + 4u);
}
static void pcm_create(uint32_t *r) {
  r[7] = bb_address_encode(bb_audio_pcm_create_c(
      self(r), ptr(arg(r, 0, 28)), ptr(arg(r, 1, 28)), r[3] + 4u));
}
static void wav_load(uint32_t *r) {
  r[7] = bb_audio_wav_load_entry(arg(r, 0, 0), ptr(arg(r, 1, 0)),
                                 ptr(arg(r, 2, 0)), ptr(arg(r, 3, 0)),
                                 ptr(arg(r, 4, 0)));
}
static void buffer_fill(uint32_t *r) {
  r[7] = bb_audio_buffer_fill_c(ptr(arg(r, 0, 16)), ptr(arg(r, 1, 16)),
                                arg(r, 2, 16), r[3] + 4u);
}
static void wave_find(uint32_t *r) {
  r[7] = bb_audio_wave_find_entry(ptr(arg(r, 0, 0)), ptr(arg(r, 1, 0)),
                                  ptr(arg(r, 2, 0)), ptr(arg(r, 3, 0)));
}
static void array_construct(uint32_t *r) {
  bb_object_array_construct_entry(
      ptr(arg(r, 0, 0)), arg(r, 1, 0), arg(r, 2, 0),
      (void(BB_THISCALL *)(void *))(uintptr_t)arg(r, 3, 0));
}
static void fatal(uint32_t *r) { bb_fatal_teardown_entry(arg(r, 0, 0)); }
static void flush(uint32_t *r) { bb_display_flush_entry(self(r)); }
static void present_wait(uint32_t *r) {
  bb_display_present_wait_entry(self(r));
}
const struct bb_model_target *bb_bootstrap_model_find(void *unused,
                                                      uint32_t id) {
  (void)unused;
  switch (id) {
#define TARGET(id, body, words, reserve)                                       \
  case id: {                                                                   \
    static const struct bb_model_target t = {id, body, words, reserve, 0};     \
    return &t;                                                                 \
  }
    TARGET(0x00401000u, construct, 0, 0)
    TARGET(0x00401040u, initialize, 1, 0)
    TARGET(0x00401070u, start, 1, 0)
    TARGET(0x004010b0u, reset, 0, 0)
    TARGET(0x004010e0u, create, 3, 0)
    TARGET(0x004010f0u, stop, 0, 0)
    TARGET(0x00401110u, configure, 2, 0)
    TARGET(0x004011a0u, clear, 0, 0)
    TARGET(0x004011d0u, net_start, 0, 0)
    TARGET(0x00401db0u, manager_construct, 0, 0)
    TARGET(0x00401dc0u, manager_walk, 0, 0)
    TARGET(0x00401dd0u, manager_initialize, 1, 0)
    TARGET(0x00401ec0u, manager_shutdown, 0, 0)
    TARGET(0x00401ef0u, resource_create, 2, 0)
    TARGET(0x00401f50u, resource_find, 1, 64)
    TARGET(0x00401fd0u, asset_construct, 1, 0)
    TARGET(0x00402000u, asset_load, 2, 64)
    TARGET(0x00402410u, pcm_create, 2, 28)
    TARGET(0x00402520u, wav_load, 5, 0)
    TARGET(0x00402600u, buffer_fill, 3, 16)
    TARGET(0x004026c0u, wave_find, 4, 0)
    TARGET(0x004027b0u, array_construct, 4, 0)
    TARGET(0x00403a40u, fatal, 1, 0)
    TARGET(0x004337f0u, flush, 0, 0)
    TARGET(0x00433830u, present_wait, 0, 0)
#undef TARGET
  default:
    return NULL;
  }
}
