/* SPDX-License-Identifier: GPL-3.0-only */
#include "kernel_handle.h"
#include <stdio.h>
#include <string.h>
static U32 W GetModuleFileNameA(Handle module, char *name, U32 size) {
  const char *library = bb_module_name(module);
  if (!library)
    return (U32)bb_kernel_error(126);
  /* Native module identities have no on-disk EXE/DLL, but callers still need
   * a full guest path. The mixer replaces this basename with QMIXER95.INI. */
  char value[sizeof(((PeModule *)0)->name) + 4];
  snprintf(value, sizeof(value), "Z:\\%s", library);
  if (!size)
    return 0;
  if (!name)
    return (U32)bb_kernel_error(87);
  size_t length = strlen(value);
  if (size <= length) {
    memcpy(name, value, size - 1);
    name[size - 1] = 0;
    bb_kernel_last_error = 122;
    return size;
  }
  memcpy(name, value, length + 1);
  return (U32)length;
}
static Handle W GetModuleHandleA(const char *name) {
  Handle module = bb_module_handle(name);
  if (!module)
    bb_kernel_last_error = 126;
  return module;
}
static Handle W LoadLibraryA(const char *name) {
  if (!name) {
    bb_kernel_last_error = 87;
    return NULL;
  }
  return GetModuleHandleA(name);
}
static void *W GetProcAddress(Handle module, const char *name) {
  void *function = bb_module_export(module, name);
  if (!function)
    bb_kernel_last_error = !module || !bb_module_name(module) ? 126 : 127;
  return function;
}

void *bb_module_api(const char *name) {
  static const struct ApiEntry table[] = {
      API_ENTRY(GetModuleFileNameA), API_ENTRY(GetModuleHandleA),
      API_ENTRY(LoadLibraryA), API_ENTRY(GetProcAddress)};
  API_RETURN(table);
}
#ifdef BB_MODEL_RUNTIME
#include "module_models.inc"
#endif
