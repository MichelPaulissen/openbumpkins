/* SPDX-License-Identifier: GPL-3.0-only */
#include "environment_models.h"
static uint32_t environment_arg(uint32_t *r, unsigned n) {
  return BB_U32(r[3] + 8u + n * 4u);
}
static void model_environment_set(uint32_t *r) {
  uint32_t name = environment_arg(r, 0), value = environment_arg(r, 1);
  r[7] = SetEnvironmentVariableA(name ? BB_DATA(name) : NULL,
                                 value ? BB_DATA(value) : NULL);
}
static void model_environment_ansi(uint32_t *r) {
  r[7] = bb_address_encode(GetEnvironmentStrings());
}
static void model_environment_wide(uint32_t *r) {
  r[7] = bb_address_encode(GetEnvironmentStringsW());
}
static void model_environment_free_ansi(uint32_t *r) {
  uint32_t ref = environment_arg(r, 0);
  r[7] = FreeEnvironmentStringsA(ref ? BB_DATA(ref) : NULL);
}
static void model_environment_free_wide(uint32_t *r) {
  uint32_t ref = environment_arg(r, 0);
  r[7] = FreeEnvironmentStringsW(ref ? BB_DATA(ref) : NULL);
}
static const struct {
  const char *name;
  struct bb_model_target target;
} environment_targets[] = {
#define T(i, name, body, words)                                                \
  {#name, {BB_ENVIRONMENT_MODEL_API + i * 4u, body, words, 0, 0}}
    T(0, SetEnvironmentVariableA, model_environment_set, 2),
    T(1, GetEnvironmentStrings, model_environment_ansi, 0),
    T(2, GetEnvironmentStringsW, model_environment_wide, 0),
    T(3, FreeEnvironmentStringsA, model_environment_free_ansi, 1),
    T(4, FreeEnvironmentStringsW, model_environment_free_wide, 1)
#undef T
};
uint32_t bb_environment_model_export(const char *name) {
  for (unsigned n = 0;
       n < sizeof environment_targets / sizeof *environment_targets; n++)
    if (!strcmp(name, environment_targets[n].name))
      return environment_targets[n].target.identity;
  return 0;
}
const struct bb_model_target *bb_environment_model_find(void *context,
                                                        uint32_t id) {
  (void)context;
  for (unsigned n = 0;
       n < sizeof environment_targets / sizeof *environment_targets; n++)
    if (environment_targets[n].target.identity == id)
      return &environment_targets[n].target;
  return NULL;
}
