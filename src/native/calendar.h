/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_CALENDAR_H
#define BB_CALENDAR_H
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

/* Signed Gregorian conversion, independent of a host CRT's supported years.
 * Day overflow is intentional: the game accepts non-leap February 29. */
static inline int64_t bb_calendar_seconds(const struct tm *t) {
  int64_t y = (int64_t)t->tm_year + 1900, month = t->tm_mon;
  int64_t carry = month / 12;
  if (month % 12 < 0)
    --carry;
  y += carry;
  unsigned m = (unsigned)(month - carry * 12) + 1;
  y -= m <= 2;
  int64_t era = (y >= 0 ? y : y - 399) / 400;
  unsigned yo = (unsigned)(y - era * 400);
  unsigned day = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5;
  int64_t days = era * 146097 + yo * 365 + yo / 4 - yo / 100 + day - 719468;
  return (days + t->tm_mday - 1) * 86400 +
         (int64_t)t->tm_hour * 3600 + t->tm_min * 60 + t->tm_sec;
}
static inline int bb_calendar_utc(int64_t seconds, struct tm *t) {
  int64_t days = seconds / 86400, rest = seconds % 86400;
  if (rest < 0) {
    rest += 86400;
    --days;
  }
  int64_t z = days + 719468;
  int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  unsigned d = (unsigned)(z - era * 146097);
  unsigned yo = (d - d / 1460 + d / 36524 - d / 146096) / 365;
  int64_t year = era * 400 + yo;
  unsigned yd = d - (365 * yo + yo / 4 - yo / 100);
  unsigned mp = (5 * yd + 2) / 153;
  unsigned day = yd - (153 * mp + 2) / 5 + 1;
  unsigned month = mp < 10 ? mp + 3 : mp - 9;
  year += month <= 2;
  if (year - 1900 < INT_MIN || year - 1900 > INT_MAX)
    return 0;
  memset(t, 0, sizeof(*t));
  t->tm_year = (int)(year - 1900);
  t->tm_mon = (int)month - 1;
  t->tm_mday = (int)day;
  t->tm_hour = (int)(rest / 3600);
  t->tm_min = (int)(rest / 60 % 60);
  t->tm_sec = (int)(rest % 60);
  t->tm_wday = (int)((days % 7 + 11) % 7);
  struct tm january = {.tm_year = t->tm_year, .tm_mday = 1};
  t->tm_yday = (int)(days - bb_calendar_seconds(&january) / 86400);
  return 1;
}
#endif
