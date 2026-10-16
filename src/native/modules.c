/* SPDX-License-Identifier: GPL-3.0-only */
#include "native.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#ifdef BB_MODEL_RUNTIME
#include "../bridge/address.h"
#include "../bridge/model_runtime.h"
#include "platform_models.h"
#endif
static PeModule modules[16];
static unsigned module_count;
Handle bb_module_reference(const PeModule *module) {
#ifdef BB_MODEL_RUNTIME
  return (Handle)(uintptr_t)module->reference;
#else
  return module->base;
#endif
}
PeModule *bb_pe_module(Handle handle) {
  if (!handle && module_count)
    return &modules[0];
  for (unsigned n = 0; n < module_count; n++)
    if (bb_module_reference(&modules[n]) == handle)
      return &modules[n];
  return NULL;
}
void *bb_pe_export(PeModule *module, const char *name) {
  if (module->builtin == BB_MODULE_MIXER)
    return bb_mixer_export(name);
  return NULL;
}
void bb_pe_initialize(PeModule *module) {
  if (module->initialized || !module->entry)
    return;
#ifdef BB_MODEL_RUNTIME
  struct bb_call_state state = {0, 0, 0, 0x202};
  uint32_t arguments[] = {module->reference, 1, 0};
  bb_model_invoke(&state, module->reference + module->entry, arguments, 3);
  int okay = state.eax != 0;
#else
  int(W * initialize)(Handle, U32, Handle) =
      (void *)bb_native_entry((uintptr_t)module->base + module->entry);
  int okay = initialize(module->base, 1, NULL);
#endif
  if (!okay)
    bb_die("DLL initialization failed: %s", module->name);
  module->initialized = 1;
}
PeModule *bb_pe_find(const char *name) {
  for (unsigned n = 0; n < module_count; n++)
    if (!strcasecmp(modules[n].name, name))
      return &modules[n];
  return NULL;
}
PeModule *bb_pe_load(const char *path) {
  const char *name = strrchr(path, '/');
  name = name ? name + 1 : path;
  PeModule *loaded = bb_pe_find(name);
  if (loaded)
    return loaded;
  if (module_count == 16)
    bb_die("too many native modules");
  if (!strcasecmp(name, "BEASTS.EXE")) {
    PeModule *module = &modules[module_count++];
    bb_game_create(module);
    return module;
  }
  if (!strcasecmp(name, "QMIXER95.DLL")) {
    PeModule *module = &modules[module_count++];
    bb_mixer_create(module);
    return module;
  }
  bb_die("unsupported native module: %s", path);
}

void *bb_pe_resource(Handle handle, const char *name, const char *type,
                     U32 *size) {
  PeModule *module = bb_pe_module(handle);
  if (module && module->builtin == BB_MODULE_MIXER)
    return bb_mixer_resource(name, type, size);
  if (module && module->builtin == BB_MODULE_GAME)
    return bb_game_resource(name, type, size);
  return NULL;
}

/* Owners must stop callbacks and release resource consumers before teardown. */
void bb_modules_reset(void) {
  bb_resources_reset();
  bb_game_unload_assets();
  for (unsigned n = 0; n < module_count; ++n) {
#ifdef BB_MODEL_RUNTIME
    if (!bb_address_unbind(modules[n].reference))
      bb_die("module image lost its logical mapping");
    free(modules[n].base);
#else
    munmap(modules[n].base, modules[n].size);
#endif
  }
  memset(modules, 0, sizeof modules);
  module_count = 0;
#ifdef BB_MODEL_RUNTIME
  bb_platform_model_reset();
#endif
}
