/* SPDX-License-Identifier: GPL-3.0-only */
#include "crt_bootstrap.h"
#include "crt_debug_heap.h"
#include "crt_errors.h"
#include "game_calls.h"
#include <signal.h>
#define STR(a) ((const char *)(uintptr_t)(a))
void *bb_crt_boot_new_entry(uint32_t size) {
  return bb_crt_malloc_retry_entry(size, 1);
}
void bb_crt_boot_delete_entry(void *p) {
  if (!p)
    return;
  uint32_t type = BB_U32(bb_address_encode(p) - 12u);
  if ((type & 0xffff) != 4 && type != 1 && (type & 0xffff) != 2 && type != 3) {
#ifdef BB_MODEL_RUNTIME
    int32_t result =
        BB_GAME_CALL(0x00470ef0u, 0, 2, 0x482508u, 47, 0, 0x482514u);
#else
    int32_t result =
        bb_crt_errors_report_entry(2, STR(0x482508), 47, NULL, STR(0x482514));
#endif
    if (result == 1)
#ifdef BB_MODEL_RUNTIME
#ifdef _WIN32
      bb_host_debug_break();
#else
      raise(SIGTRAP);
#endif
#else
      __asm__ volatile("int3");
#endif
  }
  bb_crt_free_debug_entry(p, BB_U32(bb_address_encode(p) - 12u));
}
