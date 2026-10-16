/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_GAME_AUDIO_MODELS_H
#define BB_GAME_AUDIO_MODELS_H
#include "model_runtime.h"
/* Original logical identities, C bodies, legacy entries, argument words and
 * protected local bytes. Entry names also identify packed callback values. */
#define BB_GAME_AUDIO_METHODS(X) \
  X(0x00402210u, bb_sound_resource_play_c, bb_sound_resource_play_entry, 1, 0) \
  X(0x00402300u, bb_sound_resource_pan_c, bb_sound_resource_pan_entry, 1, 0) \
  X(0x00402350u, bb_sound_resource_volume_c, bb_sound_resource_volume_entry, 1, 0) \
  X(0x004023a0u, bb_sound_resource_voice_c, bb_sound_resource_voice_entry, 0, 16) \
  X(0x00415730u, bb_sound_play_c, bb_sound_play_entry, 1, 0) \
  X(0x00415740u, bb_sound_position_c, bb_sound_position_entry, 3, 0) \
  X(0x00415860u, bb_sample_play_c, bb_sample_play_entry, 1, 0) \
  X(0x00415880u, bb_sample_position_c, bb_sample_position_entry, 3, 0) \
  X(0x00455d90u, bb_sound_bank_replace_c, bb_sound_bank_replace_entry, 1, 64) \
  X(0x00455dd0u, bb_sound_bank_load_c, bb_sound_bank_load_entry, 1, 64) \
  X(0x00455de0u, bb_sound_state_clear_c, bb_sound_state_clear_entry, 1, 64) \
  X(0x00455df0u, bb_sound_states_clear_c, bb_sound_states_clear_entry, 0, 64) \
  X(0x00455e10u, bb_sound_state_activate_c, bb_sound_state_activate_entry, 1, 64) \
  X(0x00455e20u, bb_sound_state_active_c, bb_sound_state_active_entry, 1, 64) \
  X(0x00455e30u, bb_sound_state_count_c, bb_sound_state_count_entry, 0, 64) \
  X(0x00455e50u, bb_sound_selection_update_c, bb_sound_selection_update_entry, 0, 64) \
  X(0x00455f40u, bb_sound_current_stop_c, bb_sound_current_stop_entry, 0, 64) \
  X(0x00455f50u, bb_sound_bank_stop_c, bb_sound_bank_stop_entry, 0, 64) \
  X(0x00455f70u, bb_sound_flags_reset_c, bb_sound_flags_reset_entry, 0, 64) \
  X(0x00455fd0u, bb_sound_cues_reset_c, bb_sound_cues_reset_entry, 0, 64) \
  X(0x00456030u, bb_sound_mode_set_c, bb_sound_mode_set_entry, 1, 64) \
  X(0x004560d0u, bb_sound_transition_c, bb_sound_transition_entry, 8, 64) \
  X(0x00456320u, bb_sample_lookup_c, bb_sample_lookup_entry, 4, 0) \
  X(0x00456920u, bb_sound_selector_456920_c, bb_sound_selector_456920_entry, 0, 64) \
  X(0x00456960u, bb_sound_selector_456960_c, bb_sound_selector_456960_entry, 0, 64) \
  X(0x004569a0u, bb_sound_selector_4569a0_c, bb_sound_selector_4569a0_entry, 0, 64) \
  X(0x004569e0u, bb_sound_selector_4569e0_c, bb_sound_selector_4569e0_entry, 0, 64) \
  X(0x00456a20u, bb_sound_selector_456a20_c, bb_sound_selector_456a20_entry, 0, 64) \
  X(0x00456a50u, bb_sound_selector_456a50_c, bb_sound_selector_456a50_entry, 0, 64) \
  X(0x00456aa0u, bb_sound_cue_456aa0_c, bb_sound_cue_456aa0_entry, 1, 64) \
  X(0x00456d20u, bb_sound_cue_456d20_c, bb_sound_cue_456d20_entry, 1, 64) \
  X(0x00456ee0u, bb_sound_cue_456ee0_c, bb_sound_cue_456ee0_entry, 1, 64) \
  X(0x004573d0u, bb_sound_cue_4573d0_c, bb_sound_cue_4573d0_entry, 1, 64) \
  X(0x00457590u, bb_sound_cue_457590_c, bb_sound_cue_457590_entry, 1, 64) \
  X(0x00457820u, bb_sound_cue_457820_c, bb_sound_cue_457820_entry, 1, 64) \
  X(0x004579e0u, bb_sound_cue_4579e0_c, bb_sound_cue_4579e0_entry, 2, 64) \
  X(0x00457bf0u, bb_sound_selector_457bf0_c, bb_sound_selector_457bf0_entry, 0, 64) \
  X(0x00457c80u, bb_sound_selector_457c80_c, bb_sound_selector_457c80_entry, 0, 64) \
  X(0x00457cc0u, bb_sound_selector_457cc0_c, bb_sound_selector_457cc0_entry, 0, 64) \
  X(0x00457d30u, bb_sound_selector_457d30_c, bb_sound_selector_457d30_entry, 0, 64) \
  X(0x00457d60u, bb_sound_selector_457d60_c, bb_sound_selector_457d60_entry, 0, 64) \
  X(0x00457d90u, bb_sound_selector_457d90_c, bb_sound_selector_457d90_entry, 0, 64) \
  X(0x00457de0u, bb_sound_selector_457de0_c, bb_sound_selector_457de0_entry, 0, 64) \
  X(0x00457e20u, bb_sound_selector_457e20_c, bb_sound_selector_457e20_entry, 0, 64) \
  X(0x00457e50u, bb_sound_selector_457e50_c, bb_sound_selector_457e50_entry, 0, 64) \
  X(0x00457e80u, bb_sound_selector_457e80_c, bb_sound_selector_457e80_entry, 0, 64) \
  X(0x00457ec0u, bb_sound_selector_457ec0_c, bb_sound_selector_457ec0_entry, 1, 64) \
  X(0x00457f50u, bb_sound_selector_457f50_c, bb_sound_selector_457f50_entry, 0, 64) \
  X(0x00457f80u, bb_sound_selector_457f80_c, bb_sound_selector_457f80_entry, 0, 64) \
  X(0x00457ff0u, bb_sound_selector_457ff0_c, bb_sound_selector_457ff0_entry, 0, 64) \
  X(0x00458050u, bb_sound_selector_458050_c, bb_sound_selector_458050_entry, 0, 64) \
  X(0x004580a0u, bb_sound_cue_4580a0_c, bb_sound_cue_4580a0_entry, 2, 64) \
  X(0x004581c0u, bb_sound_cue_4581c0_c, bb_sound_cue_4581c0_entry, 2, 64) \
  X(0x00458350u, bb_sound_cue_458350_c, bb_sound_cue_458350_entry, 2, 64) \
  X(0x004584d0u, bb_sound_cue_4584d0_c, bb_sound_cue_4584d0_entry, 1, 64) \
  X(0x00458660u, bb_sound_cue_458660_c, bb_sound_cue_458660_entry, 2, 64) \
  X(0x00458890u, bb_sound_cue_458890_c, bb_sound_cue_458890_entry, 2, 64) \
  X(0x00458ad0u, bb_sound_cue_458ad0_c, bb_sound_cue_458ad0_entry, 2, 64) \
  X(0x00458c90u, bb_sound_cue_458c90_c, bb_sound_cue_458c90_entry, 2, 64) \
  X(0x00458e10u, bb_sound_cue_458e10_c, bb_sound_cue_458e10_entry, 1, 64) \
  X(0x00458fa0u, bb_sound_cue_458fa0_c, bb_sound_cue_458fa0_entry, 2, 64) \
  X(0x00459070u, bb_sound_cue_459070_c, bb_sound_cue_459070_entry, 2, 64) \
  X(0x004594a0u, bb_sound_cue_4594a0_c, bb_sound_cue_4594a0_entry, 2, 64) \
  X(0x00459620u, bb_sound_cue_459620_c, bb_sound_cue_459620_entry, 2, 64) \
  X(0x004597a0u, bb_sound_cue_4597a0_c, bb_sound_cue_4597a0_entry, 1, 64) \
  X(0x00459930u, bb_sound_init_c, bb_sound_init_entry, 0, 64) \
  X(0x004599f0u, bb_sound_shutdown_c, bb_sound_shutdown_entry, 0, 64) \
  X(0x00459a50u, bb_sample_schedule_c, bb_sample_schedule_entry, 4, 16) \
  X(0x00459b30u, bb_sound_enable_c, bb_sound_enable_entry, 1, 64) \
  X(0x00459b80u, bb_sound_event_c, bb_sound_event_entry, 2, 64) \
  X(0x00459ca0u, bb_sound_property_c, bb_sound_property_entry, 2, 64) \
  X(0x00459d80u, bb_sound_update_c, bb_sound_update_entry, 0, 64) \
  X(0x004637f0u, bb_sample_bank_load_c, bb_sample_bank_load_entry, 1, 16) \
  X(0x004638b2u, bb_sample_bank_free_c, bb_sample_bank_free_entry, 1, 4) \
  X(0x004638d0u, bb_sample_status_text_c, bb_sample_status_text_entry, 0, 16) \
  X(0x00463982u, bb_sample_session_init_c, bb_sample_session_init_entry, 2, 12) \
  X(0x00463befu, bb_sample_session_shutdown_c, bb_sample_session_shutdown_entry, 0, 4) \
  X(0x00463c46u, bb_sample_channels_refresh_c, bb_sample_channels_refresh_entry, 0, 8) \
  X(0x00463cb8u, bb_sample_channel_id_c, bb_sample_channel_id_entry, 2, 4) \
  X(0x00463d1cu, bb_sample_instance_count_c, bb_sample_instance_count_entry, 2, 12) \
  X(0x00463daau, bb_sample_channel_count_c, bb_sample_channel_count_entry, 0, 4) \
  X(0x00463dd3u, bb_sample_channel_play_c, bb_sample_channel_play_entry, 6, 24) \
  X(0x00463f6bu, bb_sample_channel_submit_c, bb_sample_channel_submit_entry, 7, 60) \
  X(0x004643b0u, bb_sample_channel_stop_c, bb_sample_channel_stop_entry, 1, 4) \
  X(0x004643eeu, bb_sample_channel_active_c, bb_sample_channel_active_entry, 1, 4) \
  X(0x00464441u, bb_sample_device_text_c, bb_sample_device_text_entry, 0, 8) \
  X(0x004644b0u, bb_sample_channel_pan_c, bb_sample_channel_pan_entry, 2, 8) \
  X(0x00464562u, bb_sample_channel_volume_c, bb_sample_channel_volume_entry, 2, 4) \
  X(0x004645ceu, bb_sample_channel_pitch_c, bb_sample_channel_pitch_entry, 2, 12) \
  X(0x0046465eu, bb_sample_channels_release_c, bb_sample_channels_release_entry, 0, 8) \
  X(0x00464704u, bb_sample_bank_stop_c, bb_sample_bank_stop_entry, 1, 8) \
  X(0x0046479eu, bb_sample_session_pause_c, bb_sample_session_pause_entry, 0, 4) \
  X(0x004647d0u, bb_sample_session_resume_c, bb_sample_session_resume_entry, 0, 4) \
  X(0x00464802u, bb_sample_session_enable_c, bb_sample_session_enable_entry, 1, 4) \
  X(0x0046487au, bb_sample_session_options_c, bb_sample_session_options_entry, 2, 8) \
  X(0x00464a30u, bb_sample_effect_reset_c, bb_sample_effect_reset_entry, 1, 4) \
  X(0x00464ac0u, bb_sample_effects_reset_c, bb_sample_effects_reset_entry, 0, 8) \
  X(0x00464b01u, bb_sample_effects_init_c, bb_sample_effects_init_entry, 0, 4) \
  X(0x00464b4eu, bb_sample_effects_stop_c, bb_sample_effects_stop_entry, 1, 8) \
  X(0x00464c12u, bb_sample_effect_reserve_c, bb_sample_effect_reserve_entry, 1, 8) \
  X(0x00464cafu, bb_sample_effect_release_c, bb_sample_effect_release_entry, 1, 4) \
  X(0x00464ccbu, bb_sample_effect_bind_c, bb_sample_effect_bind_entry, 2, 4) \
  X(0x00464d21u, bb_sample_effect_channel_c, bb_sample_effect_channel_entry, 1, 4) \
  X(0x00464d3du, bb_sample_random_range_c, bb_sample_random_range_entry, 2, 12) \
  X(0x00464d8bu, bb_sample_random_clock_seed_c, bb_sample_random_clock_seed_entry, 0, 4) \
  X(0x00464daau, bb_sample_gain_c, bb_sample_gain_entry, 1, 8) \
  X(0x00464e0cu, bb_sample_oscillation_step_c, bb_sample_oscillation_step_entry, 6, 8) \
  X(0x00464ef3u, bb_sample_oscillation_init_c, bb_sample_oscillation_init_entry, 2, 4) \
  X(0x00464f0cu, bb_sample_ramp_step_c, bb_sample_ramp_step_entry, 5, 8) \
  X(0x00464fbbu, bb_sample_random_ramp_step_c, bb_sample_random_ramp_step_entry, 7, 8) \
  X(0x004650c1u, bb_sample_loop_fade_step_c, bb_sample_loop_fade_step_entry, 10, 28) \
  X(0x00465629u, bb_sample_sequence_init_c, bb_sample_sequence_init_entry, 9, 4) \
  X(0x004656ecu, bb_sample_sequence_stop_c, bb_sample_sequence_stop_entry, 1, 4) \
  X(0x0046579eu, bb_sample_sequence_volume_c, bb_sample_sequence_volume_entry, 2, 4) \
  X(0x004657c9u, bb_sample_sequence_pan_c, bb_sample_sequence_pan_entry, 2, 4) \
  X(0x004657f4u, bb_sample_sequence_pitch_c, bb_sample_sequence_pitch_entry, 2, 4) \
  X(0x0046581fu, bb_sample_sequence_loops_c, bb_sample_sequence_loops_entry, 2, 4) \
  X(0x0046584au, bb_sample_continuous_step_c, bb_sample_continuous_step_entry, 1, 8) \
  X(0x004659d6u, bb_sample_crossfade_step_c, bb_sample_crossfade_step_entry, 1, 44) \
  X(0x00465f10u, bb_sample_pitch_scale_c, bb_sample_pitch_scale_entry, 1, 16) \
  X(0x0046a170u, bb_audio_resource_volume_c, bb_audio_resource_volume_entry, 1, 0) \
  X(0x0046a190u, bb_audio_resource_init_c, bb_audio_resource_init_entry, 1, 0) \
  X(0x0046a210u, bb_audio_timer_start_c, bb_audio_timer_start_entry, 1, 8) \
  X(0x0046a2a0u, bb_audio_buffer_create_c, bb_audio_buffer_create_entry, 3, 48) \
  X(0x0046a400u, bb_audio_timer_callback_c, bb_audio_timer_callback_entry, 5, 0) \
  X(0x0046a430u, bb_audio_resource_update_c, bb_audio_resource_update_entry, 0, 16416) \
  X(0x0046a6c0u, bb_audio_buffer_gap_c, bb_audio_buffer_gap_entry, 1, 8) \
  X(0x0046a6f0u, bb_audio_voice_mix_c, bb_audio_voice_mix_entry, 3, 8) \
  X(0x0046a7b0u, bb_audio_voice_reset_c, bb_audio_voice_reset_entry, 0, 0) \
  X(0x0046a7d0u, bb_audio_voice_play_c, bb_audio_voice_play_entry, 5, 0) \
  X(0x0046a880u, bb_audio_read_integer_c, bb_audio_read_integer_entry, 0, 0) \
  X(0x0046a8e0u, bb_audio_voice_duration_c, bb_audio_voice_duration_entry, 1, 0) \
  X(0x0046a920u, bb_audio_voice_stop_c, bb_audio_voice_stop_entry, 1, 0) \
  X(0x0046a950u, bb_audio_voices_stop_c, bb_audio_voices_stop_entry, 0, 0) \
  X(0x0046a970u, bb_audio_resource_shutdown_c, bb_audio_resource_shutdown_entry, 0, 0) \
  X(0x0046a9a0u, bb_audio_timer_stop_c, bb_audio_timer_stop_entry, 0, 4) \
  X(0x0046aa20u, bb_audio_decode_init_c, bb_audio_decode_init_entry, 0, 8) \
  X(0x0046aaf0u, bb_audio_bits_read_c, bb_audio_bits_read_entry, 0, 0) \
  X(0x0046ab40u, bb_audio_decode_block_c, bb_audio_decode_block_entry, 0, 548) \
  X(0x0046ae30u, bb_audio_decode_residual_c, bb_audio_decode_residual_entry, 2, 8) \
  X(0x0046afe0u, bb_audio_bits_skip_c, bb_audio_bits_skip_entry, 0, 0) \
  X(0x0046b020u, bb_audio_interpolate_c, bb_audio_interpolate_entry, 0, 0) \
  X(0x0046b080u, bb_audio_synthesize_c, bb_audio_synthesize_entry, 1, 48) \
  X(0x0046ba10u, bb_audio_coefficients_c, bb_audio_coefficients_entry, 0, 108) \
  X(0x0046be00u, bb_sample_random_seed_c, bb_sample_random_seed_entry, 1, 4)
enum {
#define BB_AUDIO_ID(id, body, entry, argc, reserve) BB_AUDIO_##entry = id,
  BB_GAME_AUDIO_METHODS(BB_AUDIO_ID)
#undef BB_AUDIO_ID
  BB_AUDIO_bb_random = 0x0046be20u,
  BB_AUDIO_bb_entity_distance = 0x00434de0u
};
#define BB_AUDIO_DECLARE(id, body, entry, argc, reserve) void body(uint32_t [9]);
BB_GAME_AUDIO_METHODS(BB_AUDIO_DECLARE)
#undef BB_AUDIO_DECLARE
const struct bb_model_target *bb_game_audio_model_find(void *, uint32_t);
#endif
