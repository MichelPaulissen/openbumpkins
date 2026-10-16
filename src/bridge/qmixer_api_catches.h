/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_QMIXER_API_CATCHES_H
#define BB_QMIXER_API_CATCHES_H
#include <stdint.h>

#define BB_QMIX_API_CATCHES(X) \
  X(0x100063d6u, bb_qmix_export_activate_catch_error, -24, -40, 0x100063e2u, 0) \
  X(0x100063bau, bb_qmix_export_activate_catch_memory, -28, 0, 0x100063d1u, 1) \
  X(0x100064ecu, bb_qmix_export_close_channel_catch_error, -24, -40, 0x100064f8u, 0) \
  X(0x100064d0u, bb_qmix_export_close_channel_catch_memory, -28, 0, 0x100064e7u, 1) \
  X(0x10006587u, bb_qmix_export_close_session_catch_error, -20, -28, 0x10006593u, 0) \
  X(0x1000656bu, bb_qmix_export_close_session_catch_memory, -24, 0, 0x10006582u, 1) \
  X(0x10006605u, bb_qmix_export_configure_init_catch_memory, 0, 0, 0x100065e8u, 1) \
  X(0x1000677du, bb_qmix_export_enable_channel_catch_error, -28, -48, 0x10006789u, 0) \
  X(0x10006761u, bb_qmix_export_enable_channel_catch_memory, -32, 0, 0x10006778u, 1) \
  X(0x100068d3u, bb_qmix_export_flush_channel_catch_error, -28, -48, 0x100068dfu, 0) \
  X(0x100068b7u, bb_qmix_export_flush_channel_catch_memory, -32, 0, 0x100068ceu, 1) \
  X(0x10006a16u, bb_qmix_export_free_wave_catch_error, -28, -52, 0x10006a22u, 0) \
  X(0x100069f7u, bb_qmix_export_free_wave_catch_memory, -32, 0, 0x10006a0eu, 1) \
  X(0x10006afbu, bb_qmix_export_get_direct_sound_catch_error, -24, -36, 0x10006b07u, 0) \
  X(0x10006be7u, bb_qmix_export_get_distance_mapping_catch_memory, -24, 0, 0x10006bfeu, 1) \
  X(0x10006d40u, bb_qmix_export_get_frequency_catch_error, -24, -40, 0x10006d4cu, 0) \
  X(0x10006d24u, bb_qmix_export_get_frequency_catch_memory, -28, 0, 0x10006d3bu, 1) \
  X(0x10006e6eu, bb_qmix_export_get_listener_orientation_catch_error, -24, -36, 0x10006e7au, 0) \
  X(0x10006f4au, bb_qmix_export_get_listener_position_catch_error, -24, -36, 0x10006f56u, 0) \
  X(0x1000702au, bb_qmix_export_get_listener_velocity_catch_error, -24, -36, 0x10007036u, 0) \
  X(0x1000710bu, bb_qmix_export_get_options_catch_memory, -24, 0, 0x10007122u, 1) \
  X(0x100071c2u, bb_qmix_export_get_pan_catch_error, -20, -28, 0x100071ceu, 0) \
  X(0x100071a6u, bb_qmix_export_get_pan_catch_memory, -24, 0, 0x100071bdu, 1) \
  X(0x100072c0u, bb_qmix_export_get_pan_rate_catch_error, -24, -40, 0x100072ccu, 0) \
  X(0x100072a4u, bb_qmix_export_get_pan_rate_catch_memory, -28, 0, 0x100072bbu, 1) \
  X(0x100073e3u, bb_qmix_export_get_position_catch_error, -24, -40, 0x100073efu, 0) \
  X(0x100073c7u, bb_qmix_export_get_position_catch_memory, -28, 0, 0x100073deu, 1) \
  X(0x100074bbu, bb_qmix_export_get_room_size_catch_error, -24, -36, 0x100074c7u, 0) \
  X(0x100075afu, bb_qmix_export_get_source_cone_catch_error, -24, -36, 0x100075bbu, 0) \
  X(0x10007697u, bb_qmix_export_get_source_position_catch_error, -24, -36, 0x100076a3u, 0) \
  X(0x10007787u, bb_qmix_export_get_source_velocity_catch_error, -24, -36, 0x10007793u, 0) \
  X(0x1000786au, bb_qmix_export_get_speed_of_sound_catch_error, -24, -36, 0x10007876u, 0) \
  X(0x1000797bu, bb_qmix_export_get_play_position_catch_error, -24, -40, 0x10007987u, 0) \
  X(0x1000795fu, bb_qmix_export_get_play_position_catch_memory, -28, 0, 0x10007976u, 1) \
  X(0x10007a80u, bb_qmix_export_get_volume_catch_error, -24, -40, 0x10007a8cu, 0) \
  X(0x10007a64u, bb_qmix_export_get_volume_catch_memory, -28, 0, 0x10007a7bu, 1) \
  X(0x10007b97u, bb_qmix_export_is_channel_done_catch_memory, -24, 0, 0x10007baeu, 1) \
  X(0x10007c9fu, bb_qmix_export_open_channel_catch_error, -24, -40, 0x10007cabu, 0) \
  X(0x10007c83u, bb_qmix_export_open_channel_catch_memory, -28, 0, 0x10007c9au, 1) \
  X(0x10007e1du, bb_qmix_export_pause_channel_catch_error, -28, -48, 0x10007e29u, 0) \
  X(0x10007e01u, bb_qmix_export_pause_channel_catch_memory, -32, 0, 0x10007e18u, 1) \
  X(0x10007f18u, bb_qmix_export_play_catch_error, -24, -40, 0x10007f24u, 0) \
  X(0x10007efcu, bb_qmix_export_play_catch_memory, -28, 0, 0x10007f13u, 1) \
  X(0x100080c5u, bb_qmix_export_restart_channel_catch_error, -28, -48, 0x100080d1u, 0) \
  X(0x100080a9u, bb_qmix_export_restart_channel_catch_memory, -32, 0, 0x100080c0u, 1) \
  X(0x10008217u, bb_qmix_export_set_distance_mapping_catch_error, -28, -48, 0x10008223u, 0) \
  X(0x100081fbu, bb_qmix_export_set_distance_mapping_catch_memory, -32, 0, 0x10008212u, 1) \
  X(0x10008377u, bb_qmix_export_set_frequency_catch_error, -28, -48, 0x10008383u, 0) \
  X(0x1000835bu, bb_qmix_export_set_frequency_catch_memory, -32, 0, 0x10008372u, 1) \
  X(0x1000848au, bb_qmix_export_set_listener_orientation_catch_error, -24, -40, 0x10008496u, 0) \
  X(0x1000846eu, bb_qmix_export_set_listener_orientation_catch_memory, -28, 0, 0x10008485u, 1) \
  X(0x10008596u, bb_qmix_export_set_listener_position_catch_error, -24, -40, 0x100085a2u, 0) \
  X(0x1000857au, bb_qmix_export_set_listener_position_catch_memory, -28, 0, 0x10008591u, 1) \
  X(0x100086a6u, bb_qmix_export_set_listener_velocity_catch_error, -24, -40, 0x100086b2u, 0) \
  X(0x1000868au, bb_qmix_export_set_listener_velocity_catch_memory, -28, 0, 0x100086a1u, 1) \
  X(0x1000878eu, bb_qmix_export_set_options_catch_memory, -24, 0, 0x100087a5u, 1) \
  X(0x1000889eu, bb_qmix_export_set_pan_catch_error, -24, -40, 0x100088aau, 0) \
  X(0x10008882u, bb_qmix_export_set_pan_catch_memory, -28, 0, 0x10008899u, 1) \
  X(0x100089f7u, bb_qmix_export_set_pan_rate_catch_error, -28, -48, 0x10008a03u, 0) \
  X(0x100089dbu, bb_qmix_export_set_pan_rate_catch_memory, -32, 0, 0x100089f2u, 1) \
  X(0x10008b57u, bb_qmix_export_set_position_catch_error, -28, -48, 0x10008b63u, 0) \
  X(0x10008b3bu, bb_qmix_export_set_position_catch_memory, -32, 0, 0x10008b52u, 1) \
  X(0x10008c56u, bb_qmix_export_set_remix_buffer_size_catch_error, -24, -40, 0x10008c62u, 0) \
  X(0x10008c3au, bb_qmix_export_set_remix_buffer_size_catch_memory, -28, 0, 0x10008c51u, 1) \
  X(0x10008d66u, bb_qmix_export_set_room_size_catch_error, -24, -40, 0x10008d72u, 0) \
  X(0x10008d4au, bb_qmix_export_set_room_size_catch_memory, -28, 0, 0x10008d61u, 1) \
  X(0x10008ed7u, bb_qmix_export_set_source_cone_catch_error, -28, -48, 0x10008ee3u, 0) \
  X(0x10008ebbu, bb_qmix_export_set_source_cone_catch_memory, -32, 0, 0x10008ed2u, 1) \
  X(0x10009037u, bb_qmix_export_set_source_position_catch_error, -28, -48, 0x10009043u, 0) \
  X(0x1000901bu, bb_qmix_export_set_source_position_catch_memory, -32, 0, 0x10009032u, 1) \
  X(0x10009197u, bb_qmix_export_set_source_velocity_catch_error, -28, -48, 0x100091a3u, 0) \
  X(0x1000917bu, bb_qmix_export_set_source_velocity_catch_memory, -32, 0, 0x10009192u, 1) \
  X(0x100092afu, bb_qmix_export_set_speaker_placement_catch_error, -24, -44, 0x100092bbu, 0) \
  X(0x10009293u, bb_qmix_export_set_speaker_placement_catch_memory, -28, 0, 0x100092aau, 1) \
  X(0x100093b6u, bb_qmix_export_set_speed_of_sound_catch_error, -24, -40, 0x100093c2u, 0) \
  X(0x1000939au, bb_qmix_export_set_speed_of_sound_catch_memory, -28, 0, 0x100093b1u, 1) \
  X(0x10009517u, bb_qmix_export_set_volume_catch_error, -28, -48, 0x10009523u, 0) \
  X(0x100094fbu, bb_qmix_export_set_volume_catch_memory, -32, 0, 0x10009512u, 1) \
  X(0x100095f6u, bb_qmix_export_start_profile_catch_memory, -24, 0, 0x1000960du, 1) \
  X(0x1000974du, bb_qmix_export_stop_channel_catch_error, -28, -48, 0x10009759u, 0) \
  X(0x10009731u, bb_qmix_export_stop_channel_catch_memory, -32, 0, 0x10009748u, 1) \
  X(0x1000982au, bb_qmix_export_stop_profile_catch_memory, -24, 0, 0x10009841u, 1)

