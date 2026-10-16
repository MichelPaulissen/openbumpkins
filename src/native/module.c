/* SPDX-License-Identifier: GPL-3.0-only */
#include "native.h"
#include <string.h>
#include <strings.h>

#include "module_names.h"
#ifdef BB_MODEL_RUNTIME
#include "platform_models.h"
#endif

Handle bb_module_handle(const char *name) {
  if (!name) {
    PeModule *game = bb_pe_module(NULL);
    return game ? bb_module_reference(game) : NULL;
  }
  const char *base = name;
  for (const char *p = name; *p; ++p)
    if (*p == '/' || *p == '\\')
      base = p + 1;
  size_t length = strlen(base);
  for (size_t n = 0;
       n < sizeof bb_native_libraries / sizeof *bb_native_libraries; ++n) {
    const char *library = bb_native_libraries[n].name;
    if (strcasecmp(base, library) &&
        (strchr(base, '.') || strlen(library) != length + 4 ||
         strncasecmp(base, library, length) ||
         strcasecmp(library + length, ".dll")))
      continue;
    if (n < 2) {
      PeModule *pe = bb_pe_find(library);
      return pe ? bb_module_reference(pe) : NULL;
    }
#ifdef BB_MODEL_RUNTIME
    return (Handle)(uintptr_t)(0x0a000000u + n * 4u);
#else
    return (Handle)&bb_native_libraries[n];
#endif
  }
  return NULL;
}

const char *bb_module_name(Handle handle) {
  PeModule *pe = bb_pe_module(handle);
  if (pe)
    return pe->name;
  for (size_t n = 2;
       n < sizeof bb_native_libraries / sizeof *bb_native_libraries; ++n)
    if (handle ==
#ifdef BB_MODEL_RUNTIME
        (Handle)(uintptr_t)(0x0a000000u + n * 4u)
#else
        (Handle)&bb_native_libraries[n]
#endif
    )
      return bb_native_libraries[n].name;
  return NULL;
}

void *bb_library_api(const char *dll, const char *name) {
#ifdef BB_MODEL_RUNTIME
  return (void *)(uintptr_t)bb_platform_model_export(dll, name);
#else
  /* Win32 service ordinal tables are not part of this adapter. The mixer's
   * original public ordinals are handled separately by bb_pe_export. */
  if (!dll || (uintptr_t)name <= 0xffff)
    return NULL;
  if (!strcasecmp(dll, "KERNEL32.dll"))
    return strncmp(name, "Reg", 3) ? bb_kernel_api(name) : NULL;
  if (!strcasecmp(dll, "ADVAPI32.dll"))
    return !strncmp(name, "Reg", 3) ? bb_kernel_api(name) : NULL;
  if (!strcasecmp(dll, "USER32.dll"))
    return !strcmp(name, "LoadImageA") ? bb_draw_api(name)
                                       : bb_window_api(name);
  if (!strcasecmp(dll, "GDI32.dll"))
    return strcmp(name, "LoadImageA") && strcmp(name, "DirectDrawCreate")
               ? bb_draw_api(name)
               : NULL;
  if (!strcasecmp(dll, "DDRAW.dll"))
    return !strcmp(name, "DirectDrawCreate") ? bb_draw_api(name) : NULL;
  if (!strcasecmp(dll, "DSOUND.dll"))
    return !strcmp(name, "DirectSoundCreate") ? bb_audio_api(name) : NULL;
  if (!strcasecmp(dll, "WINMM.dll"))
    return strcmp(name, "DirectSoundCreate") ? bb_audio_api(name) : NULL;
  return NULL;
#endif
}

void *bb_module_export(Handle handle, const char *name) {
  if (!handle || !name)
    return NULL;
  PeModule *pe = bb_pe_module(handle);
  return pe ? bb_pe_export(pe, name)
            : bb_library_api(bb_module_name(handle), name);
}
