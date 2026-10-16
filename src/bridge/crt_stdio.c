/* SPDX-License-Identifier: GPL-3.0-only */
#include "crt_bootstrap.h"
#include "crt_debug_heap.h"
#include "crt_stream.h"
#include "game_calls.h"
#define BYTE(a) BB_U8(a)
#define STR(a) ((const char *)(uintptr_t)(a))
#ifdef BB_MODEL_RUNTIME
#define CALLOC(count, size, type, file, line)                                  \
  ((void *)(uintptr_t)BB_GAME_CALL(0x0046dad0u, 0, count, size, type,          \
                                   bb_address_encode(file), line))
#define FREE(p, type) BB_GAME_CALL(0x0046e130u, 0, bb_address_encode(p), type)
#define FATAL(code) BB_GAME_CALL(0x0046d620u, 0, code)
#else
#define CALLOC bb_crt_calloc_debug_entry
#define FREE bb_crt_free_debug_entry
#define FATAL bb_crt_boot_fatal_entry
#endif
void bb_crt_boot_stdio_entry(void) {
  if (!BB_U32(0x6c9bb0))
    BB_U32(0x6c9bb0) = 512;
  else if (BB_U32(0x6c9bb0) < 20)
    BB_U32(0x6c9bb0) = 20;
  BB_SET_POINTER(0x6c8ba4, CALLOC(BB_U32(0x6c9bb0), 4, 2, STR(0x482f50), 132));
  if (!BB_U32(0x6c8ba4)) {
    BB_U32(0x6c9bb0) = 20;
    BB_SET_POINTER(0x6c8ba4, CALLOC(20, 4, 2, STR(0x482f50), 135));
    if (!BB_U32(0x6c8ba4))
      FATAL(26);
  }
  for (unsigned n = 0; n < 20; ++n)
    BB_U32(BB_U32(0x6c8ba4) + n * 4) = 0x48cd08 + n * 32;
  for (unsigned n = 0; n < 3; ++n) {
    uint32_t handle = BB_U32(BB_U32(0x6c8aa0) + n * 8);
    if (handle == 0 || handle == UINT32_MAX)
      BB_U32(0x48cd18 + n * 32) = UINT32_MAX;
  }
}
uint32_t bb_crt_boot_close_streams_entry(void) {
  uint32_t closed = 0;
  for (uint32_t n = 3; n < BB_U32(0x6c9bb0); ++n) {
    if (BB_U32(BB_U32(0x6c8ba4) + n * 4)) {
      if (BYTE(BB_U32(BB_U32(0x6c8ba4) + n * 4) + 12) & 0x83) {
        if (bb_crt_stream_close_entry(BB_POINTER(BB_U32(0x6c8ba4) + n * 4)) !=
            -1)
          ++closed;
      }
      if (n > 19) {
        FREE(BB_POINTER(BB_U32(0x6c8ba4) + n * 4), 2);
        BB_U32(BB_U32(0x6c8ba4) + n * 4) = 0;
      }
    }
  }
  return closed;
}
void bb_crt_boot_end_stdio_entry(void) {
  bb_crt_stream_flush_all_entry();
  if (BYTE(0x48cbe8))
    bb_crt_boot_close_streams_entry();
}
