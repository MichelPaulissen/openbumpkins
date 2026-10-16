/* SPDX-License-Identifier: GPL-3.0-only */
#include "../bridge/address.h"
#include "sync_models.h"
static uint32_t sync_arg(uint32_t *r, unsigned n) {
  return BB_U32(r[3] + 8u + n * 4u);
}
#define A(n) sync_arg(r, n)
static void model_create(uint32_t *r) {
  Handle handle = CreateEventA((Handle)(uintptr_t)A(0), (int)A(1), (int)A(2),
                               (const char *)(uintptr_t)A(3));
  r[7] = handle ? bb_kernel_handle_export(handle) : 0;
  if (handle && !r[7]) {
    bb_kernel_handle_release(handle);
    bb_kernel_last_error = 8;
  }
}
static void model_set(uint32_t *r) {
  Handle handle = bb_kernel_handle_acquire(A(0));
  r[7] = SetEvent(handle);
  bb_kernel_handle_unborrow(handle);
}
static void model_reset(uint32_t *r) {
  Handle handle = bb_kernel_handle_acquire(A(0));
  r[7] = ResetEvent(handle);
  bb_kernel_handle_unborrow(handle);
}
static void model_wait(uint32_t *r) {
  Handle handle = bb_kernel_handle_acquire(A(0));
  r[7] = WaitForSingleObject(handle, A(1));
  bb_kernel_handle_unborrow(handle);
}
#undef A
static const struct {
  const char *name;
  struct bb_model_target target;
} sync_targets[] = {
    {"CreateEventA", {BB_SYNC_MODEL_API, model_create, 4, 0, 0}},
    {"SetEvent", {BB_SYNC_MODEL_API + 4, model_set, 1, 0, 0}},
    {"ResetEvent", {BB_SYNC_MODEL_API + 8, model_reset, 1, 0, 0}},
    {"WaitForSingleObject", {BB_SYNC_MODEL_API + 12, model_wait, 2, 0, 0}}};
uint32_t bb_sync_model_export(const char *name) {
  for (unsigned i = 0; i < sizeof(sync_targets) / sizeof(*sync_targets); ++i)
    if (!strcmp(name, sync_targets[i].name))
      return sync_targets[i].target.identity;
  return 0;
}
const struct bb_model_target *bb_sync_model_find(void *unused, uint32_t id) {
  (void)unused;
  for (unsigned i = 0; i < sizeof(sync_targets) / sizeof(*sync_targets); ++i)
    if (id == sync_targets[i].target.identity)
      return &sync_targets[i].target;
  return NULL;
}
