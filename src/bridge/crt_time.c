/* SPDX-License-Identifier: GPL-3.0-only */
#include "crt_time.h"
#include "abi.h"
#include "crt_debug_heap.h"
#include "crt_locale.h"
#include "crt_primitives.h"
#include "interop.h"
#include <stddef.h>
#ifdef BB_MODEL_RUNTIME
#include "game_calls.h"
#include "model_buffer.h"
static uint32_t time_platform(uint32_t id, void *output, size_t bytes) {
  struct bb_model_buffer buffer;
  bb_model_buffer_begin(&buffer, output, bytes);
  uint32_t result = BB_GAME_CALL(id, 0, buffer.reference);
  bb_model_buffer_end(&buffer, 1);
  return result;
}
#endif
#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
#define WIN BB_STDCALL
static int32_t signed_word(uint32_t x) { return (int32_t)x; }
int32_t *bb_crt_time_gmtime_entry(const int32_t *clock) {
  int32_t seconds = BB_I32(clock);
  if (seconds < 0)
    return NULL;
  int32_t cycle = seconds / 126230400, remainder = seconds % 126230400,
          year = cycle * 4 + 70;
  unsigned leap = 0;
  if (remainder >= 31536000) {
    ++year;
    remainder -= 31536000;
    if (remainder >= 31536000) {
      ++year;
      remainder -= 31536000;
      if (remainder < 31622400)
        leap = 1;
      else {
        ++year;
        remainder -= 31622400;
      }
    }
  }
  BB_I32(0x48cc34) = year;
  BB_I32(0x48cc3c) = remainder / 86400;
  uintptr_t days = leap ? 0x48cfd8 : 0x48d010;
  uint32_t month = 1;
  while (BB_I32(days + month * 4) < BB_I32(0x48cc3c))
    ++month;
  BB_U32(0x48cc30) = month - 1;
  BB_U32(0x48cc2c) = BB_U32(0x48cc3c) - BB_U32(days + BB_U32(0x48cc30) * 4);
  BB_I32(0x48cc38) = (BB_I32(clock) / 86400 + 4) % 7;
  BB_I32(0x48cc28) = (remainder % 86400) / 3600;
  BB_I32(0x48cc24) = (remainder % 3600) / 60;
  BB_I32(0x48cc20) = remainder % 60;
  BB_U32(0x48cc40) = 0;
  return (int32_t *)0x48cc20;
}
void bb_crt_time_transition_entry(int32_t start, int32_t relative,
                                  uint32_t year, int32_t month, int32_t week,
                                  int32_t weekday, int32_t day, int32_t hour,
                                  int32_t minute, int32_t second,
                                  int32_t millis) {
  uintptr_t table = year & 3 ? 0x48d00c : 0x48cfd4;
  uint32_t first = BB_U32(table + (uint32_t)month * 4), date;
  if (relative == 1) {
    int32_t dow = signed_word((year - 70) * 365 + ((int32_t)(year - 1) >> 2) -
                              13 + first + 1) %
                  7;
    uint32_t weeks = (uint32_t)week;
    if (dow < weekday)
      --weeks;
    date = first + 1 + (uint32_t)weekday - (uint32_t)dow + weeks * 7;
    if (week == 5 && BB_I32(table + (uint32_t)month * 4 + 4) < (int32_t)date)
      date -= 7;
  } else
    date = first + (uint32_t)day;
  uint32_t time =
      (((uint32_t)hour * 60 + (uint32_t)minute) * 60 + (uint32_t)second) *
          1000 +
      (uint32_t)millis;
  if (start == 1) {
    BB_U32(0x48e2ac) = date;
    BB_U32(0x48e2b0) = time;
    BB_U32(0x48e2a8) = year;
  } else {
    BB_U32(0x48e2bc) = date;
    BB_U32(0x48e2c0) = time + BB_U32(0x48e210) * 1000;
    if (BB_I32(0x48e2c0) < 0)
      BB_U32(0x48e2c0) += 86399999;
    else if (BB_I32(0x48e2c0) >= 86400000)
      BB_U32(0x48e2c0) -= 86399999;
    BB_U32(0x48e2b8) = year;
  }
}
static void platform_transition(int32_t start, uint32_t year, uintptr_t p) {
  bb_crt_time_transition_entry(
      start, !WORD(p), year, WORD(p + 2), WORD(p) ? 0 : WORD(p + 6),
      WORD(p) ? 0 : WORD(p + 4), WORD(p) ? WORD(p + 6) : 0, WORD(p + 8),
      WORD(p + 10), WORD(p + 12), WORD(p + 14));
}
int32_t bb_crt_time_daylight_entry(const int32_t *time) {
  if (!BB_U32(0x48e20c))
    return 0;
  if (BB_U32((uintptr_t)time + 20) != BB_U32(0x48e2a8) ||
      BB_U32((uintptr_t)time + 20) != BB_U32(0x48e2b8)) {
    if (!BB_U32(0x6c89d8)) {
      bb_crt_time_transition_entry(1, 1, BB_U32((uintptr_t)time + 20), 4, 1, 0,
                                   0, 2, 0, 0, 0);
      bb_crt_time_transition_entry(0, 1, BB_U32((uintptr_t)time + 20), 10, 5, 0,
                                   0, 2, 0, 0, 0);
    } else {
      platform_transition(1, BB_U32((uintptr_t)time + 20), 0x6c8a78);
      platform_transition(0, BB_U32((uintptr_t)time + 20), 0x6c8a24);
    }
  }
  int32_t yday = BB_I32((uintptr_t)time + 28), start = BB_I32(0x48e2ac),
          end = BB_I32(0x48e2bc);
  if (start < end) {
    if (yday < start || end < yday)
      return 0;
    if (start < yday && yday < end)
      return 1;
  } else {
    if (yday < end || start < yday)
      return 1;
    if (end < yday && yday < start)
      return 0;
  }
  int32_t millis =
      signed_word((BB_U32((uintptr_t)time + 8) * 3600 +
                   BB_U32((uintptr_t)time + 4) * 60 + BB_U32(time)) *
                  1000);
  return yday == BB_I32(0x48e2ac) ? millis >= BB_I32(0x48e2b0)
                                  : millis < BB_I32(0x48e2c0);
}
void bb_crt_time_set_zone_entry(void) {
  BB_U32(0x6c89d8) = 0;
  BB_U32(0x48e2b8) = UINT32_MAX;
  BB_U32(0x48e2a8) = BB_U32(0x48e2b8);
  const char *text = bb_crt_locale_getenv_entry((void *)0x4834e8);
  if (!text) {
#ifdef BB_MODEL_RUNTIME
    if (BB_GAME_CALL(BB_U32(0x6ca468), 0, 0x6c89e0)) {
#else
    if (((uint32_t(WIN *)(void *))BB_CODE(BB_POINTER(0x6ca468)))(
            (void *)0x6c89e0)) {
#endif
      BB_U32(0x6c89d8) = 1;
      BB_U32(0x48e208) = BB_U32(0x6c89e0) * 60;
      if (WORD(0x6c8a26))
        BB_U32(0x48e208) += BB_U32(0x6c8a34) * 60;
      if (!WORD(0x6c8a7a) || !BB_U32(0x6c8a88)) {
        BB_U32(0x48e20c) = 0;
        BB_U32(0x48e210) = 0;
      } else {
        BB_U32(0x48e20c) = 1;
        BB_U32(0x48e210) = (BB_U32(0x6c8a88) - BB_U32(0x6c8a34)) * 60;
      }
      bb_crt_locale_wcstombs_entry(BB_POINTER(0x48e298), (void *)0x6c89e4, 64);
      bb_crt_locale_wcstombs_entry(BB_POINTER(0x48e29c), (void *)0x6c8a38, 64);
      BYTE((uintptr_t)BB_POINTER(0x48e29c) + 63) = 0;
      BYTE((uintptr_t)BB_POINTER(0x48e298) + 63) =
          BYTE((uintptr_t)BB_POINTER(0x48e29c) + 63);
    }
    return;
  }
  if (!BYTE(text) || (BB_U32(0x48e2a0) &&
                      !bb_crt_string_compare_entry(text, BB_POINTER(0x48e2a0))))
    return;
  bb_crt_free_debug_entry(BB_POINTER(0x48e2a0), 2);
  BB_SET_POINTER(0x48e2a0,
                 bb_crt_malloc_debug_entry(bb_crt_string_length_entry(text) + 1,
                                           2, (void *)0x4834e0, 236));
  if (!BB_U32(0x48e2a0))
    return;
  bb_crt_string_copy_entry(BB_POINTER(0x48e2a0), text);
  bb_crt_string_copy_count_entry(BB_POINTER(0x48e298), text, 3);
  BYTE((uintptr_t)BB_POINTER(0x48e298) + 3) = 0;
  text += 3;
  int negative = BYTE(text) == '-';
  if (negative)
    ++text;
  BB_U32(0x48e208) = (uint32_t)bb_crt_string_long_entry(text) * 3600;
  while (BYTE(text) == '+' || (BYTE(text) >= '0' && BYTE(text) <= '9'))
    ++text;
  if (BYTE(text) == ':') {
    ++text;
    BB_U32(0x48e208) += (uint32_t)bb_crt_string_long_entry(text) * 60;
    while (BYTE(text) >= '0' && BYTE(text) <= '9')
      ++text;
    if (BYTE(text) == ':') {
      ++text;
      BB_U32(0x48e208) += (uint32_t)bb_crt_string_long_entry(text);
      while (BYTE(text) >= '0' && BYTE(text) <= '9')
        ++text;
    }
  }
  if (negative)
    BB_U32(0x48e208) = 0u - BB_U32(0x48e208);
  BB_I32(0x48e20c) = (int8_t)BYTE(text);
  if (!BB_U32(0x48e20c))
    BYTE(BB_U32(0x48e29c)) = 0;
  else {
    bb_crt_string_copy_count_entry(BB_POINTER(0x48e29c), text, 3);
    BYTE((uintptr_t)BB_POINTER(0x48e29c) + 3) = 0;
  }
}
void bb_crt_time_initialize_zone_entry(void) {
  if (!BB_U32(0x48e2c4)) {
    bb_crt_time_set_zone_entry();
    BB_U32(0x48e2c4) += 1;
  }
}
/* The old local converter sets year/month/hour/yday, but reads inherited
 * minute/second slots on transition days. Keep those slots explicit while
 * original binary callers and their stack layouts remain. */
static int32_t local_time(int32_t year, int32_t month, int32_t day,
                          int32_t hour, int32_t minute, int32_t second,
                          int32_t daylight, int32_t inherited_second,
                          int32_t inherited_minute) {
  uint32_t tm_year = (uint32_t)year - 1900;
  if ((int32_t)tm_year < 70 || (int32_t)tm_year > 138)
    return -1;
  uint32_t yday = BB_U32(0x48d00c + (uint32_t)month * 4) + (uint32_t)day;
  if (!(tm_year & 3) && month > 2)
    ++yday;
  uint32_t seconds = ((((tm_year - 70) * 365 +
                        (uint32_t)((int32_t)(tm_year - 1) >> 2) - 17 + yday) *
                           24 +
                       (uint32_t)hour) *
                          60 +
                      (uint32_t)minute) *
                         60 +
                     (uint32_t)second;
  bb_crt_time_initialize_zone_entry();
  seconds += BB_U32(0x48e208);
  int32_t time[9] = {inherited_second,
                     inherited_minute,
                     hour,
                     0,
                     signed_word((uint32_t)month - 1),
                     tm_year,
                     0,
                     yday,
                     0};
  if (daylight == 1 ||
      (daylight == -1 && BB_U32(0x48e20c) && bb_crt_time_daylight_entry(time)))
    seconds += BB_U32(0x48e210);
  return seconds;
}
void __attribute__((used, noinline)) bb_crt_time_local_c(uint32_t *r) {
  uintptr_t a = r[3] + 136;
  r[7] = local_time(BB_I32(a), BB_I32(a + 4), BB_I32(a + 8), BB_I32(a + 12),
                    BB_I32(a + 16), BB_I32(a + 20), BB_I32(a + 24),
                    BB_I32(r[3] + 88), BB_I32(r[3] + 92));
}
void __attribute__((used, noinline)) bb_crt_time_now_c(uint32_t *r) {
  uint16_t local[8], system[8];
  uint8_t zone[172];
#ifdef BB_MODEL_RUNTIME
  time_platform(BB_U32(0x6ca460), local, sizeof local);
  time_platform(BB_U32(0x6ca470), system, sizeof system);
#else
  ((void(WIN *)(void *))BB_CODE(BB_POINTER(0x6ca460)))(local);
  ((void(WIN *)(void *))BB_CODE(BB_POINTER(0x6ca470)))(system);
#endif
  int32_t daylight;
  if (system[5] == WORD(0x6c886a) && system[4] == WORD(0x6c8868) &&
      system[3] == WORD(0x6c8866) && system[1] == WORD(0x6c8862) &&
      system[0] == WORD(0x6c8860))
    daylight = BB_I32(0x6c8858);
  else {
#ifdef BB_MODEL_RUNTIME
    uint32_t status = time_platform(BB_U32(0x6ca468), zone, sizeof zone);
#else
    uint32_t status =
        ((uint32_t(WIN *)(void *))BB_CODE(BB_POINTER(0x6ca468)))(zone);
#endif
    daylight = status == 2 ? 1 : status == 1 ? 0 : -1;
    BB_U32(0x6c8858) = daylight;
    for (unsigned n = 0; n < 8; ++n)
      WORD(0x6c8860 + n * 2) = system[n];
  }
  BB_U32(0x6c8858) = daylight;
  int32_t seconds =
      local_time(local[0], local[1], local[3], local[4], local[5], local[6],
                 daylight, BB_I32(r[3] + 208), BB_I32(r[3] + 212));
  uintptr_t a = r[3] + 520;
  if (BB_U32(a))
    BB_I32(BB_U32(a)) = seconds;
  r[7] = seconds;
}
#ifdef BB_MODEL_RUNTIME
int32_t bb_crt_time_local_entry(int32_t a, int32_t b, int32_t c, int32_t d,
                                int32_t e, int32_t f, int32_t g) {
  return BB_GAME_CALL(0x472aa0, 0, a, b, c, d, e, f, g);
}
int32_t bb_crt_time_now_entry(int32_t *out) {
  struct bb_model_buffer buffer;
  bb_model_buffer_begin(&buffer, out, out ? 4 : 0);
  int32_t result = BB_GAME_CALL(0x46c760, 0, buffer.reference);
  bb_model_buffer_end(&buffer, 1);
  return result;
}
#else
#ifdef _WIN32
#define ASM_NAME(n) "_" #n
#else
#define ASM_NAME(n) #n
#endif
#define UNUSED __attribute__((unused))
int32_t __attribute__((naked, noinline))
bb_crt_time_local_entry(int32_t a UNUSED, int32_t b UNUSED, int32_t c UNUSED,
                        int32_t d UNUSED, int32_t e UNUSED, int32_t f UNUSED,
                        int32_t g UNUSED) {
  __asm__ volatile(
      "lea -128(%esp),%esp;pushfl;pushal;cld;pushl %esp;call " ASM_NAME(
          bb_crt_time_local_c) ";addl $4,%esp;popal;popfl;lea "
                               "128(%esp),%esp;ret");
}
int32_t __attribute__((naked, noinline))
bb_crt_time_now_entry(int32_t *out UNUSED) {
  __asm__ volatile(
      "lea -512(%esp),%esp;pushfl;pushal;cld;pushl %esp;call " ASM_NAME(
          bb_crt_time_now_c) ";addl $4,%esp;popal;popfl;lea "
                             "512(%esp),%esp;ret");
}

#endif
