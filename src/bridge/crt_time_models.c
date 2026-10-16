/* SPDX-License-Identifier: GPL-3.0-only */
#include "crt_time_models.h"
#include "address.h"
#include "crt_time.h"
extern void bb_crt_time_local_c(uint32_t *);
extern void bb_crt_time_now_c(uint32_t *);
#define A(n) BB_U32(r[3] + 8u + (n) * 4u)
static void gmtime_entry(uint32_t *r) {
  r[7] = bb_address_encode(
      bb_crt_time_gmtime_entry((const int32_t *)(uintptr_t)A(0)));
}
static void initialize(uint32_t *r) {
  (void)r;
  bb_crt_time_initialize_zone_entry();
}
static void set_zone(uint32_t *r) {
  (void)r;
  bb_crt_time_set_zone_entry();
}
static void model_daylight(uint32_t *r) {
  r[7] = bb_crt_time_daylight_entry((const int32_t *)(uintptr_t)A(0));
}
static void transition(uint32_t *r) {
  bb_crt_time_transition_entry(A(0), A(1), A(2), A(3), A(4), A(5), A(6), A(7),
                               A(8), A(9), A(10));
}
const struct bb_model_target *bb_crt_time_model_find(void *unused,
                                                     uint32_t id) {
  (void)unused;
  static const struct bb_model_target targets[] = {
      {0x46c570, gmtime_entry, 1, 0, 0},
      {0x46c760, bb_crt_time_now_c, 1, 512, 0},
      {0x472aa0, bb_crt_time_local_c, 7, 128, 0},
      {0x47c550, initialize, 0, 0, 0},
      {0x47c580, set_zone, 0, 0, 0},
      {0x47c8e0, model_daylight, 1, 0, 0},
      {0x47cc50, transition, 11, 0, 0}};
  for (unsigned n = 0; n < sizeof targets / sizeof *targets; n++)
    if (targets[n].identity == id)
      return &targets[n];
  return NULL;
}
