/* SPDX-License-Identifier: GPL-3.0-only */
#include "qmixer_vtables.h"
#include "interop.h"
#include <stdatomic.h>
#include <stddef.h>

/* Class method tables are owned C data. Their slots are callable C entry
 * points, including for callers outside the mixer dispatcher. */
#define BB_QMIX_SLOT(identity, entry) ((bb_qmix_virtual_method)entry)
#define BB_QMIX_TABLE(name, count, ...) \
  bb_qmix_virtual_method const name[count] = {__VA_ARGS__};
#include "qmixer_method_tables.h"
#undef BB_QMIX_TABLE
#undef BB_QMIX_SLOT

struct Method {
  uintptr_t legacy;
  bb_qmix_virtual_method entry;
};
static const struct Method methods[] = {
    {0x10001930u, (bb_qmix_virtual_method)bb_qmix_decoder_base_delete_entry},
    {0x10001a00u, (bb_qmix_virtual_method)bb_qmix_pcm_delete_entry},
    {0x10001a70u, (bb_qmix_virtual_method)bb_qmix_pcm_remaining_entry},
    {0x10001a90u, (bb_qmix_virtual_method)bb_qmix_pcm_progress_entry},
    {0x10001ae0u, (bb_qmix_virtual_method)bb_qmix_pcm_mono8_entry},
    {0x10001b40u, (bb_qmix_virtual_method)bb_qmix_pcm_mono16_entry},
    {0x10001ba0u, (bb_qmix_virtual_method)bb_qmix_pcm_stereo8_entry},
    {0x10001c10u, (bb_qmix_virtual_method)bb_qmix_pcm_stereo16_entry},
    {0x10001c80u, (bb_qmix_virtual_method)bb_qmix_pcm_reset_entry},
    {0x10001c90u, (bb_qmix_virtual_method)bb_qmix_pcm_advance_entry},
    {0x10001f10u, (bb_qmix_virtual_method)bb_qmix_codec_delete_entry},
    {0x10001fe0u, (bb_qmix_virtual_method)bb_qmix_codec_remaining_entry},
    {0x10002010u, (bb_qmix_virtual_method)bb_qmix_codec_progress_entry},
    {0x10002060u, (bb_qmix_virtual_method)bb_qmix_codec_mono8_entry},
    {0x100020d0u, (bb_qmix_virtual_method)bb_qmix_codec_mono16_entry},
    {0x10002140u, (bb_qmix_virtual_method)bb_qmix_codec_stereo8_entry},
    {0x100021c0u, (bb_qmix_virtual_method)bb_qmix_codec_stereo16_entry},
    {0x100022d0u, (bb_qmix_virtual_method)bb_qmix_codec_reset_entry},
    {0x100022e0u, (bb_qmix_virtual_method)bb_qmix_codec_advance_entry},
    {0x100023c0u, (bb_qmix_virtual_method)bb_qmix_callback_progress_entry},
    {0x100023d0u, (bb_qmix_virtual_method)bb_qmix_callback_delete_entry},
    {0x10002440u, (bb_qmix_virtual_method)bb_qmix_callback_remaining_entry},
    {0x10002470u, (bb_qmix_virtual_method)bb_qmix_callback_mono8_entry},
    {0x100024d0u, (bb_qmix_virtual_method)bb_qmix_callback_mono16_entry},
    {0x10002530u, (bb_qmix_virtual_method)bb_qmix_callback_stereo8_entry},
    {0x100025a0u, (bb_qmix_virtual_method)bb_qmix_callback_stereo16_entry},
    {0x10002610u, (bb_qmix_virtual_method)bb_qmix_callback_reset_entry},
    {0x10002620u, (bb_qmix_virtual_method)bb_qmix_callback_advance_entry},
    {0x100028a0u, (bb_qmix_virtual_method)bb_qmix_session_delete_entry},
    {0x10002950u, (bb_qmix_virtual_method)bb_qmix_session_initialize_entry},
    {0x10002a60u, (bb_qmix_virtual_method)bb_qmix_session_backend_select_entry},
    {0x10002b60u, (bb_qmix_virtual_method)bb_qmix_session_options_entry},
    {0x10002c50u, (bb_qmix_virtual_method)bb_qmix_listener_position_entry},
    {0x10002c80u, (bb_qmix_virtual_method)bb_qmix_listener_orientation_entry},
    {0x10002dc0u, (bb_qmix_virtual_method)bb_qmix_listener_velocity_entry},
    {0x10002df0u, (bb_qmix_virtual_method)bb_qmix_listener_default_entry},
    {0x10002e00u, (bb_qmix_virtual_method)bb_qmix_listener_speed_entry},
    {0x10002e20u, (bb_qmix_virtual_method)bb_qmix_listener_gain_entry},
    {0x10002e70u, (bb_qmix_virtual_method)bb_qmix_session_default_entry},
    {0x10002e80u, (bb_qmix_virtual_method)bb_qmix_listener_position_get_entry},
    {0x10002eb0u, (bb_qmix_virtual_method)bb_qmix_listener_orientation_get_entry},
    {0x10002ef0u, (bb_qmix_virtual_method)bb_qmix_listener_velocity_get_entry},
    {0x10002f20u, (bb_qmix_virtual_method)bb_qmix_listener_speed_get_entry},
    {0x10002f30u, (bb_qmix_virtual_method)bb_qmix_listener_gain_get_entry},
    {0x10002fb0u, (bb_qmix_virtual_method)bb_qmix_timing_begin_entry},
    {0x10002ff0u, (bb_qmix_virtual_method)bb_qmix_timing_end_entry},
    {0x10003190u, (bb_qmix_virtual_method)bb_qmix_current_voice_entry},
    {0x100031a0u, (bb_qmix_virtual_method)bb_qmix_channel_delete_entry},
    {0x100033b0u, (bb_qmix_virtual_method)bb_qmix_channel_enable_entry},
    {0x100033d0u, (bb_qmix_virtual_method)bb_qmix_pan_rate_entry},
    {0x10003410u, (bb_qmix_virtual_method)bb_qmix_channel_volume_entry},
    {0x10003460u, (bb_qmix_virtual_method)bb_qmix_channel_frequency_entry},
    {0x100034b0u, (bb_qmix_virtual_method)bb_qmix_channel_position_entry},
    {0x100034d0u, (bb_qmix_virtual_method)bb_qmix_source_position_entry},
    {0x10003500u, (bb_qmix_virtual_method)bb_qmix_source_velocity_entry},
    {0x10003530u, (bb_qmix_virtual_method)bb_qmix_cone_set_entry},
    {0x100035f0u, (bb_qmix_virtual_method)bb_qmix_distance_set_entry},
    {0x10003770u, (bb_qmix_virtual_method)bb_qmix_voice_queue_entry},
    {0x10003810u, (bb_qmix_virtual_method)bb_qmix_voice_start_entry},
    {0x10003870u, (bb_qmix_virtual_method)bb_qmix_mark_stop_entry},
    {0x10003890u, (bb_qmix_virtual_method)bb_qmix_channel_stop_entry},
    {0x10003900u, (bb_qmix_virtual_method)bb_qmix_channel_drain_entry},
    {0x10003ed0u, (bb_qmix_virtual_method)bb_qmix_channel_progress_entry},
    {0x10003f20u, (bb_qmix_virtual_method)bb_qmix_source_position_get_entry},
    {0x10003f40u, (bb_qmix_virtual_method)bb_qmix_source_velocity_get_entry},
    {0x10003f60u, (bb_qmix_virtual_method)bb_qmix_cone_get_entry},
    {0x10003fb0u, (bb_qmix_virtual_method)bb_qmix_distance_get_entry},
    {0x10003fd0u, (bb_qmix_virtual_method)bb_qmix_queued_duration_entry},
    {0x10004660u, (bb_qmix_virtual_method)bb_qmix_directsound_get_entry},
    {0x10004670u, (bb_qmix_virtual_method)bb_qmix_directsound_delete_entry},
    {0x10004760u, (bb_qmix_virtual_method)bb_qmix_directsound_configure_entry},
    {0x100048d0u, (bb_qmix_virtual_method)bb_qmix_directsound_initialize_entry},
    {0x10004bd0u, (bb_qmix_virtual_method)bb_qmix_directsound_activate_entry},
    {0x10004c20u, (bb_qmix_virtual_method)bb_qmix_directsound_profile_end_entry},
    {0x10004c60u, (bb_qmix_virtual_method)bb_qmix_directsound_pump_entry},
    {0x10005110u, (bb_qmix_virtual_method)bb_qmix_session_base_delete_entry},
    {0x10005410u, (bb_qmix_virtual_method)bb_qmix_session_validate_format_entry},
    {0x10005450u, (bb_qmix_virtual_method)bb_qmix_session_configure_format_entry},
    {0x100054e0u, (bb_qmix_virtual_method)bb_qmix_session_allocate_channels_entry},
    {0x10005530u, (bb_qmix_virtual_method)bb_qmix_session_default_backend_entry},
    {0x10005540u, (bb_qmix_virtual_method)bb_qmix_session_default_orientation_entry},
    {0x10005550u, (bb_qmix_virtual_method)bb_qmix_session_option_bits_entry},
    {0x10005570u, (bb_qmix_virtual_method)bb_qmix_session_default_position_entry},
    {0x10005580u, (bb_qmix_virtual_method)bb_qmix_session_default_velocity_entry},
    {0x100055f0u, (bb_qmix_virtual_method)bb_qmix_session_default_speed_entry},
    {0x10005600u, (bb_qmix_virtual_method)bb_qmix_session_default_rate_entry},
    {0x10005610u, (bb_qmix_virtual_method)bb_qmix_session_default_profile_entry},
    {0x10005660u, (bb_qmix_virtual_method)bb_qmix_session_default_profile_end_entry},
    {0x10005670u, (bb_qmix_virtual_method)bb_qmix_session_activate_entry},
    {0x10005710u, (bb_qmix_virtual_method)bb_qmix_session_default_orientation_get_entry},
    {0x10005720u, (bb_qmix_virtual_method)bb_qmix_session_default_position_get_entry},
    {0x10005730u, (bb_qmix_virtual_method)bb_qmix_session_default_velocity_get_entry},
    {0x10005740u, (bb_qmix_virtual_method)bb_qmix_session_default_speed_get_entry},
    {0x10005750u, (bb_qmix_virtual_method)bb_qmix_session_default_gain_entry},
    {0x10005a40u, (bb_qmix_virtual_method)bb_qmix_default_true_entry},
    {0x10005a50u, (bb_qmix_virtual_method)bb_qmix_default_false_entry},
    {0x10005a60u, (bb_qmix_virtual_method)bb_qmix_default_zero_entry},
    {0x10005a70u, (bb_qmix_virtual_method)bb_qmix_deleting_destructor_entry},
    {0x10005aa0u, (bb_qmix_virtual_method)bb_qmix_activate_entry},
    {0x10005ab0u, (bb_qmix_virtual_method)bb_qmix_enabled_set_entry},
    {0x10005ac0u, (bb_qmix_virtual_method)bb_qmix_default_position_entry},
    {0x10005ad0u, (bb_qmix_virtual_method)bb_qmix_volume_store_entry},
    {0x10005ae0u, (bb_qmix_virtual_method)bb_qmix_position_store_entry},
    {0x10005b10u, (bb_qmix_virtual_method)bb_qmix_pan_rate_store_entry},
    {0x10005b20u, (bb_qmix_virtual_method)bb_qmix_frequency_store_entry},
    {0x10005b70u, (bb_qmix_virtual_method)bb_qmix_default_reset_entry},
    {0x10005b80u, (bb_qmix_virtual_method)bb_qmix_default_set_entry},
    {0x10005b90u, (bb_qmix_virtual_method)bb_qmix_default_position_get_entry},
    {0x10005bb0u, (bb_qmix_virtual_method)bb_qmix_pause_entry},
    {0x10005bc0u, (bb_qmix_virtual_method)bb_qmix_restart_entry},
    {0x10005bd0u, (bb_qmix_virtual_method)bb_qmix_default_pump_entry},
    {0x10005be0u, (bb_qmix_virtual_method)bb_qmix_deactivate_entry},
    {0x10009dc0u, (bb_qmix_virtual_method)bb_qmix_waveout_delete_entry},
    {0x10009fe0u, (bb_qmix_virtual_method)bb_qmix_waveout_configure_entry},
    {0x1000a0c0u, (bb_qmix_virtual_method)bb_qmix_waveout_initialize_entry},
    {0x1000a4a0u, (bb_qmix_virtual_method)bb_qmix_waveout_activate_entry},
    {0x1000a740u, (bb_qmix_virtual_method)bb_qmix_waveout_profile_end_entry},
    {0x1000a780u, (bb_qmix_virtual_method)bb_qmix_waveout_pump_entry},
    {0x1000ae70u, (bb_qmix_virtual_method)bb_qmix_device_base_delete_entry},
    {0x1000ae90u, (bb_qmix_virtual_method)bb_qmix_device_invalidate_entry},
    {0x1000aea0u, (bb_qmix_virtual_method)bb_qmix_device_delete_entry},
    {0x1000af30u, (bb_qmix_virtual_method)bb_qmix_device_mode_entry},
    {0x1000afa0u, (bb_qmix_virtual_method)bb_qmix_device_volume_entry},
    {0x1000afb0u, (bb_qmix_virtual_method)bb_qmix_device_position_entry},
    {0x1000afd0u, (bb_qmix_virtual_method)bb_qmix_device_rate_entry},
    {0x1000aff0u, (bb_qmix_virtual_method)bb_qmix_device_distance_entry},
    {0x1000b000u, (bb_qmix_virtual_method)bb_qmix_device_refresh_entry},
    {0x1000b010u, (bb_qmix_virtual_method)bb_qmix_device_transition_entry},
    {0x1000b0f0u, (bb_qmix_virtual_method)bb_qmix_device_mono16_entry},
    {0x1000b150u, (bb_qmix_virtual_method)bb_qmix_device_dual16_entry},
    {0x1000b1d0u, (bb_qmix_virtual_method)bb_qmix_device_stereo16_entry},
    {0x1000b230u, (bb_qmix_virtual_method)bb_qmix_device_mono8_entry},
    {0x1000b290u, (bb_qmix_virtual_method)bb_qmix_device_dual8_entry},
    {0x1000b310u, (bb_qmix_virtual_method)bb_qmix_device_stereo8_entry},
    {0x1000b430u, (bb_qmix_virtual_method)bb_qmix_mixer_default_entry},
    {0x1000b440u, (bb_qmix_virtual_method)bb_qmix_mixer_noop_entry},
    {0x1000b450u, (bb_qmix_virtual_method)bb_qmix_mixer_base_delete_entry},
    {0x1000b470u, (bb_qmix_virtual_method)bb_qmix_mixer_delete_entry},
    {0x1000b540u, (bb_qmix_virtual_method)bb_qmix_mixer_configure_entry},
    {0x1000b620u, (bb_qmix_virtual_method)bb_qmix_mixer_flags_entry},
    {0x1000b690u, (bb_qmix_virtual_method)bb_qmix_mixer_device_create_entry},
    {0x1000b720u, (bb_qmix_virtual_method)bb_qmix_mixer_output_entry},
    {0x1000b810u, (bb_qmix_virtual_method)bb_qmix_mixer_silent_entry},
    {0x1000b860u, (bb_qmix_virtual_method)bb_qmix_mixer_clear_entry},
    {0x1000bab0u, (bb_qmix_virtual_method)bb_qmix_ramp_set_entry},
    {0x1000bb10u, (bb_qmix_virtual_method)bb_qmix_ramp_mono8_entry},
    {0x1000bbf0u, (bb_qmix_virtual_method)bb_qmix_ramp_mono16_entry},
    {0x1000bd60u, (bb_qmix_virtual_method)bb_qmix_accumulator_base_delete_entry},
    {0x1000bd80u, (bb_qmix_virtual_method)bb_qmix_float_accumulator_delete_entry},
    {0x1000be10u, (bb_qmix_virtual_method)bb_qmix_accumulator_feed_entry},
    {0x1000be20u, (bb_qmix_virtual_method)bb_qmix_float_ramp_create_entry},
    {0x1000c070u, (bb_qmix_virtual_method)bb_qmix_accumulator_merge_entry},
    {0x1000c110u, (bb_qmix_virtual_method)bb_qmix_accumulator_stereo_output_entry},
    {0x1000c150u, (bb_qmix_virtual_method)bb_qmix_accumulator_flush_entry},
    {0x1000c1a0u, (bb_qmix_virtual_method)bb_qmix_accumulator_silent_entry},
    {0x1000c1d0u, (bb_qmix_virtual_method)bb_qmix_accumulator_clear_entry},
    {0x1000c650u, (bb_qmix_virtual_method)bb_qmix_float_accumulator_create_entry},
    {0x1000c700u, (bb_qmix_virtual_method)bb_qmix_integer_accumulator_create_entry},
    {0x1000c7a0u, (bb_qmix_virtual_method)bb_qmix_integer_ramp_set_entry},
    {0x1000c7f0u, (bb_qmix_virtual_method)bb_qmix_integer_ramp_mono8_entry},
    {0x1000c8b0u, (bb_qmix_virtual_method)bb_qmix_integer_ramp_mono16_entry},
    {0x1000c9f0u, (bb_qmix_virtual_method)bb_qmix_integer_accumulator_delete_entry},
    {0x1000ca80u, (bb_qmix_virtual_method)bb_qmix_integer_ramp_create_entry},
    {0x1000caf0u, (bb_qmix_virtual_method)bb_qmix_integer_accumulator_feed_entry},
    {0x1000cca0u, (bb_qmix_virtual_method)bb_qmix_integer_accumulator_merge_entry},
    {0x1000cd40u, (bb_qmix_virtual_method)bb_qmix_integer_accumulator_stereo_output_entry},
    {0x1000cd90u, (bb_qmix_virtual_method)bb_qmix_integer_accumulator_flush_entry},
    {0x1000cde0u, (bb_qmix_virtual_method)bb_qmix_integer_accumulator_silent_entry},
    {0x1000ce00u, (bb_qmix_virtual_method)bb_qmix_integer_accumulator_clear_entry},
    {0x1000d1a0u, (bb_qmix_virtual_method)bb_qmix_short_ramp_set_entry},
    {0x1000d200u, (bb_qmix_virtual_method)bb_qmix_short_ramp_mono8_entry},
    {0x1000d2d0u, (bb_qmix_virtual_method)bb_qmix_short_ramp_mono16_entry},
    {0x1000d420u, (bb_qmix_virtual_method)bb_qmix_short_accumulator_delete_entry},
    {0x1000d4b0u, (bb_qmix_virtual_method)bb_qmix_short_accumulator_feed_entry},
    {0x1000d4c0u, (bb_qmix_virtual_method)bb_qmix_short_ramp_create_entry},
    {0x1000d6d0u, (bb_qmix_virtual_method)bb_qmix_short_accumulator_merge_entry},
    {0x1000d780u, (bb_qmix_virtual_method)bb_qmix_short_accumulator_stereo_output_entry},
    {0x1000d7c0u, (bb_qmix_virtual_method)bb_qmix_short_accumulator_flush_entry},
    {0x1000d820u, (bb_qmix_virtual_method)bb_qmix_short_accumulator_silent_entry},
    {0x1000d840u, (bb_qmix_virtual_method)bb_qmix_short_accumulator_clear_entry},
    {0x1000dea0u, (bb_qmix_virtual_method)bb_qmix_short_accumulator_create_entry},
    {0x1000df50u, (bb_qmix_virtual_method)bb_qmix_filter_create_entry},
    {0x1000e080u, (bb_qmix_virtual_method)bb_qmix_filter_delete_entry},
    {0x1000e0f0u, (bb_qmix_virtual_method)bb_qmix_filter_intermediate_delete_entry},
    {0x1000e1e0u, (bb_qmix_virtual_method)bb_qmix_filter_base_delete_entry},
    {0x1000e200u, (bb_qmix_virtual_method)bb_qmix_filter_default_entry},
    {0x1000e210u, (bb_qmix_virtual_method)bb_qmix_filter_process_entry},
    {0x1000e2a0u, (bb_qmix_virtual_method)bb_qmix_filter_noop_entry},
    {0x1000e2b0u, (bb_qmix_virtual_method)bb_qmix_filter_attach_entry},
    {0x1000e2d0u, (bb_qmix_virtual_method)bb_qmix_filter_detach_entry},
    {0x1000e2f0u, (bb_qmix_virtual_method)bb_qmix_filter_silent_entry},
    {0x1000e330u, (bb_qmix_virtual_method)bb_qmix_filter_clear_entry},
    {0x1000e3d0u, (bb_qmix_virtual_method)bb_qmix_float_biquad_create_entry},
    {0x1000e520u, (bb_qmix_virtual_method)bb_qmix_float_biquad_delete_entry},
    {0x1000e5c0u, (bb_qmix_virtual_method)bb_qmix_float_biquad_process_entry},
    {0x1000e660u, (bb_qmix_virtual_method)bb_qmix_float_biquad_gain_attach_entry},
    {0x1000e680u, (bb_qmix_virtual_method)bb_qmix_float_biquad_gain_detach_entry},
    {0x1000e6a0u, (bb_qmix_virtual_method)bb_qmix_float_biquad_silent_entry},
    {0x1000e6e0u, (bb_qmix_virtual_method)bb_qmix_float_biquad_clear_entry},
    {0x1000e760u, (bb_qmix_virtual_method)bb_qmix_float_fir_create_entry},
    {0x1000e8c0u, (bb_qmix_virtual_method)bb_qmix_float_fir_delete_entry},
    {0x1000e960u, (bb_qmix_virtual_method)bb_qmix_float_fir_process_entry},
    {0x1000ea80u, (bb_qmix_virtual_method)bb_qmix_float_fir_silent_entry},
    {0x1000eac0u, (bb_qmix_virtual_method)bb_qmix_float_fir_clear_entry},
    {0x1000eaf0u, (bb_qmix_virtual_method)bb_qmix_float_delay_create_entry},
    {0x1000ec60u, (bb_qmix_virtual_method)bb_qmix_float_delay_delete_entry},
    {0x1000ecd0u, (bb_qmix_virtual_method)bb_qmix_delay_base_delete_entry},
    {0x1000edc0u, (bb_qmix_virtual_method)bb_qmix_float_delay_process_entry},
    {0x1000f0e0u, (bb_qmix_virtual_method)bb_qmix_float_delay_silent_entry},
    {0x1000f120u, (bb_qmix_virtual_method)bb_qmix_float_delay_clear_entry},
    {0x1000f140u, (bb_qmix_virtual_method)bb_qmix_integer_iir_create_entry},
    {0x1000f310u, (bb_qmix_virtual_method)bb_qmix_integer_iir_delete_entry},
    {0x1000f3b0u, (bb_qmix_virtual_method)bb_qmix_integer_iir_process_entry},
    {0x1000f430u, (bb_qmix_virtual_method)bb_qmix_integer_iir_silent_entry},
    {0x1000f470u, (bb_qmix_virtual_method)bb_qmix_integer_iir_clear_entry},
    {0x1000f530u, (bb_qmix_virtual_method)bb_qmix_integer_biquad_create_entry},
    {0x1000f730u, (bb_qmix_virtual_method)bb_qmix_integer_biquad_delete_entry},
    {0x1000f7d0u, (bb_qmix_virtual_method)bb_qmix_integer_biquad_process_entry},
    {0x1000f850u, (bb_qmix_virtual_method)bb_qmix_integer_biquad_silent_entry},
    {0x1000f890u, (bb_qmix_virtual_method)bb_qmix_integer_biquad_clear_entry},
    {0x1000f920u, (bb_qmix_virtual_method)bb_qmix_integer_fir_create_entry},
    {0x1000fa90u, (bb_qmix_virtual_method)bb_qmix_integer_fir_delete_entry},
    {0x1000fb30u, (bb_qmix_virtual_method)bb_qmix_integer_fir_process_entry},
    {0x1000fbd0u, (bb_qmix_virtual_method)bb_qmix_integer_fir_silent_entry},
    {0x1000fc10u, (bb_qmix_virtual_method)bb_qmix_integer_fir_clear_entry},
    {0x1000fc50u, (bb_qmix_virtual_method)bb_qmix_integer_delay_create_entry},
    {0x1000fdd0u, (bb_qmix_virtual_method)bb_qmix_integer_delay_delete_entry},
    {0x1000fe70u, (bb_qmix_virtual_method)bb_qmix_integer_delay_process_entry},
    {0x1000ffe0u, (bb_qmix_virtual_method)bb_qmix_integer_delay_silent_entry},
    {0x10010020u, (bb_qmix_virtual_method)bb_qmix_integer_delay_clear_entry},
    {0x10010060u, (bb_qmix_virtual_method)bb_qmix_short_iir_create_entry},
    {0x10010330u, (bb_qmix_virtual_method)bb_qmix_short_iir_delete_entry},
    {0x100103d0u, (bb_qmix_virtual_method)bb_qmix_short_iir_process_entry},
    {0x100104b0u, (bb_qmix_virtual_method)bb_qmix_short_iir_silent_entry},
    {0x100104f0u, (bb_qmix_virtual_method)bb_qmix_short_iir_clear_entry},
    {0x10010670u, (bb_qmix_virtual_method)bb_qmix_short_biquad_create_entry},
    {0x100109a0u, (bb_qmix_virtual_method)bb_qmix_short_biquad_delete_entry},
    {0x10010a40u, (bb_qmix_virtual_method)bb_qmix_short_biquad_process_entry},
    {0x10010c80u, (bb_qmix_virtual_method)bb_qmix_short_biquad_silent_entry},
    {0x10010ca0u, (bb_qmix_virtual_method)bb_qmix_short_biquad_clear_entry},
    {0x10010cc0u, (bb_qmix_virtual_method)bb_qmix_short_fir_create_entry},
    {0x10010f10u, (bb_qmix_virtual_method)bb_qmix_short_fir_delete_entry},
    {0x10010fb0u, (bb_qmix_virtual_method)bb_qmix_short_fir_process_entry},
    {0x10011130u, (bb_qmix_virtual_method)bb_qmix_short_fir_silent_entry},
    {0x10011170u, (bb_qmix_virtual_method)bb_qmix_short_fir_clear_entry},
    {0x100111a0u, (bb_qmix_virtual_method)bb_qmix_tap_delay_factory_entry},
    {0x10011320u, (bb_qmix_virtual_method)bb_qmix_tap_delay_delete_entry},
    {0x100113c0u, (bb_qmix_virtual_method)bb_qmix_tap_delay_process_entry},
    {0x10011620u, (bb_qmix_virtual_method)bb_qmix_tap_delay_silent_entry},
    {0x10011650u, (bb_qmix_virtual_method)bb_qmix_tap_delay_clear_entry},
    {0x100116a0u, (bb_qmix_virtual_method)bb_qmix_exception_object_delete_entry},
    {0x10011c40u, (bb_qmix_virtual_method)bb_qmix_pure_virtual_failure_entry},
};

