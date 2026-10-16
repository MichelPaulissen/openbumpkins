/* SPDX-License-Identifier: GPL-3.0-only */
#include "native.h"
#define BB_ENGINE_ENTRIES_ONLY
#define BB_QMIX_ENTRIES_ONLY
#include "../bridge/engine_hooks.h"
#include "../bridge/qmixer_hooks.h"

/* Built before any game initialization or worker creation, then immutable.
 * Logical code values remain metadata; calls need no executable alias pages. */
struct Entry {
  uintptr_t id, target;
};
static struct Entry entries[8192];
static unsigned bucket(uintptr_t id) {
  return ((id >> 3) * 2654435761u) & 8191u;
}
static void insert(uintptr_t id, void (*target)(void)) {
  unsigned slot = bucket(id);
  while (entries[slot].id && entries[slot].id != id)
    slot = (slot + 1) & 8191u;
  entries[slot] = (struct Entry){id, (uintptr_t)target};
}
void bb_native_initialize_entries(void) {
  for (size_t n = 0; n < sizeof(bb_engine_hooks) / sizeof(bb_engine_hooks[0]);
       ++n)
    insert(bb_engine_hooks[n].address, bb_engine_hooks[n].replacement);
  for (size_t n = 0; n < sizeof(bb_qmixer_hooks) / sizeof(bb_qmixer_hooks[0]);
       ++n)
    insert(bb_qmixer_hooks[n].address, bb_qmixer_hooks[n].replacement);
}
uintptr_t bb_native_entry(uintptr_t id) {
  if (!((id >= 0x401000 && id < 0x482000) ||
        (id >= 0x10001000 && id < 0x1001a000)))
    return id;
  unsigned slot = bucket(id);
  while (entries[slot].id) {
    if (entries[slot].id == id)
      return entries[slot].target;
    slot = (slot + 1) & 8191u;
  }
  return id;
}

/* Scope/member funclets receive live registers and the owner's EBP. Resolve
 * EAX's logical call target without changing those inputs, flags or x87 state.
 * The final ret transfers to C with the original caller return still below it.
 */
void __attribute__((naked, noinline)) bb_native_dispatch_eax(void) {
  __asm__ volatile("pushl %eax;pushfl;pushal;cld;pushl 28(%esp);"
                   "call bb_native_entry;addl $4,%esp;movl %eax,36(%esp);"
                   "popal;popfl;ret");
}
