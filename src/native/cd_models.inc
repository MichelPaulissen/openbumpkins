/* SPDX-License-Identifier: GPL-3.0-only */
#include "../bridge/interop.h"
#include "cd_models.h"
static void model_command(uint32_t *r) {
  uint32_t p = r[3] + 8u, command = BB_U32(p), result = BB_U32(p + 4u);
  uint32_t capacity = BB_U32(p + 8u), window = BB_U32(p + 12u);
  r[7] = bb_cd_command(command ? BB_DATA(command) : NULL,
                       result && capacity ? BB_SPAN(result, capacity) : NULL,
                       capacity, (Handle)(uintptr_t)window);
}
uint32_t bb_cd_model_export(const char *name) {
  return !strcmp(name, "mciSendStringA") ? BB_CD_MODEL_API : 0;
}
const struct bb_model_target *bb_cd_model_find(void *unused, uint32_t id) {
  (void)unused;
  static const struct bb_model_target t = {BB_CD_MODEL_API, model_command, 4, 0,
                                           0};
  return id == t.identity ? &t : NULL;
}
