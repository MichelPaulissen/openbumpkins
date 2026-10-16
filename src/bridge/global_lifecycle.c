/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "global_lifecycle.h"
#include "bootstrap_resources.h"
#include "controls.h"
#include "dialog.h"
#include "interface_controls.h"
#include "interface_platform.h"
#include "interop.h"
#include "letters.h"
#include "script.h"
#include "selection_panels.h"
#include "worker_runtime.h"
#ifdef BB_MODEL_RUNTIME
#include "game_calls.h"
#include "lifecycle_models.h"
#endif
#define BYTE(a) BB_U8(a)
#define PTR(a) ((void *)(uintptr_t)(a))
#ifndef BB_MODEL_RUNTIME
typedef void(BB_THISCALL * method0)(void *);
typedef void(BB_THISCALL * method1)(void *, const char *);
/* Static callbacks have no return-value contract. Class entry shims with
 * parameterless declarations still consume ECX; isolate that temporary i386
 * ABI adaptation from ordinary typed C calls. */
static __attribute__((noinline, noipa)) void
abi_object_callback(void (*entry)(void), void *object) {
  ((method0)BB_CODE((uintptr_t)entry))(object);
}
static __attribute__((noinline, noipa)) void
abi_archive_callback(void (*entry)(void), void *object, const char *filename) {
  ((method1)BB_CODE((uintptr_t)entry))(object, filename);
}
#define OBJECT_CALLBACK(name, child, object)                                   \
  void name(void) { child(PTR(object)); }
#define ABI_OBJECT_CALLBACK(name, child, object)                               \
  void name(void) { abi_object_callback(child, PTR(object)); }
#define ARCHIVE_CALLBACK(name, object, filename)                               \
  void name(void) {                                                            \
    abi_archive_callback(bb_archive_construct_entry, PTR(object),              \
                         PTR(filename));                                       \
  }
#else
/* These callbacks have no explicit method arguments. Supply the object both
 * in the method receiver and the first packed word for legacy cdecl entries. */
