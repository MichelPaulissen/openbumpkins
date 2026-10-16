/* SPDX-License-Identifier: GPL-3.0-only */
#include "game_calls.h"
#include "interface_platform.h"
void BB_THISCALL bb_interface_audio_start_entry(void *object) {
  uintptr_t p = bb_address_encode(object);
  if (!BB_U32(p + 16)) {
    BB_GAME_CALL(0x46a190u, 0, BB_U32(0x48bc70));
    BB_GAME_CALL(0x46a170u, 0, 0xfffffd44);
    BB_U32(p + 16) = 1;
  }
}
void BB_THISCALL bb_interface_audio_stop_entry(void *object) {
  uintptr_t p = bb_address_encode(object);
  if (BB_U32(p + 16)) {
    BB_GAME_CALL(0x46a950u, 0);
    BB_GAME_CALL(0x46a970u, 0);
    BB_U32(p + 16) = 0;
  }
}
void BB_THISCALL bb_interface_sound_disable_entry(void *object) {
  uintptr_t p = bb_address_encode(object);
  if (BB_U32(p + 20)) {
    BB_GAME_CALL(0x459b30u, 0, 0);
    BB_U32(p + 20) = 0;
  }
}
void BB_THISCALL bb_interface_sound_enable_entry(void *object) {
  uintptr_t p = bb_address_encode(object);
  if (!BB_U32(p + 20)) {
    BB_GAME_CALL(0x459b30u, 0, 1);
    BB_U32(p + 20) = 1;
  }
}
