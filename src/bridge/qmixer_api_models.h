/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_QMIXER_API_MODELS_H
#define BB_QMIXER_API_MODELS_H
#include <stdint.h>
#include <stddef.h>
/* Explicit C API recovery scopes. Columns: owner identity, entry name, frame
 * reserve, highest protected stage, WaveError action, WaveMemory action.
 * All API ranges start at stage zero; stage one owns a scoped mixer lock.
 * Catch states have no resources of their own. */
#define BB_QMIX_API_SCOPES(X) \
  X(0x100063e7u, bb_qmix_export_activate, 512, 1, bb_qmix_export_activate_catch_error_c, bb_qmix_export_activate_catch_memory_c) \
  X(0x100064fdu, bb_qmix_export_close_channel, 512, 1, bb_qmix_export_close_channel_catch_error_c, bb_qmix_export_close_channel_catch_memory_c) \
  X(0x10006598u, bb_qmix_export_close_session, 512, 0, bb_qmix_export_close_session_catch_error_c, bb_qmix_export_close_session_catch_memory_c) \
  X(0x10006615u, bb_qmix_export_configure_init, 2048, 0, NULL, bb_qmix_export_configure_init_catch_memory_c) \
  X(0x1000678eu, bb_qmix_export_enable_channel, 512, 1, bb_qmix_export_enable_channel_catch_error_c, bb_qmix_export_enable_channel_catch_memory_c) \
  X(0x100068e4u, bb_qmix_export_flush_channel, 512, 1, bb_qmix_export_flush_channel_catch_error_c, bb_qmix_export_flush_channel_catch_memory_c) \
  X(0x10006a2au, bb_qmix_export_free_wave, 512, 1, bb_qmix_export_free_wave_catch_error_c, bb_qmix_export_free_wave_catch_memory_c) \
  X(0x10006b0cu, bb_qmix_export_get_direct_sound, 512, 1, bb_qmix_export_get_direct_sound_catch_error_c, NULL) \
  X(0x10006c03u, bb_qmix_export_get_distance_mapping, 512, 1, NULL, bb_qmix_export_get_distance_mapping_catch_memory_c) \
  X(0x10006d51u, bb_qmix_export_get_frequency, 512, 1, bb_qmix_export_get_frequency_catch_error_c, bb_qmix_export_get_frequency_catch_memory_c) \
  X(0x10006e7fu, bb_qmix_export_get_listener_orientation, 512, 1, bb_qmix_export_get_listener_orientation_catch_error_c, NULL) \
  X(0x10006f5bu, bb_qmix_export_get_listener_position, 512, 1, bb_qmix_export_get_listener_position_catch_error_c, NULL) \
  X(0x1000703bu, bb_qmix_export_get_listener_velocity, 512, 1, bb_qmix_export_get_listener_velocity_catch_error_c, NULL) \
  X(0x10007127u, bb_qmix_export_get_options, 512, 1, NULL, bb_qmix_export_get_options_catch_memory_c) \
  X(0x100071d3u, bb_qmix_export_get_pan, 512, 0, bb_qmix_export_get_pan_catch_error_c, bb_qmix_export_get_pan_catch_memory_c) \
  X(0x100072d1u, bb_qmix_export_get_pan_rate, 512, 1, bb_qmix_export_get_pan_rate_catch_error_c, bb_qmix_export_get_pan_rate_catch_memory_c) \
  X(0x100073f4u, bb_qmix_export_get_position, 512, 1, bb_qmix_export_get_position_catch_error_c, bb_qmix_export_get_position_catch_memory_c) \
  X(0x100074ccu, bb_qmix_export_get_room_size, 512, 1, bb_qmix_export_get_room_size_catch_error_c, NULL) \
  X(0x100075c0u, bb_qmix_export_get_source_cone, 512, 1, bb_qmix_export_get_source_cone_catch_error_c, NULL) \
  X(0x100076a8u, bb_qmix_export_get_source_position, 512, 1, bb_qmix_export_get_source_position_catch_error_c, NULL) \
  X(0x10007798u, bb_qmix_export_get_source_velocity, 512, 1, bb_qmix_export_get_source_velocity_catch_error_c, NULL) \
  X(0x1000787bu, bb_qmix_export_get_speed_of_sound, 512, 1, bb_qmix_export_get_speed_of_sound_catch_error_c, NULL) \
  X(0x1000798cu, bb_qmix_export_get_play_position, 512, 1, bb_qmix_export_get_play_position_catch_error_c, bb_qmix_export_get_play_position_catch_memory_c) \
  X(0x10007a91u, bb_qmix_export_get_volume, 512, 1, bb_qmix_export_get_volume_catch_error_c, bb_qmix_export_get_volume_catch_memory_c) \
  X(0x10007bb3u, bb_qmix_export_is_channel_done, 512, 1, NULL, bb_qmix_export_is_channel_done_catch_memory_c) \
  X(0x10007cbbu, bb_qmix_export_open_channel, 512, 1, bb_qmix_export_open_channel_catch_error_c, bb_qmix_export_open_channel_catch_memory_c) \
  X(0x10007e2eu, bb_qmix_export_pause_channel, 512, 1, bb_qmix_export_pause_channel_catch_error_c, bb_qmix_export_pause_channel_catch_memory_c) \
  X(0x10007f29u, bb_qmix_export_play, 512, 1, bb_qmix_export_play_catch_error_c, bb_qmix_export_play_catch_memory_c) \
  X(0x100080d6u, bb_qmix_export_restart_channel, 512, 1, bb_qmix_export_restart_channel_catch_error_c, bb_qmix_export_restart_channel_catch_memory_c) \
  X(0x10008228u, bb_qmix_export_set_distance_mapping, 512, 1, bb_qmix_export_set_distance_mapping_catch_error_c, bb_qmix_export_set_distance_mapping_catch_memory_c) \
  X(0x10008388u, bb_qmix_export_set_frequency, 512, 1, bb_qmix_export_set_frequency_catch_error_c, bb_qmix_export_set_frequency_catch_memory_c) \
  X(0x1000849bu, bb_qmix_export_set_listener_orientation, 512, 1, bb_qmix_export_set_listener_orientation_catch_error_c, bb_qmix_export_set_listener_orientation_catch_memory_c) \
  X(0x100085a7u, bb_qmix_export_set_listener_position, 512, 1, bb_qmix_export_set_listener_position_catch_error_c, bb_qmix_export_set_listener_position_catch_memory_c) \
  X(0x100086b7u, bb_qmix_export_set_listener_velocity, 512, 1, bb_qmix_export_set_listener_velocity_catch_error_c, bb_qmix_export_set_listener_velocity_catch_memory_c) \
  X(0x100087aau, bb_qmix_export_set_options, 512, 1, NULL, bb_qmix_export_set_options_catch_memory_c) \
  X(0x100088afu, bb_qmix_export_set_pan, 512, 1, bb_qmix_export_set_pan_catch_error_c, bb_qmix_export_set_pan_catch_memory_c) \
  X(0x10008a08u, bb_qmix_export_set_pan_rate, 512, 1, bb_qmix_export_set_pan_rate_catch_error_c, bb_qmix_export_set_pan_rate_catch_memory_c) \
  X(0x10008b68u, bb_qmix_export_set_position, 512, 1, bb_qmix_export_set_position_catch_error_c, bb_qmix_export_set_position_catch_memory_c) \
  X(0x10008c67u, bb_qmix_export_set_remix_buffer_size, 512, 1, bb_qmix_export_set_remix_buffer_size_catch_error_c, bb_qmix_export_set_remix_buffer_size_catch_memory_c) \
  X(0x10008d77u, bb_qmix_export_set_room_size, 512, 1, bb_qmix_export_set_room_size_catch_error_c, bb_qmix_export_set_room_size_catch_memory_c) \
  X(0x10008ee8u, bb_qmix_export_set_source_cone, 512, 1, bb_qmix_export_set_source_cone_catch_error_c, bb_qmix_export_set_source_cone_catch_memory_c) \
  X(0x10009048u, bb_qmix_export_set_source_position, 512, 1, bb_qmix_export_set_source_position_catch_error_c, bb_qmix_export_set_source_position_catch_memory_c) \
  X(0x100091a8u, bb_qmix_export_set_source_velocity, 512, 1, bb_qmix_export_set_source_velocity_catch_error_c, bb_qmix_export_set_source_velocity_catch_memory_c) \
  X(0x100092c0u, bb_qmix_export_set_speaker_placement, 512, 1, bb_qmix_export_set_speaker_placement_catch_error_c, bb_qmix_export_set_speaker_placement_catch_memory_c) \
  X(0x100093c7u, bb_qmix_export_set_speed_of_sound, 512, 1, bb_qmix_export_set_speed_of_sound_catch_error_c, bb_qmix_export_set_speed_of_sound_catch_memory_c) \
  X(0x10009528u, bb_qmix_export_set_volume, 512, 1, bb_qmix_export_set_volume_catch_error_c, bb_qmix_export_set_volume_catch_memory_c) \
  X(0x10009612u, bb_qmix_export_start_profile, 512, 1, NULL, bb_qmix_export_start_profile_catch_memory_c) \
  X(0x1000975eu, bb_qmix_export_stop_channel, 512, 1, bb_qmix_export_stop_channel_catch_error_c, bb_qmix_export_stop_channel_catch_memory_c) \
  X(0x10009846u, bb_qmix_export_stop_profile, 512, 1, NULL, bb_qmix_export_stop_profile_catch_memory_c)
#ifdef BB_MODEL_RUNTIME
void bb_qmix_model_api_call(void (*)(uint32_t *), uint32_t *, unsigned,
                            unsigned, void (*)(uint32_t *),
                            void (*)(uint32_t *));
#endif
#endif
