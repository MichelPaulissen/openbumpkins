/* SPDX-License-Identifier: GPL-3.0-only */
/* Keep calendar discovery independent of the original CRT's 32-bit time_t. */
#define _FILE_OFFSET_BITS 64
#define _TIME_BITS 64
#include "native.h"
#include <string.h>
#include <time.h>
#ifdef _WIN32
U32 bb_time_zone_information(int64_t seconds, void *output) {
  (void)seconds;
  return output ? bb_host_timezone(output) : UINT32_MAX;
}
#else

/* TIME_ZONE_INFORMATION uses Windows UTF-16 and 32-bit minute biases. */
struct ZoneInformation {
  I32 bias;
  U16 standard_name[32], standard_date[8];
  I32 standard_bias;
  U16 daylight_name[32], daylight_date[8];
  I32 daylight_bias;
};
_Static_assert(sizeof(struct ZoneInformation) == 172, "Windows time-zone layout");

struct ZoneState {
  long offset;
  int daylight;
  char name[32];
};
struct ZoneYear {
  struct ZoneState standard, daylight;
  U16 standard_date[8], daylight_date[8];
  unsigned states, standard_changes, daylight_changes;
  int irregular;
};

static int state_at(time_t seconds, struct ZoneState *state, struct tm *local) {
  if (!localtime_r(&seconds, local))
    return 0;
  state->offset = local->tm_gmtoff;
  state->daylight = local->tm_isdst > 0;
  const char *name = local->tm_zone ? local->tm_zone : "";
  size_t length = strnlen(name, sizeof(state->name) - 1);
  memcpy(state->name, name, length);
  state->name[length] = 0;
  return 1;
}
static int same_state(const struct ZoneState *a, const struct ZoneState *b) {
  return a->offset == b->offset && a->daylight == b->daylight;
}
static void remember_state(struct ZoneYear *result,
                           const struct ZoneState *state) {
  unsigned bit = 1u << state->daylight;
  struct ZoneState *slot = state->daylight ? &result->daylight : &result->standard;
  if ((result->states & bit) && slot->offset != state->offset)
    result->irregular = 1;
  *slot = *state;
  result->states |= bit;
}
static void transition_date(time_t at, long before_offset, U16 date[8]) {
  /* The Windows transition is expressed in the local time BEFORE the change. */
  time_t wall = at + before_offset;
  struct tm broken;
  gmtime_r(&wall, &broken);
  date[0] = (U16)(broken.tm_year + 1900);
  date[1] = (U16)(broken.tm_mon + 1);
  date[2] = (U16)broken.tm_wday;
  date[3] = (U16)broken.tm_mday;
  date[4] = (U16)broken.tm_hour;
  date[5] = (U16)broken.tm_min;
  date[6] = (U16)broken.tm_sec;
  date[7] = 0;
}
static int zone_year(int year, struct ZoneYear *result) {
  memset(result, 0, sizeof(*result));
  struct tm january = {.tm_year = year - 1900, .tm_mday = 1};
  time_t first = timegm(&january) - 2 * 86400;
  january = (struct tm){.tm_year = year - 1899, .tm_mday = 1};
  time_t last = timegm(&january) + 2 * 86400;
  struct ZoneState before;
  struct tm local;
  if (!state_at(first, &before, &local))
    return 0;
  for (time_t next = first + 21600; next <= last; next += 21600) {
    struct ZoneState after;
    if (!state_at(next, &after, &local))
      return 0;
    if (local.tm_year == year - 1900)
      remember_state(result, &after);
    if (!same_state(&before, &after)) {
      time_t low = next - 21600, high = next;
      while (high - low > 1) {
        time_t middle = low + (high - low) / 2;
        struct ZoneState candidate;
        if (!state_at(middle, &candidate, &local))
          return 0;
        if (same_state(&before, &candidate))
          low = middle;
        else
          high = middle;
      }
      U16 date[8];
      transition_date(high, before.offset, date);
      if (date[0] == year) {
        if (before.daylight == after.daylight)
          result->irregular = 1;
        else if (after.daylight) {
          memcpy(result->daylight_date, date, sizeof(date));
          ++result->daylight_changes;
        } else {
          memcpy(result->standard_date, date, sizeof(date));
          ++result->standard_changes;
        }
      }
    }
    before = after;
  }
  return 1;
}
static int paired(const struct ZoneYear *zone) {
  return zone->states == 3 && !zone->irregular &&
         zone->standard_changes == 1 && zone->daylight_changes == 1;
}
static void relative_date(const U16 absolute[8], U16 relative[8]) {
  static const U16 days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  unsigned year = absolute[0], month = absolute[1];
  unsigned length = days[month - 1] +
                    (month == 2 && !(year % 4) && (year % 100 || !(year % 400)));
  memcpy(relative, absolute, 16);
  relative[0] = 0;
  relative[3] = absolute[3] + 7u > length ? 5 : (absolute[3] - 1u) / 7u + 1u;
}
static int same_rules(const struct ZoneYear *a, const struct ZoneYear *b) {
  if (!paired(b) || a->standard.offset != b->standard.offset ||
      a->daylight.offset != b->daylight.offset)
    return 0;
  U16 ad[8], bd[8], as[8], bs[8];
  relative_date(a->daylight_date, ad);
  relative_date(b->daylight_date, bd);
  relative_date(a->standard_date, as);
  relative_date(b->standard_date, bs);
  return !memcmp(ad, bd, sizeof(ad)) && !memcmp(as, bs, sizeof(as));
}
static void wide_name(U16 output[32], const char *name) {
  for (unsigned i = 0; i < 31 && name[i]; ++i)
    output[i] = (unsigned char)name[i];
}

