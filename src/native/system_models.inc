/* SPDX-License-Identifier: GPL-3.0-only */
#include "../bridge/address.h"
#include "system_models.h"
#include <pthread.h>
static pthread_once_t command_once = PTHREAD_ONCE_INIT;
static uint32_t command_reference;
static void command_map(void) {
  char *command = GetCommandLineA();
  if (!bb_address_bind_auto(0xf0000000u, UINT64_C(0x100000000), command,
                            strlen(command) + 1, 16, &command_reference))
    bb_die("cannot map process command line");
}
static uint32_t system_arg(uint32_t *r, unsigned n) {
  return BB_U32(r[3] + 8u + n * 4u);
}
#define A(n) system_arg(r, n)
static void model_version(uint32_t *r) { r[7] = GetVersion(); }
static void model_acp(uint32_t *r) { r[7] = GetACP(); }
static void model_oem(uint32_t *r) { r[7] = GetOEMCP(); }
static void model_cpinfo(uint32_t *r) {
  r[7] =
      GetCPInfo(A(0), A(1) && bb_codepage_id(A(0)) ? BB_SPAN(A(1), 20) : NULL);
}
static void model_command(uint32_t *r) {
  pthread_once(&command_once, command_map);
  r[7] = command_reference;
}
static void model_startup(uint32_t *r) { GetStartupInfoA(BB_SPAN(A(0), 68)); }
static void model_debug(uint32_t *r) { OutputDebugStringA(BB_DATA(A(0))); }
static void model_length(uint32_t *r) { r[7] = lstrlenA(BB_DATA(A(0))); }
static void model_copy(uint32_t *r) {
  const char *source = BB_DATA(A(1));
  r[7] = bb_address_encode(lstrcpyA(BB_SPAN(A(0), strlen(source) + 1), source));
}
static void model_muldiv(uint32_t *r) {
  r[7] = MulDiv((int32_t)A(0), (int32_t)A(1), (int32_t)A(2));
}
static void model_filter_set(uint32_t *r) {
  r[7] = (uintptr_t)SetUnhandledExceptionFilter((void *)(uintptr_t)A(0));
}
static void model_filter(uint32_t *r) {
  /* The existing shim deliberately ignores the exceptional record. */
  (void)A(0);
  r[7] = UnhandledExceptionFilter(NULL);
}
#undef A
static const struct {
  const char *name;
  struct bb_model_target target;
} system_targets[] = {
#define T(i, name, body, words)                                                \
  {name, {BB_SYSTEM_MODEL_API + i * 4u, body, words, 0, 0}}
    T(0, "GetVersion", model_version, 0),
    T(1, "GetACP", model_acp, 0),
    T(2, "GetOEMCP", model_oem, 0),
    T(3, "GetCPInfo", model_cpinfo, 2),
    T(4, "GetCommandLineA", model_command, 0),
    T(5, "GetStartupInfoA", model_startup, 1),
    T(6, "OutputDebugStringA", model_debug, 1),
    T(7, "lstrlenA", model_length, 1),
    T(8, "lstrcpyA", model_copy, 2),
    T(9, "MulDiv", model_muldiv, 3),
    T(10, "SetUnhandledExceptionFilter", model_filter_set, 1),
    T(11, "UnhandledExceptionFilter", model_filter, 1)
#undef T
};
uint32_t bb_system_model_export(const char *name) {
  for (unsigned n = 0; n < sizeof(system_targets) / sizeof(*system_targets);
       n++)
    if (!strcmp(name, system_targets[n].name))
      return system_targets[n].target.identity;
  return 0;
}
const struct bb_model_target *bb_system_model_find(void *unused, uint32_t id) {
  (void)unused;
  for (unsigned n = 0; n < sizeof(system_targets) / sizeof(*system_targets);
       n++)
    if (id == system_targets[n].target.identity)
      return &system_targets[n].target;
  return NULL;
}
