/* SPDX-License-Identifier: GPL-3.0-only */
#include "qmixer_internal.h"
#include "qmixer_api_models.h"
#include "qmixer_lifetime.h"
#include <stdio.h>
#include <string.h>

void bb_qmix_model_api_call(void (*body)(uint32_t *), uint32_t *input,
                            unsigned frame, unsigned high,
                            void (*wave_error)(uint32_t *),
                            void (*memory_error)(uint32_t *)) {
  uint32_t owner = input[3] + frame - 12u;
  struct bb_qmix_model_error error;
  if (bb_qmix_model_try_body(body, input, owner, &error))
    return;
  void (*action)(uint32_t *) = error.information == 0x1001ae90u ? wave_error
                              : error.information == 0x1001ad90u ? memory_error
                                                                : NULL;
  int32_t stage = BB_I32(owner + 8u);
  if (!action || stage < 0 || (unsigned)stage > high)
    bb_qmix_model_raise_error(error);

  /* The protected operation's lock is released before its catch runs. The
   * native error value replaces the temporary MSVC catch-object copy. */
  bb_qmix_model_cleanup_to(owner, 0);
  BB_U32(owner + 8u) = high + 1u;
  uint32_t recovery[10];
  memcpy(recovery, input, sizeof recovery);
  recovery[2] = owner + 12u;
  recovery[3] = input[3] + frame - 2048u;
  bb_qmix_model_catch_body(action, recovery, error);
  switch (recovery[7]) {
#define RESUME(address, name, result, stage, cleanup, pops)                     \
  case address:                                                               \
    name##_c(recovery);                                                        \
    break;
    BB_QMIX_API_RETURNS(RESUME)
#undef RESUME
  default:
    fputs("bumpkins: invalid mixer C API recovery action\n", stderr);
    abort();
  }
  input[5] = recovery[5];
  input[6] = recovery[6];
  input[7] = recovery[7];
}