/* The imported API asks for the current profile, rather than historical rules
 * for an arbitrary conversion year. Use recurring Windows dates only when the
 * adjacent host years agree; otherwise preserve the year's absolute dates.
 * A profile without an annual standard/daylight pair uses its current offset,
 * as a fixed zone. This also covers political changes and permanent DST.
 * No rule cache hides changes to TZ or the host's zone configuration.
 * https://learn.microsoft.com/windows/win32/api/timezoneapi/ns-timezoneapi-time_zone_information
 */
U32 bb_time_zone_information(int64_t seconds, void *output) {
  if (!output)
    return UINT32_MAX;
  struct ZoneInformation information = {0};
  memcpy(output, &information, sizeof(information));
  tzset();
  struct tm local;
  struct ZoneState current;
  if (!state_at((time_t)seconds, &current, &local) ||
      local.tm_year < -1898 || local.tm_year > 63633)
    return UINT32_MAX;
  int year = local.tm_year + 1900;
  struct ZoneYear profile;
  if (!zone_year(year, &profile))
    return UINT32_MAX;
  U32 status = 0;
  if (paired(&profile)) {
    information.bias = (I32)(-profile.standard.offset / 60);
    information.daylight_bias =
        (I32)((profile.standard.offset - profile.daylight.offset) / 60);
    wide_name(information.standard_name, profile.standard.name);
    wide_name(information.daylight_name, profile.daylight.name);
    memcpy(information.standard_date, profile.standard_date, 16);
    memcpy(information.daylight_date, profile.daylight_date, 16);
    struct ZoneYear previous, following;
    if (zone_year(year - 1, &previous) && zone_year(year + 1, &following) &&
        same_rules(&profile, &previous) && same_rules(&profile, &following)) {
      relative_date(profile.standard_date, information.standard_date);
      relative_date(profile.daylight_date, information.daylight_date);
    }
    status = current.daylight ? 2 : 1;
  } else {
    information.bias = (I32)(-current.offset / 60);
    wide_name(information.standard_name, current.name);
  }
  memcpy(output, &information, sizeof(information));
  return status;
}
#endif