/* Keep virtual calls on the existing C worker path, with its logical argument
 * and floating-return handling. Unknown host callbacks retain their ABI path.
 * A bounded hash lookup avoids a scan of every method for each audio block. */
#define METHOD_CAPACITY 1024u
static struct Method method_map[METHOD_CAPACITY];
static atomic_uint method_map_state;
_Static_assert(sizeof(methods) / sizeof(methods[0]) * 2 < METHOD_CAPACITY,
               "virtual method map has spare capacity");
static unsigned method_hash(uintptr_t entry) {
  return (unsigned)(((entry >> 4) * UINT32_C(2654435761)) &
                     (METHOD_CAPACITY - 1u));
}
static void initialize_method_map(void) {
  unsigned expected = 0;
  if (atomic_compare_exchange_strong_explicit(
          &method_map_state, &expected, 1, memory_order_acq_rel,
          memory_order_acquire)) {
    for (size_t n = 0; n < sizeof(methods) / sizeof(methods[0]); ++n) {
      unsigned slot = method_hash((uintptr_t)methods[n].entry);
      while (method_map[slot].entry &&
             method_map[slot].entry != methods[n].entry)
        slot = (slot + 1u) & (METHOD_CAPACITY - 1u);
      method_map[slot] = methods[n];
    }
    atomic_store_explicit(&method_map_state, 2, memory_order_release);
  } else {
    while (atomic_load_explicit(&method_map_state, memory_order_acquire) != 2) {
    }
  }
}
uintptr_t bb_qmix_virtual_method_id(uintptr_t entry) {
  if (entry >= 0x10000000u && entry < 0x10025000u)
    return entry;
  if (atomic_load_explicit(&method_map_state, memory_order_acquire) != 2)
    initialize_method_map();
  unsigned slot = method_hash(entry);
  while (method_map[slot].entry) {
    if ((uintptr_t)method_map[slot].entry == entry)
      return method_map[slot].legacy;
    slot = (slot + 1u) & (METHOD_CAPACITY - 1u);
  }
  return entry;
}
void bb_qmix_rebind_static_vtables(void) {
  /* A Windows bridge can attach after the original DLL's constructors ran.
   * The native launcher installs C constructors before DLL initialization. */
  if (BB_U32(0x10020bf4u) == 0x1001a470u)
    BB_U32(0x10020bf4u) = (uintptr_t)bb_qmix_vtable_float_factory;
  if (BB_U32(0x10020bf8u) == 0x1001a4c0u)
    BB_U32(0x10020bf8u) = (uintptr_t)bb_qmix_vtable_integer_factory;
  if (BB_U32(0x10020bfcu) == 0x1001a510u)
    BB_U32(0x10020bfcu) = (uintptr_t)bb_qmix_vtable_short_factory;
  static const uintptr_t descriptors[] = {
      0x1001e040u, 0x1001e138u, 0x1001f438u};
  for (size_t n = 0; n < sizeof(descriptors) / sizeof(descriptors[0]); ++n)
    if (BB_U32(descriptors[n]) == 0x1001a734u)
      BB_U32(descriptors[n]) = (uintptr_t)bb_qmix_vtable_exception_object;
}
