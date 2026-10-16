/* SPDX-License-Identifier: GPL-3.0-only */
#include "entity_models.h"
#include "interop.h"
#include "road_topology.h"
#include "script.h"
#include "world_controls.h"
/* These adapters replace the original register/stack entry shims. The C
 * workers below retain their existing game logic and real child identities. */
#define A(n) BB_U32(r[3] + reserve + 8u + (n) * 4u)
#define P(n) ((void *)(uintptr_t)A(n))
#define S ((void *)(uintptr_t)self)
#define V(expr)                                                                \
  do {                                                                         \
    uint64_t value = (expr);                                                   \
    r[7] = value;                                                              \
    r[5] = value >> 32;                                                        \
  } while (0)
static uint32_t logic(uint32_t v, unsigned bits) {
  uint32_t mask = bits == 32 ? UINT32_MAX : (1u << bits) - 1u;
  v &= mask;
  return (!v ? 0x40u : 0u) | ((v >> (bits - 1) & 1u) << 7) |
         (__builtin_parity((uint8_t)v) ? 0u : 4u);
}
static uint32_t flags_logic(uint32_t f, uint32_t v, unsigned bits) {
  return (f & ~0x8d5u) | logic(v, bits);
}
static uint32_t flags_sub(uint32_t f, uint32_t a, uint32_t b, unsigned bits) {
  uint32_t sign = 1u << (bits - 1);
  return (f & ~0x8d5u) | logic(a - b, bits) | (a < b) |
         ((a ^ b ^ (a - b)) & 16u) |
         (((a ^ b) & (a ^ (a - b)) & sign) ? 0x800u : 0u);
}
static uint32_t flags_add(uint32_t f, uint32_t a, uint32_t b) {
  uint32_t v = a + b;
  return (f & ~0x8d5u) | logic(v, 32) | (v < a) | ((a ^ b ^ v) & 16u) |
         ((~(a ^ b) & (a ^ v) & 0x80000000u) ? 0x800u : 0u);
}
extern void bb_entity_main_c(uint32_t *);
extern void bb_entity_area_damage_c(uint32_t *);
extern void bb_entity_attack_begin_c(uint32_t *);
extern void bb_entity_attack_tick_c(uint32_t *);
extern void bb_entity_damage_c(uint32_t *);
extern void bb_entity_subtype_clear_c(uint32_t *);
extern void bb_entity_attack_range_c(uint32_t *);
extern void bb_entity_target_moved_c(uint32_t *);
static void model_bb_entity_profession(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_profession((void *)(uintptr_t)self, A(0));
}
static void model_bb_entity_upgrade_parts(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_upgrade_parts((void *)(uintptr_t)self);
}
static void model_bb_entity_refresh_tiles(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_refresh_tiles((void *)(uintptr_t)self);
}
static void model_bb_entity_register_tiles(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_register_tiles((void *)(uintptr_t)self);
}
static void model_bb_entity_update_tile(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_update_tile((void *)(uintptr_t)self);
}
static void model_bb_entity_name_title(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_name_title((void *)(uintptr_t)self, (char *)(uintptr_t)A(0));
}
static void model_bb_entity_name(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_name((void *)(uintptr_t)self, (char *)(uintptr_t)A(0));
}
static void model_bb_entity_birth(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_birth((void *)(uintptr_t)self, (void *)(uintptr_t)A(0),
                         (void *)(uintptr_t)A(1));
}
static void model_bb_entity_clear_subtype(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_clear_subtype((void *)(uintptr_t)self);
}
static void model_bb_entity_subtype(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_subtype((void *)(uintptr_t)self, A(0));
}
static void model_bb_entity_sound(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_sound((void *)(uintptr_t)self, A(0));
}
static void model_bb_entity_initialize(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_initialize((void *)(uintptr_t)self, A(0), A(1), A(2), A(3));
}
static void model_bb_entity_command_position(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_command_position((void *)(uintptr_t)self);
}
static void model_bb_entity_move_goal(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_move_goal((void *)(uintptr_t)self, A(0), A(1));
}
static void model_bb_entity_move_near(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] =
      bb_entity_move_near((void *)(uintptr_t)self, (uint8_t *)(uintptr_t)A(0),
                          (uint8_t *)(uintptr_t)A(1));
}
static void model_bb_entity_move(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_move((void *)(uintptr_t)self, A(0), A(1));
}
static void model_bb_entity_move_escape(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_move_escape((void *)(uintptr_t)self, A(0), A(1));
}
static void model_bb_entity_reset_move(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_reset_move((void *)(uintptr_t)self);
}
static void model_bb_entity_command_sound(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_command_sound((void *)(uintptr_t)self, A(0));
}
static void model_bb_entity_route(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_route((void *)(uintptr_t)self, A(0), A(1));
}
static void model_bb_entity_release_tiles(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_release_tiles((void *)(uintptr_t)self);
}
static void model_bb_entity_resume(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_resume((void *)(uintptr_t)self);
}
static void model_bb_entity_place_tile(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_place_tile((void *)(uintptr_t)self, A(0), A(1));
}
static void model_bb_entity_place(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_place((void *)(uintptr_t)self, A(0), A(1));
}
static void model_bb_entity_action_begin(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_action_begin((void *)(uintptr_t)self);
}
static void model_bb_entity_cargo(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_cargo((void *)(uintptr_t)self, (void *)(uintptr_t)A(0));
}
static void model_bb_entity_path_step(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_path_step((void *)(uintptr_t)self);
}
static void model_bb_entity_destination_step(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_destination_step((void *)(uintptr_t)self);
}
static void model_bb_entity_heal(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_heal((void *)(uintptr_t)self, A(0));
}
static void model_bb_entity_idle_sound(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_idle_sound((void *)(uintptr_t)self);
}
static void model_bb_entity_arrive(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_arrive((void *)(uintptr_t)self);
}
static void model_bb_entity_can_consume(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_can_consume((void *)(uintptr_t)self, A(0), A(1));
}
static void model_bb_entity_can_steal(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_can_steal((void *)(uintptr_t)self, A(0), A(1));
}
static void model_bb_entity_consume(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_consume((void *)(uintptr_t)self, A(0), A(1));
}
static void model_bb_entity_steal(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_steal((void *)(uintptr_t)self, A(0), A(1));
}
static void model_bb_entity_feedback_delay(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_feedback_delay((void *)(uintptr_t)self, A(0), A(1));
}
static void model_bb_entity_feedback_queue(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_feedback_queue((void *)(uintptr_t)self, A(0),
                                  (void *)(uintptr_t)A(1));
}
static void model_bb_entity_partner_link(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] =
      bb_entity_partner_link((void *)(uintptr_t)self, (void *)(uintptr_t)A(0));
}
static void model_bb_entity_partner_begin(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] =
      bb_entity_partner_begin((void *)(uintptr_t)self, (void *)(uintptr_t)A(0));
}
static void model_bb_entity_partner_name(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] =
      bb_entity_partner_name((void *)(uintptr_t)self, (void *)(uintptr_t)A(0));
}
static void model_bb_entity_partner_clear(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_partner_clear((void *)(uintptr_t)self);
}
static void model_bb_entity_partner_find(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_address_encode(bb_entity_partner_find((void *)(uintptr_t)self));
}
static void model_bb_entity_home_nearest(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_address_encode(bb_entity_home_nearest((void *)(uintptr_t)self));
}
static void model_bb_entity_child_site(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_address_encode(bb_entity_child_site((void *)(uintptr_t)self));
}
static void model_bb_entity_stat(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_stat((void *)(uintptr_t)self, A(0), A(1));
}
static void model_bb_entity_cast(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_cast((void *)(uintptr_t)self, A(0), A(1), A(2));
}
static void model_bb_entity_city_name(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_address_encode(bb_entity_city_name((void *)(uintptr_t)self));
}
static void model_bb_entity_treatment_fee(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_treatment_fee((void *)(uintptr_t)self, A(0));
}
static void model_bb_entity_event_feedback(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_event_feedback((void *)(uintptr_t)self, A(0), A(1));
}
static void model_bb_entity_contact(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_contact((void *)(uintptr_t)self, (void *)(uintptr_t)A(0));
}
static void model_bb_entity_pool_tick(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_pool_tick((void *)(uintptr_t)self);
}
static void model_bb_entity_decision_tick(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_decision_tick((void *)(uintptr_t)self);
}
static void model_bb_entity_work_auto(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_work_auto((void *)(uintptr_t)self);
}
static void model_bb_entity_work_retry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_work_retry((void *)(uintptr_t)self);
}
static void model_bb_entity_target_building(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_target_building((void *)(uintptr_t)self);
}
static void model_bb_entity_target_last(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_target_last((void *)(uintptr_t)self);
}
static void model_bb_entity_target_weak(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_target_weak((void *)(uintptr_t)self);
}
static void model_bb_entity_target_strong(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_target_strong((void *)(uintptr_t)self);
}
static void model_bb_entity_target_strong_alt(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_target_strong_alt((void *)(uintptr_t)self);
}
static void model_bb_entity_enemy_command(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_enemy_command((void *)(uintptr_t)self);
}
static void model_bb_entity_enemy_evade(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_enemy_evade((void *)(uintptr_t)self);
}
static void model_bb_entity_doctor_find(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_doctor_find((void *)(uintptr_t)self);
}
static void model_bb_entity_mage_offense(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_mage_offense((void *)(uintptr_t)self);
}
static void model_bb_entity_mage_support(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_mage_support((void *)(uintptr_t)self);
}
static void model_bb_entity_idle_action(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_idle_action((void *)(uintptr_t)self);
}
static void model_bb_entity_work_wood(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_address_encode(bb_entity_work_wood((void *)(uintptr_t)self));
}
static void model_bb_entity_work_stone(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_address_encode(bb_entity_work_stone((void *)(uintptr_t)self));
}
static void model_bb_entity_work_hunt(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_address_encode(bb_entity_work_hunt((void *)(uintptr_t)self));
}
static void model_bb_entity_work_clear(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_address_encode(bb_entity_work_clear((void *)(uintptr_t)self));
}
static void model_bb_entity_work_field(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_address_encode(bb_entity_work_field((void *)(uintptr_t)self));
}
static void model_bb_entity_work_repair(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_address_encode(bb_entity_work_repair((void *)(uintptr_t)self));
}
static void model_bb_entity_need_food(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_address_encode(bb_entity_need_food((void *)(uintptr_t)self));
}
static void model_bb_entity_need_water(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_address_encode(bb_entity_need_water((void *)(uintptr_t)self));
}
static void model_bb_entity_order_site(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_address_encode(bb_entity_order_site((void *)(uintptr_t)self));
}
static void model_bb_entity_pair_allowed(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_pair_allowed((void *)(uintptr_t)self,
                                (void *)(uintptr_t)A(0), A(1));
}
static void model_bb_entity_clown_action(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_clown_action((void *)(uintptr_t)self);
}
static void model_bb_entity_morale_shock(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_morale_shock((void *)(uintptr_t)self);
}
static void model_bb_entity_home_idle(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_home_idle((void *)(uintptr_t)self);
}
static void model_bb_faction_group_cache(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_faction_group_cache((void *)(uintptr_t)self, A(0));
}
static void model_bb_faction_group_refresh(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_faction_group_refresh((void *)(uintptr_t)self, A(0));
}
static void model_bb_faction_stage(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_faction_stage((void *)(uintptr_t)self, A(0));
}
static void model_bb_faction_group_ready(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_faction_group_ready((void *)(uintptr_t)self, A(0));
}
static void model_bb_faction_plan_seed(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_faction_plan_seed((void *)(uintptr_t)self);
}
static void model_bb_faction_plan(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_faction_plan((void *)(uintptr_t)self);
}
static void model_bb_faction_workforce(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_faction_workforce((void *)(uintptr_t)self);
}
static void model_bb_faction_military(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_faction_military((void *)(uintptr_t)self);
}
static void model_bb_faction_ai(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_faction_ai((void *)(uintptr_t)self);
}
static void model_bb_faction_group_cancel(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] =
      bb_faction_group_cancel((void *)(uintptr_t)self, (void *)(uintptr_t)A(0));
}
static void model_bb_faction_flag(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_faction_flag((void *)(uintptr_t)self, A(0), A(1));
}
static void model_bb_faction_selection_remove(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_faction_selection_remove((void *)(uintptr_t)self,
                                     (void *)(uintptr_t)A(0));
}
static void model_bb_faction_selection_add(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_faction_selection_add((void *)(uintptr_t)self,
                                  (void *)(uintptr_t)A(0), A(1));
}
static void model_bb_faction_group_remove(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] =
      bb_faction_group_remove((void *)(uintptr_t)self, (void *)(uintptr_t)A(0));
}
static void model_bb_faction_can_build(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_faction_can_build((void *)(uintptr_t)self, A(0));
}
static void model_bb_faction_build_allowed(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_faction_build_allowed((void *)(uintptr_t)self, A(0));
}
static void model_bb_faction_construction(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_faction_construction((void *)(uintptr_t)self, A(0),
                                 (void *)(uintptr_t)A(1), A(2));
}
static void model_bb_entity_build(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_address_encode(bb_entity_build(A(0), A(1), A(2), A(3), A(4)));
}
static void model_bb_direction_step_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_direction_step_entry(A(0), A(1));
}
static void model_bb_building_footprint_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  bb_building_footprint_entry(A(0), A(1), A(2));
}
static void model_bb_entity_remove_kind_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  bb_entity_remove_kind_entry((void *)(uintptr_t)self, A(0));
}
static void model_bb_entity_select_rect_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  bb_entity_select_rect_entry((void *)(uintptr_t)self, A(0), A(1), A(2), A(3));
}
static void model_bb_entity_depth_check_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  bb_entity_depth_check_entry((void *)(uintptr_t)self);
}
static void model_bb_entity_pair_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_pair_entry((void *)(uintptr_t)self, (void *)(uintptr_t)A(0));
}
static void model_bb_neighbour_kind_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_neighbour_kind_entry(A(0), A(1), A(2));
}
static void model_bb_junction_refresh_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  bb_junction_refresh_entry(A(0), A(1), A(2));
}
static void model_bb_road_refresh_cardinal_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  bb_road_refresh_cardinal_entry(A(0), A(1));
}
static void model_bb_road_add_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  bb_road_add_entry(A(0), A(1));
}
static void model_bb_road_ensure_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  bb_road_ensure_entry(A(0), A(1), A(2));
}
static void model_bb_road_route_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  bb_road_route_entry(A(0), A(1), A(2), A(3), A(4));
}
static void model_bb_entity_data_construct_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_data_construct(S);
}
static void model_bb_entity_speed_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  V(bb_entity_speed_c(S));
  r[6] = BB_U8(self + 0xd8);
}
static void model_bb_entity_target_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  V(bb_entity_target_c(S, A(0), r[5]));
}
static void model_bb_entity_first_name_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  (void)r;
}
static void model_bb_entity_last_name_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  (void)r;
}
static void model_bb_entity_blockers_add_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_blockers_c(S, 1, r[7]);
}
static void model_bb_entity_blockers_remove_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_blockers_c(S, UINT32_MAX, r[7]);
}
static void model_bb_entity_move_start_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  V(bb_entity_move_start_c(S, r[7], r[5]));
}
static void model_bb_entity_move_stop_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  V(bb_entity_move_stop_c(S));
}
static void model_bb_entity_recruit_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_recruit_c(S, P(0), r[7]);
}
static void model_bb_entity_evade_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_evade_c(S, P(0), r[7]);
}
static void model_bb_entity_command_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_command_c(S, P(0), A(1), A(2), r[7]);
}
static void model_bb_entity_walk_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_walk_c(S, A(0), A(1), A(2), r[7]);
}
static void model_bb_entity_feedback_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_feedback_c(S, P(0), A(1), A(2), r[7]);
}
static void model_bb_entity_approach_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_approach_c(S, P(0), A(1), r[7]);
}
static void model_bb_entity_check_arrival_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_check_arrival_c(S, r[7]);
}
static void model_bb_entity_action_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  V(bb_entity_action_c(S, A(0), A(1), r[7], r[5]));
  r[8] = flags_sub(r[8], BB_U8(self + 0xf2), 0x73, 8);
}
static void model_bb_entity_secondary_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  V(bb_entity_secondary_c(S, P(0)));
  r[8] = flags_logic(r[8], r[5], 32);
}
static void model_bb_entity_trade_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_trade_c(S, A(0), A(1), A(2), A(3), r[7]);
}
static void model_bb_entity_transfer_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_transfer_c(S, A(0), A(1), r[7]);
}
static void model_bb_entity_activity_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_activity_c(S, A(0), P(1), r[7]);
}
static void model_bb_entity_attack_tile_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_attack_tile_c(S, A(0), A(1), r[7]);
}
static void model_bb_entity_kind_tick_entry(uint32_t *r) {
  enum { reserve = 4 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_kind_tick_c(S, BB_U32(r[3] + 4));
}
static void model_bb_entity_human_ai_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_entity_human_ai_c(S, r[7]);
}
static void model_bb_faction_recount_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  uint64_t v = bb_faction_recount_c(S);
  r[7] = v;
  r[6] = v >> 32;
  r[5] = 0;
  r[8] = flags_logic(r[8], 0, 32);
}
static void model_bb_faction_strength_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  V(bb_faction_strength_c(S));
  r[8] = flags_add(r[8], r[5] - r[7], r[7]);
}
static void model_bb_faction_selection_clear_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_faction_selection_clear_c(S, r[7]);
}
static void model_bb_faction_group_command_entry(uint32_t *r) {
  enum { reserve = 8 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_faction_group_command_c(S, A(0), A(1), A(2), BB_U8(r[3] + 7), r[7]);
}
static void model_bb_faction_buildings_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_faction_buildings_c(S, r[7]);
}
static void model_bb_faction_idle_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  (void)r;
}
static void model_bb_faction_group_restore_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_faction_group_restore_c(S, A(0), r[7]);
}
static void model_bb_faction_recruit_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_faction_recruit_c(S, A(0), r[7]);
}
static void model_bb_faction_attack_entry(uint32_t *r) {
  enum { reserve = 8 };
  uint32_t self __attribute__((unused)) = r[6];
  r[7] = bb_faction_attack_c(S, A(0), BB_U8(r[3] + 5), r[7]);
}
static void model_bb_building_road_connect_entry(uint32_t *r) {
  enum { reserve = 44 };
  uint32_t self __attribute__((unused)) = r[6];
  V(bb_building_road_connect_c(S, BB_U32(r[3] + 40), BB_U32(r[3] + 4)));
  BB_U32(r[3] + 40) = r[7];
  BB_U32(r[3] + 4) = r[5];
}
static void model_bb_entity_adjust_health_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  struct bb_entity_adjust_result value;
  bb_entity_adjust_c(S, 0xa0, A(0), A(1), r[5], &value);
  r[7] = value.eax;
  r[5] = value.edx;
  r[6] = value.ecx;
  r[8] = (r[8] & ~0x8d5u) | value.flags;
}
static void model_bb_entity_adjust_109_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  struct bb_entity_adjust_result value;
  bb_entity_adjust_c(S, 0x109, A(0), A(1), r[5], &value);
  r[7] = value.eax;
  r[5] = value.edx;
  r[6] = value.ecx;
  r[8] = (r[8] & ~0x8d5u) | value.flags;
}
static void model_bb_entity_adjust_113_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  struct bb_entity_adjust_result value;
  bb_entity_adjust_c(S, 0x113, A(0), 0, r[5], &value);
  r[7] = value.eax;
  r[5] = value.edx;
  r[6] = value.ecx;
  r[8] = (r[8] & ~0x8d5u) | value.flags;
}
static void model_bb_entity_adjust_111_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  struct bb_entity_adjust_result value;
  bb_entity_adjust_c(S, 0x111, A(0), A(1), r[5], &value);
  r[7] = value.eax;
  r[5] = value.edx;
  r[6] = value.ecx;
  r[8] = (r[8] & ~0x8d5u) | value.flags;
}
static void model_bb_entity_adjust_10d_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  struct bb_entity_adjust_result value;
  bb_entity_adjust_c(S, 0x10d, A(0), A(1), r[5], &value);
  r[7] = value.eax;
  r[5] = value.edx;
  r[6] = value.ecx;
  r[8] = (r[8] & ~0x8d5u) | value.flags;
}
static void model_bb_entity_adjust_10f_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  struct bb_entity_adjust_result value;
  bb_entity_adjust_c(S, 0x10f, A(0), A(1), r[5], &value);
  r[7] = value.eax;
  r[5] = value.edx;
  r[6] = value.ecx;
  r[8] = (r[8] & ~0x8d5u) | value.flags;
}
static void model_bb_entity_adjust_100_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  struct bb_entity_adjust_result value;
  bb_entity_adjust_c(S, 0x100, A(0), 0, r[5], &value);
  r[7] = value.eax;
  r[5] = value.edx;
  r[6] = value.ecx;
  r[8] = (r[8] & ~0x8d5u) | value.flags;
}
static void model_bb_entity_adjust_101_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  struct bb_entity_adjust_result value;
  bb_entity_adjust_c(S, 0x101, A(0), 0, r[5], &value);
  r[7] = value.eax;
  r[5] = value.edx;
  r[6] = value.ecx;
  r[8] = (r[8] & ~0x8d5u) | value.flags;
}
static void model_bb_entity_adjust_11b_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  struct bb_entity_adjust_result value;
  bb_entity_adjust_c(S, 0x11b, A(0), 0, r[5], &value);
  r[7] = value.eax;
  r[5] = value.edx;
  r[6] = value.ecx;
  r[8] = (r[8] & ~0x8d5u) | value.flags;
}
static void model_bb_entity_adjust_resources_entry(uint32_t *r) {
  enum { reserve = 0 };
  uint32_t self __attribute__((unused)) = r[6];
  struct bb_entity_adjust_result value;
  bb_entity_adjust_c(S, 0x38, A(0), 0, r[5], &value);
  r[7] = value.eax;
  r[5] = value.edx;
  r[6] = value.ecx;
  r[8] = (r[8] & ~0x8d5u) | value.flags;
}
static void model_script_event(uint32_t *r) {
  bb_script_event_execute_entry((void *)(uintptr_t)r[6]);
}
static void model_script_conditions(uint32_t *r) {
  r[7] = bb_script_condition_evaluate_entry(
      (void *)(uintptr_t)r[6], BB_U32(r[3] + 8),
      (void *)(uintptr_t)BB_U32(r[3] + 12));
}
static void model_script_block(uint32_t *r) {
  bb_script_block_tick_entry((void *)(uintptr_t)r[6], BB_U32(r[3] + 8));
}
static void model_script_tick(uint32_t *r) {
  bb_script_tick_entry((void *)(uintptr_t)r[6]);
}
static void model_script_key(uint32_t *r) {
  r[7] = bb_script_key_entry((void *)(uintptr_t)r[6], BB_U32(r[3] + 8));
}
static void model_placement_probe(uint32_t *r) {
  enum { reserve = 0 };
  r[7] = bb_placement_probe(A(0), A(1), A(2), A(3));
}
static void model_placement_clear(uint32_t *r) {
  enum { reserve = 0 };
  r[7] = bb_placement_clear(A(0), A(1), A(2), A(3));
}
static void model_placement_route(uint32_t *r) {
  enum { reserve = 0 };
  r[7] = bb_placement_route(A(0), A(1), A(2), A(3), A(4), A(5));
}
static void model_placement_reachable(uint32_t *r) {
  enum { reserve = 0 };
  r[7] = bb_placement_reachable(A(0), A(1), A(2), A(3), A(4));
}
static void model_placement_preview_clear(uint32_t *r) {
  r[7] = bb_placement_preview_clear();
}
static void model_placement_connect(uint32_t *r) {
  enum { reserve = 0 };
  r[7] = bb_placement_connect(A(0), A(1), A(2), A(3));
}
static const struct bb_model_target targets[] = {
    {0x4033f0, model_placement_probe, 4, 0, 0},
    {0x403950, model_placement_clear, 4, 0, 0},
    {0x417700, model_placement_route, 6, 0, 0},
    {0x417c90, model_placement_reachable, 5, 0, 0},
    {0x418300, model_placement_preview_clear, 0, 0, 0},
    {0x418470, model_placement_connect, 4, 0, 0},
    {0x422670, model_script_event, 0, 0, 0},
    {0x423060, model_script_conditions, 2, 0, 0},
    {0x422b20, model_script_block, 1, 0, 0},
    {0x426a60, model_script_tick, 0, 0, 0},
    {0x4268d0, model_script_key, 1, 0, 0},
    {0x4031a0, model_bb_direction_step_entry, 2, 0, 0},
    {0x403320, model_bb_building_footprint_entry, 3, 0, 0},
    {0x416960, model_bb_junction_refresh_entry, 3, 0, 0},
    {0x417060, model_bb_neighbour_kind_entry, 3, 0, 0},
    {0x4170c0, model_bb_road_route_entry, 5, 0, 0},
    {0x417660, model_bb_road_ensure_entry, 3, 0, 0},
    {0x4176a0, model_bb_road_add_entry, 2, 0, 0},
    {0x418360, model_bb_road_refresh_cardinal_entry, 2, 0, 0},
    {0x41c290, model_bb_entity_data_construct_entry, 0, 0, 0},
    {0x41ce80, model_bb_faction_can_build, 1, 0, 0},
    {0x41cec0, model_bb_faction_build_allowed, 1, 0, 0},
    {0x427530, model_bb_faction_group_cancel, 1, 0, 0},
    {0x427560, model_bb_faction_selection_add, 2, 0, 0},
    {0x427760, model_bb_faction_selection_remove, 1, 0, 0},
    {0x427810, model_bb_faction_selection_clear_entry, 0, 0, 0},
    {0x427880, model_bb_faction_group_cache, 1, 0, 0},
    {0x427930, model_bb_faction_group_restore_entry, 1, 0, 0},
    {0x4279c0, model_bb_faction_group_remove, 1, 0, 0},
    {0x427bf0, model_bb_faction_ai, 0, 0, 0},
    {0x427cd0, model_bb_faction_attack_entry, 1, 8, 0},
    {0x427f40, model_bb_faction_group_refresh, 1, 0, 0},
    {0x428000, model_bb_faction_idle_entry, 0, 0, 0},
    {0x428010, model_bb_faction_plan, 0, 0, 0},
    {0x4281b0, model_bb_faction_stage, 1, 0, 0},
    {0x4281c0, model_bb_faction_group_command_entry, 3, 8, 0},
    {0x428250, model_bb_faction_group_ready, 1, 0, 0},
    {0x4282b0, model_bb_faction_plan_seed, 0, 0, 0},
    {0x428400, model_bb_faction_workforce, 0, 0, 0},
    {0x4286f0, model_bb_faction_recruit_entry, 1, 0, 0},
    {0x428960, model_bb_faction_buildings_entry, 0, 0, 0},
    {0x428b40, model_bb_faction_military, 0, 0, 0},
    {0x428c60, model_bb_faction_strength_entry, 0, 0, 0},
    {0x428dd0, model_bb_faction_construction, 3, 0, 0},
    {0x429120, model_bb_faction_recount_entry, 0, 0, 0},
    {0x429230, model_bb_faction_flag, 2, 0, 0},
    {0x42e4e0, model_bb_entity_build, 5, 0, 0},
    {0x434620, model_bb_entity_remove_kind_entry, 1, 0, 0},
    {0x4347b0, model_bb_entity_pool_tick, 0, 0, 0},
    {0x434af0, model_bb_entity_select_rect_entry, 4, 0, 0},
    {0x434e60, bb_entity_area_damage_c, 6, 0, 0},
    {0x435070, model_bb_entity_transfer_entry, 2, 0, 0},
    {0x435180, model_bb_entity_depth_check_entry, 0, 0, 0},
    {0x435270, model_bb_entity_blockers_add_entry, 0, 0, 0},
    {0x4352d0, model_bb_entity_blockers_remove_entry, 0, 0, 0},
    {0x435340, model_bb_entity_initialize, 4, 0, 0},
    {0x437f30, model_bb_entity_profession, 1, 0, 0},
    {0x4382e0, model_bb_entity_upgrade_parts, 0, 0, 0},
    {0x438420, model_bb_entity_speed_entry, 0, 0, 0},
    {0x43f440, model_bb_entity_command_entry, 3, 0, 0},
    {0x43f710, model_bb_entity_attack_tile_entry, 2, 0, 0},
    {0x43f740, model_bb_entity_activity_entry, 2, 0, 0},
    {0x43f8c0, model_bb_entity_resume, 0, 0, 0},
    {0x43f970, model_bb_entity_place_tile, 2, 0, 0},
    {0x43fa30, model_bb_entity_place, 2, 0, 0},
    {0x43fae0, model_bb_entity_walk_entry, 3, 0, 0},
    {0x43fb50, model_bb_entity_move_escape, 2, 0, 0},
    {0x43fd10, model_bb_entity_approach_entry, 2, 0, 0},
    {0x43fe30, model_bb_entity_arrive, 0, 0, 0},
    {0x4414e0, model_bb_entity_check_arrival_entry, 0, 0, 0},
    {0x441560, model_bb_entity_action_begin, 0, 0, 0},
    {0x4415d0, model_bb_entity_action_entry, 2, 0, 0},
    {0x441600, model_bb_entity_cargo, 1, 0, 0},
    {0x4416d0, model_bb_entity_secondary_entry, 1, 0, 0},
    {0x441710, model_bb_entity_reset_move, 0, 0, 0},
    {0x441740, model_bb_entity_move_goal, 2, 0, 0},
    {0x4418a0, model_bb_entity_move, 2, 0, 0},
    {0x441ad0, model_bb_entity_move_near, 2, 0, 0},
    {0x441ee0, model_bb_entity_can_consume, 2, 0, 0},
    {0x441f60, model_bb_entity_can_steal, 2, 0, 0},
    {0x441fc0, model_bb_entity_consume, 2, 0, 0},
    {0x442320, model_bb_entity_steal, 2, 0, 0},
    {0x442490, model_bb_entity_work_wood, 0, 0, 0},
    {0x4425b0, model_bb_entity_work_stone, 0, 0, 0},
    {0x4426d0, model_bb_entity_work_hunt, 0, 0, 0},
    {0x442800, model_bb_entity_work_auto, 0, 0, 0},
    {0x4429e0, model_bb_entity_human_ai_entry, 0, 0, 0},
    {0x442f30, model_bb_entity_work_retry, 0, 0, 0},
    {0x442f40, model_bb_entity_kind_tick_entry, 0, 4, 0},
    {0x444150, model_bb_entity_decision_tick, 0, 0, 0},
    {0x445ff0, model_bb_entity_recruit_entry, 1, 0, 0},
    {0x446110, model_bb_entity_need_food, 0, 0, 0},
    {0x4461f0, model_bb_entity_need_water, 0, 0, 0},
    {0x446300, model_bb_entity_pair_allowed, 2, 0, 0},
    {0x446400, model_bb_entity_partner_begin, 1, 0, 0},
    {0x4464b0, model_bb_entity_partner_find, 0, 0, 0},
    {0x446580, model_bb_entity_home_nearest, 0, 0, 0},
    {0x446600, model_bb_entity_child_site, 0, 0, 0},
    {0x446880, model_bb_entity_order_site, 0, 0, 0},
    {0x446960, model_bb_entity_work_repair, 0, 0, 0},
    {0x446b80, model_bb_entity_work_clear, 0, 0, 0},
    {0x446cb0, model_bb_entity_work_field, 0, 0, 0},
    {0x446dc0, model_bb_entity_target_building, 0, 0, 0},
    {0x446ee0, model_bb_entity_target_last, 0, 0, 0},
    {0x446ff0, model_bb_entity_target_weak, 0, 0, 0},
    {0x447120, model_bb_entity_target_strong, 0, 0, 0},
    {0x447260, model_bb_entity_target_strong_alt, 0, 0, 0},
    {0x4473a0, model_bb_entity_enemy_command, 0, 0, 0},
    {0x4474b0, model_bb_entity_enemy_evade, 0, 0, 0},
    {0x4475d0, model_bb_entity_doctor_find, 0, 0, 0},
    {0x4476b0, model_bb_entity_clown_action, 0, 0, 0},
    {0x447750, model_bb_entity_morale_shock, 0, 0, 0},
    {0x447810, model_bb_entity_home_idle, 0, 0, 0},
    {0x4479a0, model_bb_entity_partner_link, 1, 0, 0},
    {0x4479c0, model_bb_entity_partner_name, 1, 0, 0},
    {0x447a20, model_bb_entity_partner_clear, 0, 0, 0},
    {0x447f20, model_bb_entity_refresh_tiles, 0, 0, 0},
    {0x448490, model_bb_entity_register_tiles, 0, 0, 0},
    {0x448890, model_bb_entity_release_tiles, 0, 0, 0},
    {0x448d70, model_bb_entity_contact, 1, 0, 0},
    {0x449a70, model_bb_entity_update_tile, 0, 0, 0},
    {0x449c10, model_bb_entity_city_name, 0, 0, 0},
    {0x449c50, bb_entity_main_c, 0, 0, 0},
    {0x44f880, bb_entity_attack_begin_c, 0, 0, 0},
    {0x44f900, bb_entity_attack_tick_c, 0, 0, 0},
    {0x4503c0, model_bb_entity_path_step, 0, 0, 0},
    {0x4505a0, model_bb_entity_move_start_entry, 0, 0, 0},
    {0x4505e0, model_bb_entity_move_stop_entry, 0, 0, 0},
    {0x450660, model_bb_entity_destination_step, 0, 0, 0},
    {0x450820, model_bb_entity_heal, 1, 0, 0},
    {0x4508a0, bb_entity_damage_c, 2, 0, 0},
    {0x451b90, model_bb_entity_idle_sound, 0, 0, 0},
    {0x451c00, model_bb_entity_route, 2, 0, 0},
    {0x4529b0, model_bb_building_road_connect_entry, 0, 44, 0},
    {0x452c40, model_bb_entity_first_name_entry, 0, 0, 0},
    {0x452c50, model_bb_entity_last_name_entry, 0, 0, 0},
    {0x452c60, model_bb_entity_name_title, 1, 0, 0},
    {0x452ea0, model_bb_entity_name, 1, 0, 0},
    {0x452f80, model_bb_entity_birth, 2, 0, 0},
    {0x453140, model_bb_entity_cast, 3, 0, 0},
    {0x453430, model_bb_entity_adjust_resources_entry, 1, 0, 0},
    {0x453450, model_bb_entity_treatment_fee, 1, 0, 0},
    {0x453580, model_bb_entity_event_feedback, 2, 0, 0},
    {0x453a00, model_bb_entity_feedback_entry, 3, 0, 0},
    {0x453a80, model_bb_entity_command_sound, 1, 0, 0},
    {0x453ab0, model_bb_entity_sound, 1, 0, 0},
    {0x453ae0, model_bb_entity_feedback_delay, 2, 0, 0},
    {0x453b00, model_bb_entity_feedback_queue, 2, 0, 0},
    {0x453b20, model_bb_entity_target_entry, 1, 0, 0},
    {0x453ba0, model_bb_entity_stat, 2, 0, 0},
    {0x453ee0, model_bb_entity_adjust_health_entry, 2, 0, 0},
    {0x453f80, model_bb_entity_adjust_109_entry, 2, 0, 0},
    {0x454050, model_bb_entity_adjust_113_entry, 1, 0, 0},
    {0x4540a0, model_bb_entity_adjust_111_entry, 2, 0, 0},
    {0x454140, model_bb_entity_adjust_10d_entry, 2, 0, 0},
    {0x4541e0, model_bb_entity_adjust_10f_entry, 2, 0, 0},
    {0x454280, model_bb_entity_adjust_100_entry, 1, 0, 0},
    {0x4542d0, model_bb_entity_adjust_101_entry, 1, 0, 0},
    {0x454320, model_bb_entity_adjust_11b_entry, 1, 0, 0},
    {0x454de0, model_bb_entity_clear_subtype, 0, 0, 0},
    {0x454ec0, bb_entity_subtype_clear_c, 0, 0, 0},
    {0x454ed0, model_bb_entity_subtype, 1, 0, 0},
    {0x454fc0, model_bb_entity_trade_entry, 4, 0, 0},
    {0x455240, model_bb_entity_evade_entry, 1, 0, 0},
    {0x455340, model_bb_entity_mage_offense, 0, 0, 0},
    {0x4554f0, model_bb_entity_mage_support, 0, 0, 0},
    {0x4556e0, model_bb_entity_pair_entry, 1, 0, 0},
    {0x4559c0, model_bb_entity_command_position, 0, 0, 0},
    {0x455ae0, model_bb_entity_idle_action, 0, 0, 0},
    {0x455c10, bb_entity_attack_range_c, 0, 0, 0},
    {0x455d50, bb_entity_target_moved_c, 0, 0, 0},
};
const struct bb_model_target *bb_entity_model_find(void *unused, uint32_t id) {
  (void)unused;
  for (unsigned n = 0; n < sizeof targets / sizeof *targets; n++)
    if (targets[n].identity == id)
      return &targets[n];
  return NULL;
}
