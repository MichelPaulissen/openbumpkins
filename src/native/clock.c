/* SPDX-License-Identifier: GPL-3.0-only */
#include "native.h"
#include <string.h>
#include <time.h>
/* The game subtracts 27 before comparing unsigned timestamps. */
static unsigned fast_tick = 1000;
U32 W bb_tick(void) {
  if (bb_fast)
    return __atomic_add_fetch(&fast_tick, 16, __ATOMIC_RELAXED);
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (U32)(t.tv_sec * 1000 + t.tv_nsec / 1000000);
}
static U32 W GetTickCount(void) { return bb_tick(); }
static void W Sleep(U32 milliseconds) {
  if (!bb_fast)
    SDL_Delay(milliseconds);
}
static int W QueryPerformanceCounter(void *output) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  int64_t value = (int64_t)t.tv_sec * 1000000000 + t.tv_nsec;
  memcpy(output, &value, 8);
  return 1;
}
static int W QueryPerformanceFrequency(void *output) {
  int64_t value = 1000000000;
  memcpy(output, &value, 8);
  return 1;
}
void *bb_clock_api(const char *name) {
  static const struct ApiEntry table[] = {
      API_ENTRY(GetTickCount), API_ENTRY(Sleep),
      API_ENTRY(QueryPerformanceCounter), API_ENTRY(QueryPerformanceFrequency)};
  API_RETURN(table);
}
#ifdef BB_MODEL_RUNTIME
#include "clock_models.inc"
#endif
