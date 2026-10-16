/* SPDX-License-Identifier: GPL-3.0-only */
#include "message_models.h"
extern void bb_faction_attack_alert_c(uint32_t *);
extern void bb_message_append_c(uint32_t *);
extern void bb_message_construct_c(uint32_t *);
extern void bb_message_current_c(uint32_t *);
extern void bb_message_destroy_c(uint32_t *);
extern void bb_message_destructor_c(uint32_t *);
extern void bb_message_draw_c(uint32_t *);
extern void bb_message_input_c(uint32_t *);
extern void bb_message_next_c(uint32_t *);
extern void bb_message_self_c(uint32_t *);
extern void bb_message_tick_c(uint32_t *);
extern void bb_notification_clear_c(uint32_t *);
extern void bb_notification_draw_c(uint32_t *);
extern void bb_notification_enqueue_c(uint32_t *);
extern void bb_notification_entity_c(uint32_t *);
extern void bb_notification_number_c(uint32_t *);
extern void bb_notification_tick_c(uint32_t *);
const struct bb_model_target *bb_message_model_find(void *unused, uint32_t id) {
  (void)unused;
  static const struct bb_model_target targets[] = {
      {0x41e2a0, bb_message_construct_c, 0, 0, 0},
      {0x41e2d0, bb_message_destructor_c, 0, 0, 0},
      {0x41e2e0, bb_message_destroy_c, 0, 0, 0},
      {0x41e310, bb_message_append_c, 2, 0, 0},
      {0x41e3c0, bb_message_current_c, 1, 0, 0},
      {0x41e440, bb_message_draw_c, 0, 0, 0},
      {0x41e4f0, bb_message_next_c, 0, 512, 0},
      {0x41e5a0, bb_message_tick_c, 0, 0, 0},
      {0x41e6a0, bb_message_input_c, 2, 0, 0},
      {0x41e6b0, bb_message_self_c, 0, 0, 0},
      {0x41e090, bb_notification_clear_c, 0, 0, 0},
      {0x41e0b0, bb_notification_draw_c, 0, 16, 0},
      {0x41e150, bb_notification_tick_c, 0, 0, 0},
      {0x41e180, bb_notification_number_c, 2, 256, 0},
      {0x41e1f0, bb_notification_entity_c, 2, 0, 0},
      {0x41e220, bb_notification_enqueue_c, 3, 0, 0},
      {0x429190, bb_faction_attack_alert_c, 2, 12, 0},
  };
  for (unsigned n = 0; n < sizeof targets / sizeof *targets; n++)
    if (targets[n].identity == id)
      return &targets[n];
  return NULL;
}
