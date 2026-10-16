/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "crt_bootstrap.h"
#include "crt_signals.h"
#include "static_initializers.h"
#include "crt_math_base.h"
#include "crt_errors.h"
#include "crt_debug_heap.h"
#include "crt_format.h"
#include "crt_primitives.h"
#include "crt_stream.h"
#include "game_calls.h"
#include <stddef.h>
#define BYTE(a) BB_U8(a)
#define WORD(a) BB_U16(a)
#define WIN BB_STDCALL
#define STR(a) ((const char *)(uintptr_t)(a))
static void callback(uint32_t id) {
#ifdef BB_MODEL_RUNTIME
  BB_GAME_CALL(id, 0);
#else
  ((void (*)(void))BB_CODE(id))();
#endif
}
void bb_crt_boot_initializers_entry(uint32_t *first, uint32_t *last) {
  uintptr_t p = (uintptr_t)first, end = (uintptr_t)last;
  while (p < end) {
    if (BB_U32(p))
      callback(BB_U32(p));
    p += 4;
  }
}
void bb_crt_boot_initialize_entry(void) {
  if (BB_U32(0x48cc08))
    callback(BB_U32(0x48cc08));
  bb_crt_boot_exit_table_entry();
  bb_crt_boot_stdio_entry();
  bb_crt_signal_initialize_entry();
#ifdef BB_MODEL_RUNTIME
#define INITIALIZE(address, name, child, callback) (void)BB_GAME_CALL(address, 0);
#else
#define INITIALIZE(address, name, child, callback) (void)name();
#endif
  BB_STATIC_INITIALIZERS(INITIALIZE)
#undef INITIALIZE
}
void bb_crt_boot_shutdown_entry(uint32_t code, int32_t quick,
                                int32_t returning) {
  if (BB_U32(0x48cbf0) == 1) {
#ifdef BB_MODEL_RUNTIME
    uint32_t process = BB_GAME_CALL(BB_U32(0x6ca46c), 0);
    BB_GAME_CALL(BB_U32(0x6ca47c), 0, process, code);
#else
    void *process = ((void *(WIN *)(void))BB_CODE(BB_POINTER(0x6ca46c)))();
    ((int32_t(WIN *)(void *, uint32_t))BB_CODE(BB_POINTER(0x6ca47c)))(process, code);
#endif
  }
  BB_U32(0x48cbec) = 1;
  BYTE(0x48cbe8) = returning;
  if (!quick) {
    if (BB_U32(0x6c9bc4)) {
      uintptr_t p = BB_U32(0x6c9bc0);
      while ((p -= 4) >= BB_U32(0x6c9bc4)) {
        if (BB_U32(p))
          callback(BB_U32(p));
      }
    }
    bb_crt_boot_end_stdio_entry();
  }
  if (!BB_U32(0x48cbf4) && (bb_crt_debug_set_flags_entry(-1) & 32)) {
    BB_U32(0x48cbf4) = 1;
    bb_crt_debug_dump_leaks_entry();
  }
  if (!returning) {
    BB_U32(0x48cbf0) = 1;
#ifdef BB_MODEL_RUNTIME
    BB_GAME_CALL(BB_U32(0x6ca474), 0, code);
#else
    ((void(WIN *)(uint32_t))BB_CODE(BB_POINTER(0x6ca474)))(code);
#endif
  }
}
void bb_crt_boot_exit_entry(uint32_t code) {
  bb_crt_boot_shutdown_entry(code, 0, 0);
}
void bb_crt_boot_quick_exit_entry(uint32_t code) {
  bb_crt_boot_shutdown_entry(code, 1, 0);
}
void bb_crt_boot_fatal_entry(int32_t message) {
  if (BB_U32(0x48cc5c) == 1)
    bb_crt_errors_banner_entry();
  bb_crt_errors_message_entry(message);
#ifdef BB_MODEL_RUNTIME
  BB_GAME_CALL(BB_U32(0x48cc58), 0, 255);
#else
  ((void (*)(uint32_t))BB_CODE(BB_POINTER(0x48cc58)))(255);
#endif
}
void bb_crt_boot_noop_entry(void) {}
void *bb_crt_boot_onexit_entry(void *function) {
  uint32_t size = bb_crt_size_debug_entry(BB_POINTER(0x6c9bc4), 2);
  if (size < BB_U32(0x6c9bc0) + (4 - BB_U32(0x6c9bc4))) {
    size = bb_crt_size_debug_entry(BB_POINTER(0x6c9bc4), 2);
    void *p = bb_crt_reallocate_debug_entry(BB_POINTER(0x6c9bc4), size + 16, 2,
                                            STR(0x48253c), 104);
    if (!p)
      return NULL;
    BB_U32(0x6c9bc0) = (BB_U32(0x6c9bc0) - BB_U32(0x6c9bc4)) & 0xfffffffc;
    BB_U32(0x6c9bc0) += bb_address_encode(p);
    BB_SET_POINTER(0x6c9bc4, p);
  }
  BB_SET_POINTER(BB_U32(0x6c9bc0), function);
  BB_U32(0x6c9bc0) += 4;
  return function;
}
int32_t bb_crt_boot_atexit_entry(void *function) {
  return bb_crt_boot_onexit_entry(function) ? 0 : -1;
}
void bb_crt_boot_exit_table_entry(void) {
  BB_SET_POINTER(0x6c9bc4, bb_crt_malloc_debug_entry(128, 2, STR(0x48253c), 182));
  if (!BB_U32(0x6c9bc4))
    bb_crt_boot_fatal_entry(24);
  BB_U32(BB_U32(0x6c9bc4)) = 0;
  BB_U32(0x6c9bc0) = BB_U32(0x6c9bc4);
}
