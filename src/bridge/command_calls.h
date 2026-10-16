/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_COMMAND_CALLS_H
#define BB_COMMAND_CALLS_H
#include "ui_calls.h"
#define CMD_plain_fn(id) C0(id)
#define CMD_one_fn(id, ...) C1(id, __VA_ARGS__)
#define CMD_three_fn(id, ...) C3(id, __VA_ARGS__)
#define CMD_method_fn(id, ...) M1(id, __VA_ARGS__)
#define CMD_two_fn(id, ...) M2(id, __VA_ARGS__)
#define CMD_four_fn(id, ...) M4(id, __VA_ARGS__)
#define CMD_open_fn(id, ...) C2(id, __VA_ARGS__)
#define CMD_registry_fn(id, ...) C2(id, __VA_ARGS__)
#define CMD_format_fn(id, out, fmt, ...) C_FORMAT(out, fmt, __VA_ARGS__)
#define C_FORMAT(out, fmt, ...)                                                \
  bb_crt_output_words(out, 256, fmt, (uint32_t[]){__VA_ARGS__},                \
                      sizeof((uint32_t[]){__VA_ARGS__}) / 4)
static inline uint64_t bb_command_register_call(uint32_t id, void *object,
                                                uint32_t eax, uint32_t edx,
                                                const uint32_t *args,
                                                size_t words) {
  struct bb_call_state state = {eax, bb_address_encode(object), edx, 0x202};
  bb_model_invoke(&state, id, args, words);
  return ((uint64_t)state.edx << 32) | state.eax;
}
#define CMD_REG(id, object, eax, edx, ...)                                     \
  bb_command_register_call(id, object, eax, edx, (uint32_t[]){__VA_ARGS__},    \
                           sizeof((uint32_t[]){__VA_ARGS__}) / 4)
#define bb_entity_walk_c(p, x, y, ability, eax)                                \
  CMD_REG(0x43fae0, p, eax, 0, UI_WORD(x), UI_WORD(y), UI_WORD(ability))
#define bb_entity_attack_tile_c(p, x, y, eax)                                  \
  CMD_REG(0x43f710, p, eax, 0, UI_WORD(x), UI_WORD(y))
#define bb_entity_command_c(p, target, a, b, eax)                              \
  CMD_REG(0x43f440, p, eax, 0, UI_WORD(target), UI_WORD(a), UI_WORD(b))
#define bb_entity_activity_c(p, state, target, eax)                            \
  CMD_REG(0x43f740, p, eax, 0, UI_WORD(state), UI_WORD(target))
#define bb_entity_dirty_c(p, eax, edx) CMD_REG(0x437ee0, p, eax, edx)
#define bb_entity_build(owner, x, y, kind, dir)                                \
  ((void *)(uintptr_t)C5(0x42e4e0, owner, x, y, kind, dir))
#define bb_sound_resource_play_entry(p, a) M1(0x402210, p, a)
#define bb_entity_cast(p, a, b, c) M3(0x453140, p, a, b, c)
#define bb_entity_stat(p, a, b) M2(0x453ba0, p, a, b)
#endif
