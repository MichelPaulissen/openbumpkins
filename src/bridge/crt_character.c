/* SPDX-License-Identifier: GPL-3.0-only */
#include "crt_locale.h"
#include "game_calls.h"
#include "interop.h"
#ifdef BB_MODEL_RUNTIME
#include "model_buffer.h"
#endif
static int32_t encoding_error(void) {
  BB_U32(0x48cba8) = 42;
  return -1;
}
int32_t bb_crt_locale_wctomb_entry(char *out, uint32_t ch) {
  if (!out)
    return 0;
  uint16_t c = ch;
  if (!BB_U32(0x48cfc0)) {
    if (c >= 256)
      return encoding_error();
    BB_U8(out) = c;
    return 1;
  }
  uint32_t used = 0;
  int32_t count = bb_crt_locale_wide_bytes(BB_U32(0x48cfd0), 0x220, &c, 1, out,
                                           BB_U32(0x48d35c), &used);
  return !count || used ? encoding_error() : count;
}
