/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_MESSAGE_CALLS_H
#define BB_MESSAGE_CALLS_H
#ifdef BB_MODEL_RUNTIME
#include "game_calls.h"
#define BB_MESSAGE_CALL(state, target, args, esp, edi, esi, ebp, ebx)          \
  bb_model_call_at(state, target, args, 10, esp, edi, esi, ebp, ebx)
#define BB_MESSAGE_TARGET(name) BB_MESSAGE_##name
#define BB_MESSAGE_bb_command_dispatch 0x4013d0u
#define BB_MESSAGE_bb_display_shade 0x432500u
#define BB_MESSAGE_bb_entity_sound 0x453ab0u
#define BB_MESSAGE_bb_font_bounds 0x4309b0u
#define BB_MESSAGE_bb_font_bounds_wrapped 0x430bb0u
#define BB_MESSAGE_bb_font_draw 0x430560u
#define BB_MESSAGE_bb_font_draw_wrapped 0x4306e0u
#define BB_MESSAGE_bb_message_current_entry 0x41e3c0u
#define BB_MESSAGE_bb_message_next_entry 0x41e4f0u
#define BB_MESSAGE_bb_notification_enqueue_entry 0x41e220u
#define BB_MESSAGE_bb_render_queue_both_entry 0x418a10u
#define BB_MESSAGE_bb_sample_play_entry 0x415860u
#else
#define BB_MESSAGE_CALL(state, target, args, esp, edi, esi, ebp, ebx)          \
  bb_call_words10(state, BB_CODE(target), args)
#define BB_MESSAGE_TARGET(name) ((uintptr_t)(name))
#endif
#endif