#define BB_QMIX_API_RETURNS(X) \
  X(0x100063e2u, bb_qmix_export_activate_catch_error_return, -24, 0, 8, "popl %edi;popl %esi;popl %ebx;") \
  X(0x100063d1u, bb_qmix_export_activate_catch_memory_return, -28, 0, 8, "popl %edi;popl %esi;popl %ebx;") \
  X(0x100064f8u, bb_qmix_export_close_channel_catch_error_return, -24, 0, 12, "popl %edi;popl %esi;popl %ebx;") \
  X(0x100064e7u, bb_qmix_export_close_channel_catch_memory_return, -28, 0, 12, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10006593u, bb_qmix_export_close_session_catch_error_return, -20, 0, 4, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10006582u, bb_qmix_export_close_session_catch_memory_return, -24, 0, 4, "popl %edi;popl %esi;popl %ebx;") \
  X(0x100065e8u, bb_qmix_export_configure_init_catch_memory_return, -20, -4, 4, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10006789u, bb_qmix_export_enable_channel_catch_error_return, -28, 0, 16, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10006778u, bb_qmix_export_enable_channel_catch_memory_return, -32, 0, 16, "popl %edi;popl %esi;popl %ebx;") \
  X(0x100068dfu, bb_qmix_export_flush_channel_catch_error_return, -28, 0, 12, "popl %edi;popl %esi;popl %ebx;") \
  X(0x100068ceu, bb_qmix_export_flush_channel_catch_memory_return, -32, 0, 12, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10006a22u, bb_qmix_export_free_wave_catch_error_return, -28, 0, 8, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10006a0eu, bb_qmix_export_free_wave_catch_memory_return, -32, 0, 8, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10006b07u, bb_qmix_export_get_direct_sound_catch_error_return, -24, 0, 8, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10006bfeu, bb_qmix_export_get_distance_mapping_catch_memory_return, -24, 0, 12, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10006d4cu, bb_qmix_export_get_frequency_catch_error_return, -24, 0, 12, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10006d3bu, bb_qmix_export_get_frequency_catch_memory_return, -28, 0, 12, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10006e7au, bb_qmix_export_get_listener_orientation_catch_error_return, -24, 0, 12, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10006f56u, bb_qmix_export_get_listener_position_catch_error_return, -24, 0, 8, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10007036u, bb_qmix_export_get_listener_velocity_catch_error_return, -24, 0, 8, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10007122u, bb_qmix_export_get_options_catch_memory_return, -24, 0, 8, "popl %edi;popl %esi;popl %ebx;") \
  X(0x100071ceu, bb_qmix_export_get_pan_catch_error_return, -20, 0, 12, "popl %edi;popl %esi;popl %ebx;") \
  X(0x100071bdu, bb_qmix_export_get_pan_catch_memory_return, -24, 0, 12, "popl %edi;popl %esi;popl %ebx;") \
  X(0x100072ccu, bb_qmix_export_get_pan_rate_catch_error_return, -24, 0, 12, "popl %edi;popl %esi;popl %ebx;") \
  X(0x100072bbu, bb_qmix_export_get_pan_rate_catch_memory_return, -28, 0, 12, "popl %edi;popl %esi;popl %ebx;") \
  X(0x100073efu, bb_qmix_export_get_position_catch_error_return, -24, 0, 12, "popl %edi;popl %esi;popl %ebx;") \
  X(0x100073deu, bb_qmix_export_get_position_catch_memory_return, -28, 0, 12, "popl %edi;popl %esi;popl %ebx;") \
  X(0x100074c7u, bb_qmix_export_get_room_size_catch_error_return, -24, 0, 8, "popl %edi;popl %esi;popl %ebx;") \
  X(0x100075bbu, bb_qmix_export_get_source_cone_catch_error_return, -24, 0, 20, "popl %edi;popl %esi;popl %ebx;") \
  X(0x100076a3u, bb_qmix_export_get_source_position_catch_error_return, -24, 0, 12, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10007793u, bb_qmix_export_get_source_velocity_catch_error_return, -24, 0, 12, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10007876u, bb_qmix_export_get_speed_of_sound_catch_error_return, -24, 0, 8, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10007987u, bb_qmix_export_get_play_position_catch_error_return, -24, 0, 20, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10007976u, bb_qmix_export_get_play_position_catch_memory_return, -28, 0, 20, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10007a8cu, bb_qmix_export_get_volume_catch_error_return, -24, 0, 12, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10007a7bu, bb_qmix_export_get_volume_catch_memory_return, -28, 0, 12, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10007baeu, bb_qmix_export_is_channel_done_catch_memory_return, -24, 0, 8, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10007cabu, bb_qmix_export_open_channel_catch_error_return, -24, 0, 12, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10007c9au, bb_qmix_export_open_channel_catch_memory_return, -28, 0, 12, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10007e29u, bb_qmix_export_pause_channel_catch_error_return, -28, 0, 12, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10007e18u, bb_qmix_export_pause_channel_catch_memory_return, -32, 0, 12, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10007f24u, bb_qmix_export_play_catch_error_return, -24, 0, 4, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10007f13u, bb_qmix_export_play_catch_memory_return, -28, 0, 4, "popl %edi;popl %esi;popl %ebx;") \
  X(0x100080d1u, bb_qmix_export_restart_channel_catch_error_return, -28, 0, 12, "popl %edi;popl %esi;popl %ebx;") \
  X(0x100080c0u, bb_qmix_export_restart_channel_catch_memory_return, -32, 0, 12, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10008223u, bb_qmix_export_set_distance_mapping_catch_error_return, -28, 0, 16, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10008212u, bb_qmix_export_set_distance_mapping_catch_memory_return, -32, 0, 16, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10008383u, bb_qmix_export_set_frequency_catch_error_return, -28, 0, 16, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10008372u, bb_qmix_export_set_frequency_catch_memory_return, -32, 0, 16, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10008496u, bb_qmix_export_set_listener_orientation_catch_error_return, -24, 0, 16, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10008485u, bb_qmix_export_set_listener_orientation_catch_memory_return, -28, 0, 16, "popl %edi;popl %esi;popl %ebx;") \
  X(0x100085a2u, bb_qmix_export_set_listener_position_catch_error_return, -24, 0, 12, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10008591u, bb_qmix_export_set_listener_position_catch_memory_return, -28, 0, 12, "popl %edi;popl %esi;popl %ebx;") \
  X(0x100086b2u, bb_qmix_export_set_listener_velocity_catch_error_return, -24, 0, 12, "popl %edi;popl %esi;popl %ebx;") \
  X(0x100086a1u, bb_qmix_export_set_listener_velocity_catch_memory_return, -28, 0, 12, "popl %edi;popl %esi;popl %ebx;") \
  X(0x100087a5u, bb_qmix_export_set_options_catch_memory_return, -24, 0, 12, "popl %edi;popl %esi;popl %ebx;") \
  X(0x100088aau, bb_qmix_export_set_pan_catch_error_return, -24, 0, 16, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10008899u, bb_qmix_export_set_pan_catch_memory_return, -28, 0, 16, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10008a03u, bb_qmix_export_set_pan_rate_catch_error_return, -28, 0, 16, "popl %edi;popl %esi;popl %ebx;") \
  X(0x100089f2u, bb_qmix_export_set_pan_rate_catch_memory_return, -32, 0, 16, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10008b63u, bb_qmix_export_set_position_catch_error_return, -28, 0, 16, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10008b52u, bb_qmix_export_set_position_catch_memory_return, -32, 0, 16, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10008c62u, bb_qmix_export_set_remix_buffer_size_catch_error_return, -24, 0, 8, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10008c51u, bb_qmix_export_set_remix_buffer_size_catch_memory_return, -28, 0, 8, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10008d72u, bb_qmix_export_set_room_size_catch_error_return, -24, 0, 12, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10008d61u, bb_qmix_export_set_room_size_catch_memory_return, -28, 0, 12, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10008ee3u, bb_qmix_export_set_source_cone_catch_error_return, -28, 0, 24, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10008ed2u, bb_qmix_export_set_source_cone_catch_memory_return, -32, 0, 24, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10009043u, bb_qmix_export_set_source_position_catch_error_return, -28, 0, 16, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10009032u, bb_qmix_export_set_source_position_catch_memory_return, -32, 0, 16, "popl %edi;popl %esi;popl %ebx;") \
  X(0x100091a3u, bb_qmix_export_set_source_velocity_catch_error_return, -28, 0, 16, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10009192u, bb_qmix_export_set_source_velocity_catch_memory_return, -32, 0, 16, "popl %edi;popl %esi;popl %ebx;") \
  X(0x100092bbu, bb_qmix_export_set_speaker_placement_catch_error_return, -24, 0, 8, "popl %edi;popl %esi;popl %ebx;") \
  X(0x100092aau, bb_qmix_export_set_speaker_placement_catch_memory_return, -28, 0, 8, "popl %edi;popl %esi;popl %ebx;") \
  X(0x100093c2u, bb_qmix_export_set_speed_of_sound_catch_error_return, -24, 0, 12, "popl %edi;popl %esi;popl %ebx;") \
  X(0x100093b1u, bb_qmix_export_set_speed_of_sound_catch_memory_return, -28, 0, 12, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10009523u, bb_qmix_export_set_volume_catch_error_return, -28, 0, 16, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10009512u, bb_qmix_export_set_volume_catch_memory_return, -32, 0, 16, "popl %edi;popl %esi;popl %ebx;") \
  X(0x1000960du, bb_qmix_export_start_profile_catch_memory_return, -24, 0, 4, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10009759u, bb_qmix_export_stop_channel_catch_error_return, -28, 0, 12, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10009748u, bb_qmix_export_stop_channel_catch_memory_return, -32, 0, 12, "popl %edi;popl %esi;popl %ebx;") \
  X(0x10009841u, bb_qmix_export_stop_profile_catch_memory_return, -24, 0, 12, "popl %edi;popl %esi;popl %ebx;")

#define DECLARE_CATCH(address, name, dst, src, continuation, report) uint32_t name##_entry(void);
BB_QMIX_API_CATCHES(DECLARE_CATCH)
#undef DECLARE_CATCH
#define DECLARE_RETURN(address, name, result, stage, cleanup, pops) uint32_t name##_entry(void);
BB_QMIX_API_RETURNS(DECLARE_RETURN)
#undef DECLARE_RETURN
#endif
