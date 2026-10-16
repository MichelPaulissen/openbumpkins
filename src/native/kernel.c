/* SPDX-License-Identifier: GPL-3.0-only */
#include "kernel_handle.h"

void *bb_kernel_api(const char *name) {
  void *memory = bb_memory_api(name);
  if (memory)
    return memory;
  void *file = bb_file_api(name);
  if (file)
    return file;
  void *settings = bb_settings_api(name);
  if (settings)
    return settings;
  void *clock = bb_clock_api(name);
  if (clock)
    return clock;
  void *sync = bb_sync_api(name);
  if (sync)
    return sync;
  void *environment = bb_environment_api(name);
  if (environment)
    return environment;
  void *locale = bb_locale_api(name);
  if (locale)
    return locale;
  void *system = bb_system_api(name);
  if (system)
    return system;
  void *module = bb_module_api(name);
  if (module)
    return module;
  void *resource = bb_resource_api(name);
  if (resource)
    return resource;
  return bb_process_api(name);
}
