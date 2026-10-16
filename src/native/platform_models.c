/* SPDX-License-Identifier: GPL-3.0-only */
#include "platform_models.h"
#include "audio_models.h"
#include "cd_models.h"
#include "clock_models.h"
#include "draw_models.h"
#include "environment_models.h"
#include "file_models.h"
#include "kernel_exports.h"
#include "locale_models.h"
#include "memory_models.h"
#include "module_models.h"
#include "native.h"
#include "process_models.h"
#include "resource_models.h"
#include "settings_models.h"
#include "sound_models.h"
#include "sync_models.h"
#include "system_models.h"
#include "timer_models.h"
#include "window_models.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
struct MissingExport {
  struct bb_model_target target;
  char *dll, *name;
};
static struct MissingExport missing[512];
static unsigned missing_count;
static pthread_mutex_t missing_mutex = PTHREAD_MUTEX_INITIALIZER;
static void missing_call(uint32_t *r) {
  (void)r;
  uint32_t id = bb_model_current_identity();
  pthread_mutex_lock(&missing_mutex);
  for (unsigned n = 0; n < missing_count; n++)
    if (missing[n].target.identity == id) {
      const char *dll = missing[n].dll, *name = missing[n].name;
      pthread_mutex_unlock(&missing_mutex);
      bb_die("unconverted portable service: %s!%s", dll, name);
    }
  pthread_mutex_unlock(&missing_mutex);
  bb_die("unregistered portable service: %08x", id);
}
static uint32_t missing_identity(const char *dll, const char *name) {
  pthread_mutex_lock(&missing_mutex);
  for (unsigned n = 0; n < missing_count; n++)
    if (!strcasecmp(dll, missing[n].dll) && !strcmp(name, missing[n].name)) {
      uint32_t id = missing[n].target.identity;
      pthread_mutex_unlock(&missing_mutex);
      return id;
    }
  if (missing_count == 512)
    bb_die("too many unconverted portable services");
  char *library = strdup(dll), *symbol = strdup(name);
  if (!library || !symbol)
    bb_die("cannot allocate portable service identity");
  uint32_t id = 0x0b000000u + missing_count * 4u;
  missing[missing_count++] =
      (struct MissingExport){{id, missing_call, 0, 0, 0}, library, symbol};
  pthread_mutex_unlock(&missing_mutex);
  return id;
}
static uint32_t kernel_export(const char *name) {
  uint32_t (*const providers[])(const char *) = {
      bb_memory_model_export,      bb_file_model_export,
      bb_settings_model_export,    bb_clock_model_export,
      bb_sync_model_export,        bb_module_model_export,
      bb_system_model_export,      bb_resource_model_export,
      bb_environment_model_export, bb_locale_model_export,
      bb_process_model_export};
  for (unsigned n = 0; n < sizeof(providers) / sizeof(*providers); n++) {
    uint32_t id = providers[n](name);
    if (id)
      return id;
  }
#define NAME(name) #name,
  static const char *const unconverted[] = {BB_KERNEL_BASE_EXPORTS(NAME)};
#undef NAME
  for (unsigned n = 0; n < sizeof(unconverted) / sizeof(*unconverted); n++)
    if (!strcmp(name, unconverted[n]))
      return missing_identity("KERNEL32.dll", name);
  return 0;
}
uint32_t bb_platform_model_export(const char *dll, const char *name) {
  if (!dll || (uintptr_t)name <= 0xffff)
    return 0;
  if (!strcasecmp(dll, "KERNEL32.dll"))
    return strncmp(name, "Reg", 3) ? kernel_export(name) : 0;
  if (!strcasecmp(dll, "ADVAPI32.dll"))
    return !strncmp(name, "Reg", 3) ? bb_settings_model_export(name) : 0;
  if (!strcasecmp(dll, "USER32.dll")) {
    if (!strcmp(name, "LoadImageA"))
      return bb_draw_model_export(name);
    return bb_window_model_export(name);
  }
  if (!strcasecmp(dll, "GDI32.dll"))
    return strcmp(name, "LoadImageA") && strcmp(name, "DirectDrawCreate")
               ? bb_draw_model_export(name)
               : 0;
  if (!strcasecmp(dll, "DDRAW.dll"))
    return !strcmp(name, "DirectDrawCreate") ? bb_draw_model_export(name) : 0;
  if (!strcasecmp(dll, "DSOUND.dll"))
    return !strcmp(name, "DirectSoundCreate") ? bb_sound_model_export(name) : 0;
  if (!strcasecmp(dll, "WINMM.dll")) {
    if (!strcmp(name, "DirectSoundCreate"))
      return 0;
    uint32_t id = bb_audio_model_export(name);
    if (!id)
      id = bb_timer_model_export(name);
    if (!id)
      id = bb_cd_model_export(name);
    return id;
  }
  return 0;
}
uint32_t bb_platform_model_require(const char *dll, const char *name) {
  uint32_t id = bb_platform_model_export(dll, name);
  return id ? id : missing_identity(dll, name);
}
const struct bb_model_target *bb_platform_model_find(void *ctx, uint32_t id) {
  const struct bb_model_target *(*const providers[])(void *, uint32_t) = {
      bb_memory_model_find, bb_file_model_find,     bb_settings_model_find,
      bb_clock_model_find,  bb_sync_model_find,     bb_timer_model_find,
      bb_cd_model_find,     bb_sound_model_find,    bb_audio_model_find,
      bb_window_model_find, bb_draw_model_find,     bb_module_model_find,
      bb_system_model_find, bb_resource_model_find, bb_environment_model_find,
      bb_locale_model_find, bb_process_model_find};
  for (unsigned n = 0; n < sizeof(providers) / sizeof(*providers); n++) {
    const struct bb_model_target *target = providers[n](ctx, id);
    if (target)
      return target;
  }
  pthread_mutex_lock(&missing_mutex);
  const struct bb_model_target *result = NULL;
  for (unsigned n = 0; n < missing_count; n++)
    if (missing[n].target.identity == id) {
      result = &missing[n].target;
      break;
    }
  pthread_mutex_unlock(&missing_mutex);
  return result;
}
void bb_platform_model_reset(void) {
  pthread_mutex_lock(&missing_mutex);
  for (unsigned n = 0; n < missing_count; n++) {
    free(missing[n].dll);
    free(missing[n].name);
  }
  memset(missing, 0, sizeof missing);
  missing_count = 0;
  pthread_mutex_unlock(&missing_mutex);
}
