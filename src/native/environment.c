/* SPDX-License-Identifier: GPL-3.0-only */
#include "../bridge/address.h"
#include "../bridge/heap.h"
#include "native.h"
#include <stdlib.h>
#include <string.h>
static int W SetEnvironmentVariableA(const char *name, const char *value) {
  return !(value ? setenv(name, value, 1) : unsetenv(name));
}
/* Preserve the shim's empty, double-terminated environment. Host process
 * environment mutation remains available through SetEnvironmentVariableA. */
static void *environment_allocate(size_t bytes) {
#ifdef BB_MODEL_RUNTIME
  uint32_t ref = bb_heap_zeroed(1, bytes);
  return ref ? BB_SPAN(ref, bytes) : NULL;
#else
  return calloc(1, bytes);
#endif
}
static char *W GetEnvironmentStrings(void) { return environment_allocate(2); }
static U16 *W GetEnvironmentStringsW(void) { return environment_allocate(4); }
static void environment_release(void *strings) {
#ifdef BB_MODEL_RUNTIME
  if (!bb_heap_release(bb_address_encode(strings)))
    bb_die("invalid environment block release");
#else
  free(strings);
#endif
}
static int W FreeEnvironmentStringsA(char *strings) {
  environment_release(strings);
  return 1;
}
static int W FreeEnvironmentStringsW(U16 *strings) {
  environment_release(strings);
  return 1;
}

void *bb_environment_api(const char *name) {
  static const struct ApiEntry table[] = {
      API_ENTRY(SetEnvironmentVariableA), API_ENTRY(GetEnvironmentStrings),
      API_ENTRY(GetEnvironmentStringsW), API_ENTRY(FreeEnvironmentStringsA),
      API_ENTRY(FreeEnvironmentStringsW)};
  API_RETURN(table);
}
#ifdef BB_MODEL_RUNTIME
#include "environment_models.inc"
#endif
