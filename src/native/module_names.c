/* SPDX-License-Identifier: GPL-3.0-only */
#include "module_names.h"
#include <stddef.h>
#include <strings.h>
/* These services are linked into the native runtime and live for the process
 * lifetime. Their handles must not borrow caller strings or allocate on load.
 */
const struct NativeLibrary bb_native_libraries[BB_NATIVE_LIBRARY_COUNT] = {
    {"BEASTS.EXE"},   {"QMIXER95.DLL"}, {"KERNEL32.dll"},
    {"USER32.dll"},   {"GDI32.dll"},    {"WINMM.dll"},
    {"ADVAPI32.dll"}, {"DDRAW.dll"},    {"DSOUND.dll"}};

const char *bb_library_name(const char *name) {
  if (name)
    for (size_t n = 0;
         n < sizeof bb_native_libraries / sizeof *bb_native_libraries; ++n)
      if (!strcasecmp(name, bb_native_libraries[n].name))
        return bb_native_libraries[n].name;
  return NULL;
}
