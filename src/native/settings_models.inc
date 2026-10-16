/* SPDX-License-Identifier: GPL-3.0-only */
#include "../bridge/interop.h"
#include "settings_models.h"
static uint32_t settings_arg(uint32_t *r, unsigned n) {
  return BB_U32(r[3] + 8u + n * 4u);
}
static void *settings_data(uint32_t *r, unsigned n, size_t size) {
  uint32_t p = settings_arg(r, n);
  return p ? BB_SPAN(p, size) : NULL;
}
#define A(n) settings_arg(r, n)
#define P(n, bytes) settings_data(r, n, bytes)
static void model_delete(uint32_t *r) {
  r[7] = RegDeleteKeyA((Handle)(uintptr_t)A(0), (const char *)(uintptr_t)A(1));
}
static void model_close(uint32_t *r) {
  r[7] = RegCloseKey((Handle)(uintptr_t)A(0));
}
static void model_open(uint32_t *r) {
  r[7] = RegOpenKeyExA((Handle)(uintptr_t)A(0), (const char *)(uintptr_t)A(1),
                       A(2), A(3), P(4, 4));
}
static void model_enum(uint32_t *r) {
  if (A(1) >= 13) {
    r[7] = RegEnumValueA((Handle)(uintptr_t)A(0), A(1), NULL, NULL, NULL, NULL,
                         NULL, NULL);
    return;
  }
  uint32_t ns = A(3), ds = A(7);
  r[7] = RegEnumValueA((Handle)(uintptr_t)A(0), A(1), P(2, ns ? BB_U32(ns) : 0),
                       P(3, 4), P(4, 4), P(5, 4), P(6, ds ? BB_U32(ds) : 0),
                       P(7, 4));
}
static void model_integer(uint32_t *r) {
  r[7] = GetPrivateProfileIntA(P(0, 1), P(1, 1), (int32_t)A(2), P(3, 1));
}
#undef A
#undef P
static const struct bb_model_target settings_targets[] = {
    {BB_SETTINGS_MODEL_API, model_delete, 2, 0, 0},
    {BB_SETTINGS_MODEL_API + 4, model_close, 1, 0, 0},
    {BB_SETTINGS_MODEL_API + 8, model_open, 5, 0, 0},
    {BB_SETTINGS_MODEL_API + 12, model_enum, 8, 0, 0},
    {BB_SETTINGS_MODEL_API + 16, model_integer, 4, 0, 0}};
uint32_t bb_settings_model_export(const char *name) {
  static const char *const names[] = {"RegDeleteKeyA", "RegCloseKey",
                                      "RegOpenKeyExA", "RegEnumValueA",
                                      "GetPrivateProfileIntA"};
  for (unsigned i = 0; i < sizeof(names) / sizeof(*names); ++i)
    if (!strcmp(name, names[i]))
      return settings_targets[i].identity;
  return 0;
}
const struct bb_model_target *bb_settings_model_find(void *unused,
                                                     uint32_t id) {
  (void)unused;
  for (unsigned i = 0; i < sizeof(settings_targets) / sizeof(*settings_targets);
       ++i)
    if (id == settings_targets[i].identity)
      return settings_targets + i;
  return NULL;
}
