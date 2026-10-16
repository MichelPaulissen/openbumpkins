/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_MIXER_EXPORTS_H
#define BB_MIXER_EXPORTS_H
#define BB_MIXER_EXPORTS(X)                                                    \
  X("QSWaveMixActivate", 2, 0x10006300u, bb_qmix_export_activate_entry)        \
  X("QSWaveMixCloseChannel", 3, 0x10006410u,                                   \
    bb_qmix_export_close_channel_entry)                                        \
  X("QSWaveMixCloseSession", 4, 0x10006510u,                                   \
    bb_qmix_export_close_session_entry)                                        \
  X("QSWaveMixConfigureInit", 5, 0x100065b0u,                                  \
    bb_qmix_export_configure_init_entry)                                       \
  X("QSWaveMixEnable", 6, 0x10006620u, bb_qmix_export_enable_entry)            \
  X("QSWaveMixEnableChannel", 7, 0x10006640u,                                  \
    bb_qmix_export_enable_channel_entry)                                       \
  X("QSWaveMixFlushChannel", 8, 0x100067a0u,                                   \
    bb_qmix_export_flush_channel_entry)                                        \
  X("QSWaveMixFreeWave", 9, 0x100068f0u, bb_qmix_export_free_wave_entry)       \
  X("QSWaveMixGetDirectSound", 10, 0x10006a40u,                                \
    bb_qmix_export_get_direct_sound_entry)                                     \
  X("QSWaveMixGetDistanceMapping", 11, 0x10006b20u,                            \
    bb_qmix_export_get_distance_mapping_entry)                                 \
  X("QSWaveMixGetErrorText", 12, 0x10006c10u,                                  \
    bb_qmix_export_get_error_text_entry)                                       \
  X("QSWaveMixGetFrequency", 13, 0x10006c60u,                                  \
    bb_qmix_export_get_frequency_entry)                                        \
  X("QSWaveMixGetInfo", 14, 0x10006d60u, bb_qmix_export_get_info_entry)        \
  X("QSWaveMixGetLastError", 15, 0x10006da0u,                                  \
    bb_qmix_export_get_last_error_entry)                                       \
  X("QSWaveMixGetListenerOrientation", 16, 0x10006db0u,                        \
    bb_qmix_export_get_listener_orientation_entry)                             \
  X("QSWaveMixGetListenerPosition", 17, 0x10006e90u,                           \
    bb_qmix_export_get_listener_position_entry)                                \
  X("QSWaveMixGetListenerVelocity", 18, 0x10006f70u,                           \
    bb_qmix_export_get_listener_velocity_entry)                                \
  X("QSWaveMixGetOptions", 19, 0x10007050u, bb_qmix_export_get_options_entry)  \
  X("QSWaveMixGetPan", 20, 0x10007140u, bb_qmix_export_get_pan_entry)          \
  X("QSWaveMixGetPanRate", 21, 0x100071e0u, bb_qmix_export_get_pan_rate_entry) \
  X("QSWaveMixGetPlayPosition", 22, 0x10007890u,                               \
    bb_qmix_export_get_play_position_entry)                                    \
  X("QSWaveMixGetPosition", 23, 0x100072e0u,                                   \
    bb_qmix_export_get_position_entry)                                         \
  X("QSWaveMixGetRoomSize", 24, 0x10007400u,                                   \
    bb_qmix_export_get_room_size_entry)                                        \
  X("QSWaveMixGetSourceCone", 25, 0x100074e0u,                                 \
    bb_qmix_export_get_source_cone_entry)                                      \
  X("QSWaveMixGetSourcePosition", 26, 0x100075d0u,                             \
    bb_qmix_export_get_source_position_entry)                                  \
  X("QSWaveMixGetSourceVelocity", 27, 0x100076c0u,                             \
    bb_qmix_export_get_source_velocity_entry)                                  \
  X("QSWaveMixGetSpeedOfSound", 28, 0x100077b0u,                               \
    bb_qmix_export_get_speed_of_sound_entry)                                   \
  X("QSWaveMixGetVolume", 29, 0x100079a0u, bb_qmix_export_get_volume_entry)    \
  X("QSWaveMixInit", 30, 0x10007aa0u, bb_qmix_export_init_entry)               \
  X("QSWaveMixIsChannelDone", 31, 0x10007ad0u,                                 \
    bb_qmix_export_is_channel_done_entry)                                      \
  X("QSWaveMixOpenChannel", 32, 0x10007bc0u,                                   \
    bb_qmix_export_open_channel_entry)                                         \
  X("QSWaveMixOpenWave", 33, 0x10007cd0u, bb_qmix_export_open_wave_entry)      \
  X("QSWaveMixPauseChannel", 34, 0x10007cf0u,                                  \
    bb_qmix_export_pause_channel_entry)                                        \
  X("QSWaveMixPlay", 35, 0x10007e40u, bb_qmix_export_play_entry)               \
  X("QSWaveMixPump", 36, 0x10007f40u, bb_qmix_export_pump_entry)               \
  X("QSWaveMixRestartChannel", 37, 0x10007f90u,                                \
    bb_qmix_export_restart_channel_entry)                                      \
  X("QSWaveMixSetDistanceMapping", 38, 0x100080e0u,                            \
    bb_qmix_export_set_distance_mapping_entry)                                 \
  X("QSWaveMixSetFrequency", 39, 0x10008240u,                                  \
    bb_qmix_export_set_frequency_entry)                                        \
  X("QSWaveMixSetListenerOrientation", 40, 0x100083a0u,                        \
    bb_qmix_export_set_listener_orientation_entry)                             \
  X("QSWaveMixSetListenerPosition", 41, 0x100084b0u,                           \
    bb_qmix_export_set_listener_position_entry)                                \
  X("QSWaveMixSetListenerVelocity", 42, 0x100085c0u,                           \
    bb_qmix_export_set_listener_velocity_entry)                                \
  X("QSWaveMixSetOptions", 43, 0x100086d0u, bb_qmix_export_set_options_entry)  \
  X("QSWaveMixSetPan", 44, 0x100087c0u, bb_qmix_export_set_pan_entry)          \
  X("QSWaveMixSetPanRate", 45, 0x100088c0u, bb_qmix_export_set_pan_rate_entry) \
  X("QSWaveMixSetPosition", 46, 0x10008a20u,                                   \
    bb_qmix_export_set_position_entry)                                         \
  X("QSWaveMixSetRemixBufferSize", 1, 0x10008b80u,                             \
    bb_qmix_export_set_remix_buffer_size_entry)                                \
  X("QSWaveMixSetRoomSize", 47, 0x10008c80u,                                   \
    bb_qmix_export_set_room_size_entry)                                        \
  X("QSWaveMixSetSourceCone", 48, 0x10008d90u,                                 \
    bb_qmix_export_set_source_cone_entry)                                      \
  X("QSWaveMixSetSourcePosition", 49, 0x10008f00u,                             \
    bb_qmix_export_set_source_position_entry)                                  \
  X("QSWaveMixSetSourceVelocity", 50, 0x10009060u,                             \
    bb_qmix_export_set_source_velocity_entry)                                  \
  X("QSWaveMixSetSpeakerPlacement", 51, 0x100091c0u,                           \
    bb_qmix_export_set_speaker_placement_entry)                                \
  X("QSWaveMixSetSpeedOfSound", 52, 0x100092d0u,                               \
    bb_qmix_export_set_speed_of_sound_entry)                                   \
  X("QSWaveMixSetVolume", 53, 0x100093e0u, bb_qmix_export_set_volume_entry)    \
  X("QSWaveMixStartProfile", 54, 0x10009540u,                                  \
    bb_qmix_export_start_profile_entry)                                        \
  X("QSWaveMixStopChannel", 55, 0x10009620u,                                   \
    bb_qmix_export_stop_channel_entry)                                         \
  X("QSWaveMixStopProfile", 56, 0x10009770u, bb_qmix_export_stop_profile_entry)
#endif
