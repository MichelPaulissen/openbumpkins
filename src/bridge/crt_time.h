/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BUMPKINS_CRT_TIME_H
#define BUMPKINS_CRT_TIME_H
#include <stdint.h>
int32_t *bb_crt_time_gmtime_entry(const int32_t *);
int32_t bb_crt_time_now_entry(int32_t *);
int32_t bb_crt_time_local_entry(int32_t, int32_t, int32_t, int32_t, int32_t,
                                int32_t, int32_t);
void bb_crt_time_initialize_zone_entry(void);
void bb_crt_time_set_zone_entry(void);
int32_t bb_crt_time_daylight_entry(const int32_t *);
void bb_crt_time_transition_entry(int32_t, int32_t, uint32_t, int32_t, int32_t,
                                  int32_t, int32_t, int32_t, int32_t, int32_t,
                                  int32_t);
#endif
