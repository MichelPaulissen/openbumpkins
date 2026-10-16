/* SPDX-License-Identifier: GPL-3.0-only */
#include "../bridge/address.h"
#include "module_models.h"
static uint32_t module_arg(uint32_t *r, unsigned n) {
  return BB_U32(r[3] + 8u + n * 4u);
}
#define A(n) module_arg(r, n)
#define H(n) ((Handle)(uintptr_t)A(n))
static void model_filename(uint32_t *r) {
  const char *name = bb_module_name(H(0));
  size_t span = name && A(2) ? strlen(name) + 4 : 0;
  if (span > A(2))
    span = A(2);
  r[7] =
      GetModuleFileNameA(H(0), span && A(1) ? BB_SPAN(A(1), span) : NULL, A(2));
}
static void model_handle(uint32_t *r) {
  r[7] = (uint32_t)(uintptr_t)GetModuleHandleA(A(0) ? BB_DATA(A(0)) : NULL);
}
static void model_load(uint32_t *r) {
  r[7] = (uint32_t)(uintptr_t)LoadLibraryA(A(0) ? BB_DATA(A(0)) : NULL);
}
static void model_proc(uint32_t *r) {
  r[7] = (uint32_t)(uintptr_t)GetProcAddress(
      H(0), A(1) <= 0xffff ? (const char *)(uintptr_t)A(1) : BB_DATA(A(1)));
}
#undef H
#undef A
static const struct {
  const char *name;
  struct bb_model_target target;
} module_targets[] = {
    {"GetModuleFileNameA", {BB_MODULE_MODEL_API, model_filename, 3, 0, 0}},
    {"GetModuleHandleA", {BB_MODULE_MODEL_API + 4, model_handle, 1, 0, 0}},
    {"LoadLibraryA", {BB_MODULE_MODEL_API + 8, model_load, 1, 0, 0}},
    {"GetProcAddress", {BB_MODULE_MODEL_API + 12, model_proc, 2, 0, 0}}};
uint32_t bb_module_model_export(const char *name) {
  for (unsigned n = 0; n < sizeof(module_targets) / sizeof(*module_targets);
       n++)
    if (!strcmp(name, module_targets[n].name))
      return module_targets[n].target.identity;
  return 0;
}
const struct bb_model_target *bb_module_model_find(void *unused, uint32_t id) {
  (void)unused;
  for (unsigned n = 0; n < sizeof(module_targets) / sizeof(*module_targets);
       n++)
    if (id == module_targets[n].target.identity)
      return &module_targets[n].target;
  return NULL;
}
