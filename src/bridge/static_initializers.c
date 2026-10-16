/* SPDX-License-Identifier: GPL-3.0-only */
#include "static_initializers.h"
#include "crt_bootstrap.h"
#include "global_lifecycle.h"
#ifdef BB_MODEL_RUNTIME
#include "lifecycle_models.h"
#endif

/* Construct first, then register the actual C destructor in shutdown order. */
static int32_t register_destructor(void (*callback)(void)) {
#ifdef BB_MODEL_RUNTIME
  return callback ? bb_crt_boot_atexit_entry(
                        (void *)(uintptr_t)bb_lifecycle_code_identity(callback))
                  : 0;
#else
  return callback ? bb_crt_boot_atexit_entry((void *)(uintptr_t)callback) : 0;
#endif
}
#define INITIALIZER(address, name, child, callback)                            \
  int32_t name(void) {                                                         \
    child();                                                                   \
    return register_destructor(callback);                                      \
  }
BB_STATIC_INITIALIZERS(INITIALIZER)
#undef INITIALIZER
#define REGISTRATION(address, name, callback)                                  \
  int32_t name(void) { return register_destructor(callback); }
BB_STATIC_REGISTRATIONS(REGISTRATION)
#undef REGISTRATION

/* These roots are called directly in the native C lifecycle. The Windows
 * bridge still installs their original-address entry hooks. */
int bb_static_lifecycle_owns_address(uintptr_t address) {
  switch (address) {
#define INITIALIZER(root, name, child, callback) case root:
    BB_STATIC_INITIALIZERS(INITIALIZER)
#undef INITIALIZER
#define REGISTRATION(root, name, callback) case root:
    BB_STATIC_REGISTRATIONS(REGISTRATION)
#undef REGISTRATION
#define GLOBAL(root, name) case root:
    BB_GLOBAL_LIFECYCLE_ENTRIES(GLOBAL)
#undef GLOBAL
    return 1;
  default:
    return 0;
  }
}
