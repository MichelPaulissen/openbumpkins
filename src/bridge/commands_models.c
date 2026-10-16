/* SPDX-License-Identifier: GPL-3.0-only */
#include "commands_models.h"
#include "interop.h"
#define A(n) BB_U32(r[3] + 8u + (n) * 4u)
static void pop(uint32_t *r) {
  uint64_t v = bb_command_pop_c((void *)(uintptr_t)A(0), r[5]);
  r[7] = v;
  r[5] = v >> 32;
}
static void collect(uint32_t *r) {
  r[7] = bb_command_collect((void *)(uintptr_t)r[6]);
}
static void ready(uint32_t *r) {
  r[7] = bb_command_ready((void *)(uintptr_t)r[6]);
}
static void tick(uint32_t *r) {
  r[7] = bb_command_tick((void *)(uintptr_t)r[6]);
}
static void pump(uint32_t *r) {
  r[7] = bb_command_pump((void *)(uintptr_t)r[6]);
}
static void dispatch(uint32_t *r) {
  r[7] = bb_command_dispatch((void *)(uintptr_t)r[6],
                             (const void *)(uintptr_t)A(0));
}
static void sync(uint32_t *r) {
  r[7] = bb_command_sync((void *)(uintptr_t)r[6], A(0));
}
const struct bb_model_target *bb_commands_model_find(void *unused,
                                                     uint32_t id) {
  (void)unused;
  static const struct bb_model_target targets[] = {
      {0x401240, pop, 1, 0, 0},   {0x4012b0, collect, 0, 0, 0},
      {0x4012e0, ready, 0, 0, 0}, {0x4012f0, tick, 0, 0, 0},
      {0x401390, pump, 0, 0, 0},  {0x4013d0, dispatch, 1, 0, 0},
      {0x42dc60, sync, 1, 0, 0}};
  for (unsigned n = 0; n < sizeof targets / sizeof *targets; n++)
    if (targets[n].identity == id)
      return &targets[n];
  return NULL;
}
