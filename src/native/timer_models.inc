/* SPDX-License-Identifier: GPL-3.0-only */
#include "../bridge/address.h"
#include "timer_models.h"
static uint32_t timer_arg(uint32_t *r, unsigned n) {
  return BB_U32(r[3] + 8u + n * 4u);
}
#define A(n) timer_arg(r, n)
static void model_set(uint32_t *r) {
  r[7] = timeSetEvent(A(0), A(1), A(2), A(3), A(4));
}
static void model_kill(uint32_t *r) { r[7] = timeKillEvent(A(0)); }
static void model_begin(uint32_t *r) { r[7] = timeBeginPeriod(A(0)); }
static void model_end(uint32_t *r) { r[7] = timeEndPeriod(A(0)); }
static void model_time(uint32_t *r) { r[7] = timeGetTime(); }
static void model_caps(uint32_t *r) {
  r[7] = timeGetDevCaps(A(0) && A(1) >= 8 ? BB_SPAN(A(0), 8) : NULL, A(1));
}
#undef A
static const struct {
  const char *name;
  struct bb_model_target target;
} timer_targets[] = {
    {"timeSetEvent", {BB_TIMER_MODEL_API, model_set, 5, 0, 0}},
    {"timeKillEvent", {BB_TIMER_MODEL_API + 4, model_kill, 1, 0, 0}},
    {"timeBeginPeriod", {BB_TIMER_MODEL_API + 8, model_begin, 1, 0, 0}},
    {"timeEndPeriod", {BB_TIMER_MODEL_API + 12, model_end, 1, 0, 0}},
    {"timeGetTime", {BB_TIMER_MODEL_API + 16, model_time, 0, 0, 0}},
    {"timeGetDevCaps", {BB_TIMER_MODEL_API + 20, model_caps, 2, 0, 0}}};
uint32_t bb_timer_model_export(const char *name) {
  for (unsigned i = 0; i < sizeof(timer_targets) / sizeof(*timer_targets); ++i)
    if (!strcmp(name, timer_targets[i].name))
      return timer_targets[i].target.identity;
  return 0;
}
const struct bb_model_target *bb_timer_model_find(void *unused, uint32_t id) {
  (void)unused;
  for (unsigned i = 0; i < sizeof(timer_targets) / sizeof(*timer_targets); ++i)
    if (id == timer_targets[i].target.identity)
      return &timer_targets[i].target;
  return NULL;
}