#define OBJECT_CALLBACK(name, child, object)                                   \
  void name(void) { BB_GAME_CALL(BB_GLOBAL_ID_##child, object, object); }
#define ABI_OBJECT_CALLBACK OBJECT_CALLBACK
#define ARCHIVE_CALLBACK(name, object, filename)                               \
  void name(void) { BB_GAME_CALL(0x465fb0, object, filename); }
#endif

OBJECT_CALLBACK(bb_global_4028a0_entry, bb_letters_destroy_entry, 0x5db050)
OBJECT_CALLBACK(bb_global_4028d0_entry, bb_message_destructor_entry, 0x5ecf38)
/* Four reverse-order destruction callbacks for packed numeric records. */
void bb_global_402920_entry(void) {
  uintptr_t object = 0x640ee4;
  for (unsigned i = 0; i < 4; ++i) {
    object -= 0x7e;
#ifdef BB_MODEL_RUNTIME
    BB_GAME_CALL(BB_GLOBAL_ID_bb_number_draw_entry, object, object);
#else
    abi_object_callback(bb_number_draw_entry, PTR(object));
#endif
  }
}

OBJECT_CALLBACK(bb_global_4029c0_entry, bb_audio_manager_walk_entry, 0x5590e8)
ABI_OBJECT_CALLBACK(bb_global_4029f0_entry, bb_display_destroy_entry, 0x4eebc8)
ABI_OBJECT_CALLBACK(bb_global_402a20_entry, bb_shared_destroy_entry, 0x513458)
ABI_OBJECT_CALLBACK(bb_global_402a50_entry, bb_entity_manager_destroy_entry,
                    0x553f90)
ABI_OBJECT_CALLBACK(bb_global_402ae0_entry, bb_font_destroy_entry, 0x641028)
ABI_OBJECT_CALLBACK(bb_global_402b10_entry, bb_font_destroy_entry, 0x5cd7d0)
ABI_OBJECT_CALLBACK(bb_global_402b40_entry, bb_font_destroy_entry, 0x6ab590)
ABI_OBJECT_CALLBACK(bb_global_402b70_entry, bb_font_destroy_entry, 0x6a9968)
ABI_OBJECT_CALLBACK(bb_global_402ba0_entry, bb_font_destroy_entry, 0x63f0d0)
ABI_OBJECT_CALLBACK(bb_global_402bd0_entry, bb_font_destroy_entry, 0x5117e0)
ABI_OBJECT_CALLBACK(bb_global_402c00_entry, bb_font_destroy_entry, 0x5c78f0)
ABI_OBJECT_CALLBACK(bb_global_402c30_entry, bb_font_destroy_entry, 0x69c758)
ABI_OBJECT_CALLBACK(bb_global_402c60_entry, bb_font_destroy_entry, 0x5fe420)
ABI_OBJECT_CALLBACK(bb_global_402c90_entry, bb_font_destroy_entry, 0x50faa8)
ABI_OBJECT_CALLBACK(bb_global_402cc0_entry, bb_font_destroy_entry, 0x5c5b70)
ABI_OBJECT_CALLBACK(bb_global_402cf0_entry, bb_font_destroy_entry, 0x5f51f0)
ABI_OBJECT_CALLBACK(bb_global_402d20_entry, bb_font_destroy_entry, 0x5f74b8)
ABI_OBJECT_CALLBACK(bb_global_402d50_entry, bb_font_destroy_entry, 0x5cf3f0)
ABI_OBJECT_CALLBACK(bb_global_402d80_entry, bb_archive_destroy_entry, 0x5d1028)
ABI_OBJECT_CALLBACK(bb_global_402db0_entry, bb_archive_destroy_entry, 0x5c77c8)
ABI_OBJECT_CALLBACK(bb_global_402de0_entry, bb_archive_destroy_entry, 0x5e4198)
ABI_OBJECT_CALLBACK(bb_global_402e10_entry, bb_archive_destroy_entry, 0x5f7360)
ABI_OBJECT_CALLBACK(bb_global_402e40_entry, bb_archive_destroy_entry, 0x5f6e20)
ARCHIVE_CALLBACK(bb_global_402e50_entry, 0x5f6e20, 0x4847d8)
ARCHIVE_CALLBACK(bb_global_402e60_entry, 0x5f7360, 0x4847e4)
ARCHIVE_CALLBACK(bb_global_402e70_entry, 0x5e4198, 0x4847f0)
ARCHIVE_CALLBACK(bb_global_402e80_entry, 0x5c77c8, 0x4847fc)
ARCHIVE_CALLBACK(bb_global_402e90_entry, 0x5d1028, 0x48480c)
ABI_OBJECT_CALLBACK(bb_global_402ea0_entry, bb_font_construct_entry, 0x5cf3f0)
ABI_OBJECT_CALLBACK(bb_global_402eb0_entry, bb_font_construct_entry, 0x5f74b8)
ABI_OBJECT_CALLBACK(bb_global_402ec0_entry, bb_font_construct_entry, 0x5f51f0)
ABI_OBJECT_CALLBACK(bb_global_402ed0_entry, bb_font_construct_entry, 0x5c5b70)
ABI_OBJECT_CALLBACK(bb_global_402ee0_entry, bb_font_construct_entry, 0x50faa8)
ABI_OBJECT_CALLBACK(bb_global_402ef0_entry, bb_font_construct_entry, 0x5fe420)
ABI_OBJECT_CALLBACK(bb_global_402f00_entry, bb_font_construct_entry, 0x69c758)
ABI_OBJECT_CALLBACK(bb_global_402f10_entry, bb_font_construct_entry, 0x5c78f0)
ABI_OBJECT_CALLBACK(bb_global_402f20_entry, bb_font_construct_entry, 0x5117e0)
ABI_OBJECT_CALLBACK(bb_global_402f30_entry, bb_font_construct_entry, 0x63f0d0)
ABI_OBJECT_CALLBACK(bb_global_402f40_entry, bb_font_construct_entry, 0x6a9968)
ABI_OBJECT_CALLBACK(bb_global_402f50_entry, bb_font_construct_entry, 0x6ab590)
ABI_OBJECT_CALLBACK(bb_global_402f60_entry, bb_font_construct_entry, 0x5cd7d0)
ABI_OBJECT_CALLBACK(bb_global_402f70_entry, bb_font_construct_entry, 0x641028)
ABI_OBJECT_CALLBACK(bb_global_402f80_entry, bb_render_queue_construct_entry,
                    0x5d19e0)
OBJECT_CALLBACK(bb_global_402f90_entry, bb_cursor_construct_entry, 0x6b0830)
void bb_global_402fa0_entry(void) {
  BB_U32(0x604638) = BB_U32(0x484444);
  BB_U32(0x513440) = BB_U32(0x484448) - 41u;
}

void bb_global_402fc0_entry(void) {
  BB_I32(0x5db014) = BB_I32(0x604638) / 2;
  BB_I32(0x6a9078) = BB_I32(0x513440) / 2;
}

OBJECT_CALLBACK(bb_global_402fe0_entry, bb_demo_construct_entry, 0x642c48)
OBJECT_CALLBACK(bb_global_402ff0_entry, bb_script_construct_entry, 0x5e47b0)
ABI_OBJECT_CALLBACK(bb_global_403000_entry, bb_entity_manager_construct_entry,
                    0x553f90)
OBJECT_CALLBACK(bb_global_403010_entry, bb_shared_construct_entry, 0x513458)
ABI_OBJECT_CALLBACK(bb_global_403020_entry, bb_display_construct_entry,
                    0x4eebc8)
OBJECT_CALLBACK(bb_global_403030_entry, bb_audio_manager_construct_entry,
                0x5590e8)
ABI_OBJECT_CALLBACK(bb_global_403040_entry, bb_faction_manager_construct_entry,
                    0x554fb8)
OBJECT_CALLBACK(bb_global_403050_entry, bb_cd_bank_construct_entry, 0x69e370)
OBJECT_CALLBACK(bb_global_403060_entry, bb_short_object_reset_entry, 0x5e470c)
OBJECT_CALLBACK(bb_global_403070_entry, bb_toolbar_construct_entry, 0x5d19a0)
OBJECT_CALLBACK(bb_global_403080_entry, bb_build_menu_construct_entry, 0x5f90d8)
OBJECT_CALLBACK(bb_global_403090_entry, bb_stock_panel_construct_entry,
                0x6a98e0)
OBJECT_CALLBACK(bb_global_4030a0_entry, bb_numeric_panel_construct_entry,
                0x640ce8)
OBJECT_CALLBACK(bb_global_4030b0_entry, bb_ambience_identity_entry, 0x6b0880)
OBJECT_CALLBACK(bb_global_4030c0_entry, bb_action_panel_identity_entry,
                0x6b0858)
OBJECT_CALLBACK(bb_global_4030d0_entry, bb_message_construct_entry, 0x5ecf38)
OBJECT_CALLBACK(bb_global_4030e0_entry, bb_letters_construct_entry, 0x5db050)
OBJECT_CALLBACK(bb_global_4030f0_entry, bb_message_self_entry, 0x5ca308)
OBJECT_CALLBACK(bb_global_403100_entry, bb_highlight_construct_entry, 0x6af100)
OBJECT_CALLBACK(bb_global_403110_entry, bb_building_panel_initialize_entry,
                0x6b08c0)
OBJECT_CALLBACK(bb_global_403120_entry, bb_building_stock_construct_entry,
                0x6a8ef8)
OBJECT_CALLBACK(bb_global_403130_entry, bb_minimap_panel_initialize_entry,
                0x5eac80)
OBJECT_CALLBACK(bb_global_403140_entry, bb_tooltip_construct_entry, 0x559128)
OBJECT_CALLBACK(bb_global_403150_entry, bb_mission_description_construct_entry,
                0x6a9950)
OBJECT_CALLBACK(bb_global_403160_entry, bb_panel_construct_entry, 0x6025d0)
OBJECT_CALLBACK(bb_global_403170_entry, bb_feedback_construct_entry, 0x662c98)
OBJECT_CALLBACK(bb_global_403180_entry, bb_notification_clear_entry, 0x5eaca8)

/* The shared-session and entity-manager reference destructors are bare RET. */
void bb_shared_destroy_entry(void) {}
void bb_entity_manager_destroy_entry(void) {}

/* 0x466fe0: the nested CD handle constructor clears only its first word. */
void *BB_THISCALL bb_cd_handle_construct_entry(void *object) {
  BB_U32((uintptr_t)object) = 0;
  return object;
}
/* 0x469d79: preserve other fields; the CRT copy is a retained boundary. */
void *BB_THISCALL bb_cd_track_construct_entry(void *object) {
  uintptr_t p = (uintptr_t)object;
  BB_U32(p + 0x40) = 0;
  BB_U32(p + 0x144) = 1;
  BYTE(p + 0x148) = 0;
#ifdef BB_MODEL_RUNTIME
  BB_GAME_CALL(0x46f4f0, 0, bb_address_encode(object), 0x48c68c);
#else
  ((char *(*)(char *, const char *))BB_CODE((uintptr_t)0x46f4f0))(object, PTR(0x48c68c));
#endif
  return object;
}
#ifndef BB_MODEL_RUNTIME
static void BB_THISCALL construct_track(void *object) {
  bb_cd_track_construct_entry(object);
}
#endif
/* 0x469d0a: construct all slots before publishing any of their indices. */
void *BB_THISCALL bb_cd_tracks_construct_entry(void *object) {
#ifdef BB_MODEL_RUNTIME
  for (uint32_t i = 0; i < 128; ++i)
    bb_cd_track_construct_entry(PTR((uintptr_t)object + i * 0x14c));
#else
  bb_object_array_construct_entry(object, 0x14c, 128, construct_track);
#endif
  for (uint32_t i = 0; i < 128; ++i)
    BYTE((uintptr_t)object + i * 0x14c + 0x148) = i;
  return object;
}
/* 0x46924e: nested handle at +4, slots at +0x10, final state at +0xa610. */
void *BB_THISCALL bb_cd_bank_construct_entry(void *object) {
  uintptr_t p = (uintptr_t)object;
  bb_cd_handle_construct_entry(PTR(p + 4));
  bb_cd_tracks_construct_entry(PTR(p + 0x10));
  BB_U32(p + 0xa610) = 0;
  return object;
}
