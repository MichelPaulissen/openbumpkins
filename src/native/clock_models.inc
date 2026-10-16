/* SPDX-License-Identifier: GPL-3.0-only */
#include "../bridge/interop.h"
#include "clock_models.h"
static uint32_t clock_arg(uint32_t *r) { return BB_U32(r[3] + 8u); }
static void model_tick(uint32_t *r) { r[7] = GetTickCount(); }
static void model_sleep(uint32_t *r) { Sleep(clock_arg(r)); }
static void model_counter(uint32_t *r) {
  r[7] = QueryPerformanceCounter(BB_SPAN(clock_arg(r), 8));
}
static void model_frequency(uint32_t *r) {
  r[7] = QueryPerformanceFrequency(BB_SPAN(clock_arg(r), 8));
}
static const struct bb_model_target clock_targets[] = {
    {BB_CLOCK_MODEL_API, model_tick, 0, 0, 0},
    {BB_CLOCK_MODEL_API + 4, model_sleep, 1, 0, 0},
    {BB_CLOCK_MODEL_API + 8, model_counter, 1, 0, 0},
    {BB_CLOCK_MODEL_API + 12, model_frequency, 1, 0, 0}};
uint32_t bb_clock_model_export(const char *name) {
  static const char *const names[] = {"GetTickCount", "Sleep",
                                      "QueryPerformanceCounter",
                                      "QueryPerformanceFrequency"};
  for (unsigned i = 0; i < sizeof(names) / sizeof(*names); ++i)
    if (!strcmp(name, names[i]))
      return clock_targets[i].identity;
  return 0;
}
const struct bb_model_target *bb_clock_model_find(void *unused, uint32_t id) {
  (void)unused;
  for (unsigned i = 0; i < sizeof(clock_targets) / sizeof(*clock_targets); ++i)
    if (id == clock_targets[i].identity)
      return clock_targets + i;
  return NULL;
}
