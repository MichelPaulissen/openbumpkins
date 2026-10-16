/* SPDX-License-Identifier: GPL-3.0-only */
#include "game_audio_models.h"
#include "terrain_models.h"
const struct bb_model_target *bb_game_audio_model_find(void *unused,
                                                        uint32_t identity) {
  (void)unused;
  static const struct bb_model_target targets[] = {
#define TARGET(id, body, entry, argc, reserve) {id, body, argc, reserve, 0},
      BB_GAME_AUDIO_METHODS(TARGET)
#undef TARGET
  };
  for (unsigned n = 0; n < sizeof targets / sizeof *targets; n++)
    if (targets[n].identity == identity)
      return &targets[n];
  /* Audio shares the existing random and distance leaves with terrain. */
  return bb_terrain_model_find(NULL, identity);
}
