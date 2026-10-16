/* SPDX-License-Identifier: GPL-3.0-only */
#include "legacy_models.h"
#include "instruments.h"
#include "interop.h"
#include "midi.h"
#include "script.h"
#include "wav_sample.h"
#define A(n) BB_U32(r[3] + 8u + (n) * 4u)
static void model_bb_script_time_text_entry(uint32_t *r) {
  bb_script_time_text_entry(A(0), (char *)(uintptr_t)A(1));
}
static void model_bb_script_event_text_entry(uint32_t *r) {
  bb_script_event_text_entry((void *)(uintptr_t)r[6], (char *)(uintptr_t)A(0));
}
static void model_bb_script_condition_text_entry(uint32_t *r) {
  bb_script_condition_text_entry((void *)(uintptr_t)r[6],
                                 (char *)(uintptr_t)A(0));
}
static void model_bb_script_block_draw_entry(uint32_t *r) {
  bb_script_block_draw_entry((void *)(uintptr_t)r[6], A(0), A(1));
}
static void model_bb_script_draw_entry(uint32_t *r) {
  bb_script_draw_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_script_input_entry(uint32_t *r) {
  bb_script_input_entry((void *)(uintptr_t)r[6], (const char *)(uintptr_t)A(0));
}
static void model_bb_midi_read_byte_entry(uint32_t *r) {
  r[7] = bb_midi_read_byte_entry((void *)(uintptr_t)A(0));
}
static void model_bb_midi_read_word_entry(uint32_t *r) {
  r[7] = bb_midi_read_word_entry((void *)(uintptr_t)A(0));
}
static void model_bb_midi_read_three_entry(uint32_t *r) {
  r[7] = bb_midi_read_three_entry((void *)(uintptr_t)A(0));
}
static void model_bb_midi_read_dword_entry(uint32_t *r) {
  r[7] = bb_midi_read_dword_entry((void *)(uintptr_t)A(0));
}
static void model_bb_midi_read_variable_entry(uint32_t *r) {
  r[7] = bb_midi_read_variable_entry((void *)(uintptr_t)A(0));
}
static void model_bb_midi_track_construct_entry(uint32_t *r) {
  r[7] =
      bb_address_encode(bb_midi_track_construct_entry((void *)(uintptr_t)r[6]));
}
static void model_bb_midi_track_append_entry(uint32_t *r) {
  bb_midi_track_append_entry((void *)(uintptr_t)r[6], A(0), A(1), A(2), A(3),
                             A(4));
}
static void model_bb_midi_track_program_entry(uint32_t *r) {
  bb_midi_track_program_entry((void *)(uintptr_t)r[6], A(0));
}
static void model_bb_midi_track_release_entry(uint32_t *r) {
  bb_midi_track_release_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_midi_track_begin_entry(uint32_t *r) {
  bb_midi_track_begin_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_midi_track_rewind_entry(uint32_t *r) {
  bb_midi_track_rewind_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_midi_track_event_entry(uint32_t *r) {
  bb_midi_track_event_entry((void *)(uintptr_t)r[6],
                            (const void *)(uintptr_t)A(0));
}
static void model_bb_midi_track_tick_entry(uint32_t *r) {
  bb_midi_track_tick_entry((void *)(uintptr_t)r[6], A(0));
}
static void model_bb_midi_construct_entry(uint32_t *r) {
  r[7] = bb_address_encode(bb_midi_construct_entry((void *)(uintptr_t)r[6]));
}
static void model_bb_midi_load_entry(uint32_t *r) {
  r[7] = bb_midi_load_entry((void *)(uintptr_t)r[6],
                            (const char *)(uintptr_t)A(0));
}
static void model_bb_midi_read_entry(uint32_t *r) {
  r[7] = bb_midi_read_entry((void *)(uintptr_t)r[6], (void *)(uintptr_t)A(0));
}
static void model_bb_midi_release_entry(uint32_t *r) {
  bb_midi_release_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_wav_device_initialize_entry(uint32_t *r) {
  bb_wav_device_initialize_entry((void *)(uintptr_t)r[6],
                                 (void *)(uintptr_t)A(0));
}
static void model_bb_wav_device_attach_entry(uint32_t *r) {
  bb_wav_device_attach_entry((void *)(uintptr_t)r[6], (void *)(uintptr_t)A(0),
                             (void *)(uintptr_t)A(1));
}
static void model_bb_wav_acquire_entry(uint32_t *r) {
  r[7] = bb_address_encode(bb_wav_acquire_entry((void *)(uintptr_t)r[6],
                                                (const char *)(uintptr_t)A(0)));
}
static void model_bb_wav_remove_entry(uint32_t *r) {
  bb_wav_remove_entry((void *)(uintptr_t)r[6], (void *)(uintptr_t)A(0));
}
static void model_bb_wav_play_entry(uint32_t *r) {
  bb_wav_play_entry((void *)(uintptr_t)r[6], (void *)(uintptr_t)A(0));
}
static void model_bb_wav_lookup_entry(uint32_t *r) {
  r[7] = bb_address_encode(bb_wav_lookup_entry((void *)(uintptr_t)r[6],
                                               (const char *)(uintptr_t)A(0)));
}
static void model_bb_wav_construct_entry(uint32_t *r) {
  r[7] = bb_address_encode(bb_wav_construct_entry((void *)(uintptr_t)r[6]));
}
static void model_bb_wav_buffers_entry(uint32_t *r) {
  bb_wav_buffers_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_wav_fill_entry(uint32_t *r) {
  bb_wav_fill_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_wav_load_entry(uint32_t *r) {
  r[7] =
      bb_wav_load_entry((void *)(uintptr_t)r[6], (const char *)(uintptr_t)A(0));
}
static void model_bb_wav_trigger_entry(uint32_t *r) {
  bb_wav_trigger_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_wav_root_note_entry(uint32_t *r) {
  bb_wav_root_note_entry((void *)(uintptr_t)r[6], A(0));
}
static void model_bb_wav_note_range_entry(uint32_t *r) {
  bb_wav_note_range_entry((void *)(uintptr_t)r[6], A(0), A(1));
}
static void model_bb_wav_voice_entry(uint32_t *r) {
  r[7] = bb_wav_voice_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_wav_note_entry(uint32_t *r) {
  bb_wav_note_entry((void *)(uintptr_t)r[6], A(0), A(1));
}
static void model_bb_instrument_song_start_entry(uint32_t *r) {
  bb_instrument_song_start_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_instrument_song_rewind_entry(uint32_t *r) {
  bb_instrument_song_rewind_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_instrument_song_tick_entry(uint32_t *r) {
  bb_instrument_song_tick_entry((void *)(uintptr_t)r[6], A(0));
}
static void model_bb_instrument_device_initialize_entry(uint32_t *r) {
  bb_instrument_device_initialize_entry((void *)(uintptr_t)r[6],
                                        (void *)(uintptr_t)A(0));
}
static void model_bb_instrument_device_attach_entry(uint32_t *r) {
  bb_instrument_device_attach_entry(
      (void *)(uintptr_t)r[6], (void *)(uintptr_t)A(0), (void *)(uintptr_t)A(1),
      (void *)(uintptr_t)A(2));
}
static void model_bb_instrument_runtime_initialize_entry(uint32_t *r) {
  r[7] = bb_instrument_runtime_initialize_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_instrument_runtime_release_entry(uint32_t *r) {
  bb_instrument_runtime_release_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_instrument_gain_set_entry(uint32_t *r) {
  bb_instrument_gain_set_entry((void *)(uintptr_t)r[6],
                               BB_DOUBLE(r[3] + 8u + 0 * 4u));
}
static void model_bb_instrument_gain_get_entry(uint32_t *r) {
  bb_model_return_float(bb_instrument_gain_get_entry((void *)(uintptr_t)r[6]));
}
static void model_bb_instrument_song_load_entry(uint32_t *r) {
  bb_instrument_song_load_entry((void *)(uintptr_t)r[6],
                                (const char *)(uintptr_t)A(0));
}
static void model_bb_instrument_bank_read_entry(uint32_t *r) {
  r[7] = bb_instrument_bank_read_entry((void *)(uintptr_t)r[6],
                                       (void *)(uintptr_t)A(0));
}
static void model_bb_instrument_bank_write_entry(uint32_t *r) {
  r[7] = bb_instrument_bank_write_entry((void *)(uintptr_t)r[6],
                                        (void *)(uintptr_t)A(0));
}
static void model_bb_instrument_bank_load_entry(uint32_t *r) {
  r[7] = bb_instrument_bank_load_entry((void *)(uintptr_t)r[6],
                                       (const char *)(uintptr_t)A(0));
}
static void model_bb_instrument_bank_save_entry(uint32_t *r) {
  r[7] = bb_instrument_bank_save_entry((void *)(uintptr_t)r[6],
                                       (const char *)(uintptr_t)A(0));
}
static void model_bb_instrument_bank_import_entry(uint32_t *r) {
  r[7] = bb_instrument_bank_import_entry((void *)(uintptr_t)r[6],
                                         (const char *)(uintptr_t)A(0));
}
static void model_bb_instrument_bank_export_entry(uint32_t *r) {
  r[7] = bb_instrument_bank_export_entry((void *)(uintptr_t)r[6],
                                         (const char *)(uintptr_t)A(0));
}
static void model_bb_instrument_worker_entry(uint32_t *r) {
  r[7] = bb_instrument_worker_entry((void *)(uintptr_t)A(0));
}
static void model_bb_instrument_program_append_entry(uint32_t *r) {
  bb_instrument_program_append_entry((void *)(uintptr_t)r[6],
                                     (void *)(uintptr_t)A(0));
}
static void model_bb_instrument_note_on_entry(uint32_t *r) {
  bb_instrument_note_on_entry((void *)(uintptr_t)r[6], A(0), A(1), A(2));
}
static void model_bb_instrument_note_off_entry(uint32_t *r) {
  bb_instrument_note_off_entry((void *)(uintptr_t)r[6], A(0), A(1), A(2));
}
static void model_bb_instrument_midi_open_entry(uint32_t *r) {
  bb_instrument_midi_open_entry((void *)(uintptr_t)r[6]);
}
static void model_bb_instrument_midi_close_entry(uint32_t *r) {
  bb_instrument_midi_close_entry((void *)(uintptr_t)r[6]);
}
static const struct bb_model_target targets[] = {
    {0x422600, model_bb_script_time_text_entry, 2, 0, 0},
    {0x422680, model_bb_script_event_text_entry, 1, 0, 0},
    {0x422950, model_bb_script_block_draw_entry, 2, 0, 0},
    {0x422d80, model_bb_script_condition_text_entry, 1, 0, 0},
    {0x423600, model_bb_script_draw_entry, 0, 0, 0},
    {0x424c80, model_bb_script_input_entry, 1, 0, 0},
    {0x467002, model_bb_wav_device_initialize_entry, 1, 0, 0},
    {0x4670e2, model_bb_wav_device_attach_entry, 2, 0, 0},
    {0x46710c, model_bb_wav_acquire_entry, 1, 0, 0},
    {0x46730a, model_bb_wav_remove_entry, 1, 0, 0},
    {0x4673e9, model_bb_wav_play_entry, 1, 0, 0},
    {0x467413, model_bb_wav_load_entry, 1, 0, 0},
    {0x467834, model_bb_wav_lookup_entry, 1, 0, 0},
    {0x4678a5, model_bb_wav_construct_entry, 0, 0, 0},
    {0x46792f, model_bb_wav_buffers_entry, 0, 0, 0},
    {0x467a51, model_bb_wav_fill_entry, 0, 0, 0},
    {0x467b0b, model_bb_wav_trigger_entry, 0, 0, 0},
    {0x467bb2, model_bb_wav_root_note_entry, 1, 0, 0},
    {0x467be3, model_bb_wav_note_range_entry, 2, 0, 0},
    {0x467c20, model_bb_wav_voice_entry, 0, 0, 0},
    {0x467d04, model_bb_wav_note_entry, 2, 0, 0},
    {0x467ee8, model_bb_midi_read_byte_entry, 1, 0, 0},
    {0x467f13, model_bb_midi_read_word_entry, 1, 0, 0},
    {0x467f5b, model_bb_midi_read_three_entry, 1, 0, 0},
    {0x467fba, model_bb_midi_read_dword_entry, 1, 0, 0},
    {0x468032, model_bb_midi_read_variable_entry, 1, 0, 0},
    {0x468094, model_bb_midi_track_construct_entry, 0, 0, 0},
    {0x4680d2, model_bb_midi_track_append_entry, 5, 0, 0},
    {0x46819d, model_bb_midi_track_program_entry, 1, 0, 0},
    {0x4681be, model_bb_midi_track_release_entry, 0, 0, 0},
    {0x468213, model_bb_midi_track_begin_entry, 0, 0, 0},
    {0x468259, model_bb_midi_track_rewind_entry, 0, 0, 0},
    {0x46829f, model_bb_midi_track_event_entry, 1, 0, 0},
    {0x46832f, model_bb_midi_track_tick_entry, 1, 0, 0},
    {0x468388, model_bb_midi_construct_entry, 0, 0, 0},
    {0x4683bf, model_bb_midi_load_entry, 1, 0, 0},
    {0x468412, model_bb_midi_read_entry, 1, 0, 0},
    {0x46907c, model_bb_midi_release_entry, 0, 0, 0},
    {0x4690d3, model_bb_instrument_song_start_entry, 0, 0, 0},
    {0x469190, model_bb_instrument_song_rewind_entry, 0, 0, 0},
    {0x4691f1, model_bb_instrument_song_tick_entry, 1, 0, 0},
    {0x46928a, model_bb_instrument_device_initialize_entry, 1, 0, 0},
    {0x4692c5, model_bb_instrument_device_attach_entry, 3, 0, 0},
    {0x469304, model_bb_instrument_runtime_initialize_entry, 0, 0, 0},
    {0x4693a0, model_bb_instrument_runtime_release_entry, 0, 0, 0},
    {0x469422, model_bb_instrument_gain_set_entry, 2, 0, 0},
    {0x4694a5, model_bb_instrument_gain_get_entry, 0, 0, 0},
    {0x4694c1, model_bb_instrument_song_load_entry, 1, 0, 0},
    {0x469585, model_bb_instrument_bank_read_entry, 1, 0, 0},
    {0x46987a, model_bb_instrument_bank_load_entry, 1, 0, 0},
    {0x4698cd, model_bb_instrument_bank_write_entry, 1, 0, 0},
    {0x469b48, model_bb_instrument_bank_save_entry, 1, 0, 0},
    {0x469b9b, model_bb_instrument_bank_import_entry, 1, 0, 0},
    {0x469bee, model_bb_instrument_bank_export_entry, 1, 0, 0},
    {0x469c41, model_bb_instrument_worker_entry, 1, 0, 0},
    {0x469dc4, model_bb_instrument_program_append_entry, 1, 0, 0},
    {0x469dfc, model_bb_instrument_note_on_entry, 3, 0, 0},
    {0x469ee9, model_bb_instrument_note_off_entry, 3, 0, 0},
    {0x469f48, model_bb_instrument_midi_open_entry, 0, 0, 0},
    {0x469fb9, model_bb_instrument_midi_close_entry, 0, 0, 0},
};
const struct bb_model_target *bb_legacy_model_find(void *unused, uint32_t id) {
  (void)unused;
  for (unsigned i = 0; i < sizeof targets / sizeof *targets; i++)
    if (targets[i].identity == id)
      return &targets[i];
  return NULL;
}
