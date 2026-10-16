/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#ifndef BUMPKINS_INTEROP_H
#define BUMPKINS_INTEROP_H

#include <stdint.h>
#include "address.h"
#include "call_abi.h"
#include "pointer32.h"

/* Native calls resolve logical entry identities directly to compiled C.
 * The reference bridge retains its original-address call boundary. */
#ifdef BB_NATIVE_RUNTIME
uintptr_t bb_native_entry(uintptr_t address);
#define BB_CODE(address) bb_native_entry((uintptr_t)(address))
#define BB_CALL_EAX "call bb_native_dispatch_eax"
#define BB_JUMP_EAX "jmp bb_native_dispatch_eax"
#else
#define BB_CODE(address) ((uintptr_t)(address))
#define BB_CALL_EAX "call *%eax"
#define BB_JUMP_EAX "jmp *%eax"
#endif

/* Data words retain their original layout; the portable runtime translates
 * logical addresses rather than requiring the host mapping to match them. */
#define BB_POINTER(address) bb_pointer32_load((uintptr_t)(address))
#define BB_SET_POINTER(address, value)                                         \
  bb_pointer32_store((uintptr_t)(address), (value))

/* Localized panel fields, initialization, hover help and script-editor help. */
void BB_THISCALL bb_panel_localize_entry(void *);
void BB_THISCALL bb_panel_initialize_entry(void *);
void *BB_THISCALL bb_panel_construct_entry(void *);
void BB_THISCALL bb_panel_hover_text_entry(void *, int32_t,
                                                       int32_t, char *);
void BB_THISCALL bb_panel_hover_reset_entry(void *);
void BB_THISCALL bb_script_help_entry(void *, uint32_t, uint32_t);
void BB_THISCALL bb_script_help_next_entry(void *);

/* QMIXER concrete driver lifecycle, callbacks and wave handles. */
uint32_t bb_qmix_wave_handle_constructor_entry(void);
uint32_t bb_qmix_wave_handle_destructor_entry(void);
uint32_t bb_qmix_wave_open_entry(uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_wave_prepare_entry(uint32_t);
uint32_t bb_qmix_wave_write_entry(uint32_t);
uint32_t bb_qmix_wave_unprepare_entry(uint32_t);
uint32_t bb_qmix_wave_reset_entry(void);
uint32_t bb_qmix_wave_pause_entry(void);
uint32_t bb_qmix_wave_restart_entry(void);
uint32_t bb_qmix_wave_close_entry(void);
uint32_t bb_qmix_directsound_window_callback_entry(uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_directsound_timer_callback_entry(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_directsound_timer_start_entry(void);
uint32_t bb_qmix_directsound_timer_stop_entry(void);
uint32_t bb_qmix_directsound_constructor_entry(void);
uint32_t bb_qmix_directsound_constructor_cleanup_entry(void);
uint32_t bb_qmix_directsound_get_entry(void);
uint32_t bb_qmix_directsound_delete_entry(uint32_t);
uint32_t bb_qmix_directsound_destructor_entry(void);
uint32_t bb_qmix_directsound_destructor_cleanup_entry(void);
uint32_t bb_qmix_directsound_attach_entry(uint32_t);
uint32_t bb_qmix_directsound_window_select_entry(uint32_t, uint32_t);
uint32_t bb_qmix_directsound_activate_entry(uint32_t);
uint32_t bb_qmix_directsound_profile_end_entry(uint32_t);
uint32_t bb_qmix_profile_interval_entry(uint32_t);
uint32_t bb_qmix_waveout_window_callback_entry(uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_waveout_constructor_entry(void);
uint32_t bb_qmix_waveout_constructor_pending_pool_cleanup_entry(void);
uint32_t bb_qmix_waveout_constructor_pending_list_cleanup_entry(void);
uint32_t bb_qmix_waveout_constructor_playing_pool_cleanup_entry(void);
uint32_t bb_qmix_waveout_constructor_playing_list_cleanup_entry(void);
uint32_t bb_qmix_waveout_constructor_window_cleanup_entry(void);
uint32_t bb_qmix_waveout_constructor_wave_cleanup_entry(void);
uint32_t bb_qmix_waveout_constructor_base_cleanup_entry(void);
uint32_t bb_qmix_waveout_delete_entry(uint32_t);
uint32_t bb_qmix_waveout_list_destructor_entry(void);
uint32_t bb_qmix_waveout_list_pool_cleanup_entry(void);
uint32_t bb_qmix_waveout_destructor_entry(void);
uint32_t bb_qmix_waveout_destructor_pending_cleanup_entry(void);
uint32_t bb_qmix_waveout_destructor_playing_cleanup_entry(void);
uint32_t bb_qmix_waveout_destructor_window_cleanup_entry(void);
uint32_t bb_qmix_waveout_destructor_wave_cleanup_entry(void);
uint32_t bb_qmix_waveout_destructor_base_cleanup_entry(void);
uint32_t bb_qmix_waveout_complete_entry(uint32_t);
uint32_t bb_qmix_waveout_profile_end_entry(uint32_t);
uint32_t bb_qmix_driver_window_constructor_entry(uint32_t, uint32_t);
uint32_t bb_qmix_driver_window_destructor_entry(void);
uint32_t bb_qmix_pool_chain_release_entry(void);

/* QMIXER concrete factories, device setup and playback pumps. */
uint32_t bb_qmix_directsound_create_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_directsound_free_failed_entry(void);
uint32_t bb_qmix_directsound_configure_entry(uint32_t, uint32_t);
uint32_t bb_qmix_directsound_initialize_entry(uint32_t);
uint32_t bb_qmix_directsound_pump_entry(void);
uint32_t bb_qmix_session_create_entry(uint32_t);
uint32_t bb_qmix_waveout_create_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_waveout_free_failed_entry(void);
uint32_t bb_qmix_waveout_configure_entry(uint32_t, uint32_t);
uint32_t bb_qmix_waveout_initialize_entry(uint32_t);
uint32_t bb_qmix_waveout_measure_latency_entry(uint32_t, uint32_t);
uint32_t bb_qmix_waveout_measure_cleanup_entry(void);
uint32_t bb_qmix_waveout_activate_entry(uint32_t);
uint32_t bb_qmix_waveout_pump_entry(void);

/* QMIXER DLL bootstrap and runtime lifecycle. */
uint32_t bb_qmix_dll_module_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_allocate_entry(uint32_t);
uint32_t bb_qmix_free_entry(uint32_t);
uint32_t bb_qmix_exception_object_destructor_entry(void);
uint32_t bb_qmix_exception_object_delete_entry(uint32_t);
uint32_t bb_qmix_fp_unavailable_entry(void);
uint32_t bb_qmix_integer_factory_bootstrap_entry(void);
uint32_t bb_qmix_short_factory_bootstrap_entry(void);
uint32_t bb_qmix_float_factory_bootstrap_entry(void);
uint32_t bb_qmix_crt_initialize_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_dll_entry_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_exception_bootstrap_entry(void);
uint32_t bb_qmix_exception_initialize_entry(void);
uint32_t bb_qmix_exception_restore_entry(void);
uint32_t bb_qmix_allocation_retry_entry(uint32_t);
uint32_t bb_qmix_heap_initialize_entry(void);
uint32_t bb_qmix_heap_destroy_entry(void);
uint32_t bb_qmix_constructors_entry(void);
uint32_t bb_qmix_exit_cleanup_entry(void);
uint32_t bb_qmix_exit_dispatch_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_initializer_range_entry(uint32_t, uint32_t);
uint32_t bb_qmix_io_cleanup_entry(void);
uint32_t bb_qmix_runtime_mode_entry(uint32_t);


/* QMIXER CRT and small-block allocator. */
uint32_t bb_qmix_crt_malloc_entry(uint32_t);
uint32_t bb_qmix_crt_malloc_retry_entry(uint32_t, uint32_t);
uint32_t bb_qmix_crt_malloc_block_entry(uint32_t);
uint32_t bb_qmix_crt_free_entry(uint32_t);
uint32_t bb_qmix_small_heap_grow_entry(void);
uint32_t bb_qmix_small_heap_release_entry(uint32_t);
uint32_t bb_qmix_small_heap_trim_entry(uint32_t);
uint32_t bb_qmix_small_heap_find_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_small_heap_free_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_small_heap_allocate_entry(uint32_t);
uint32_t bb_qmix_small_page_allocate_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_small_heap_resize_entry(uint32_t, uint32_t, uint32_t, uint32_t);


/* QMIXER CRT startup and process state. */
uint32_t bb_qmix_io_initialize_entry(void);
uint32_t bb_qmix_environment_initialize_entry(void);
uint32_t bb_qmix_arguments_initialize_entry(void);
uint32_t bb_qmix_arguments_parse_entry(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_codepage_set_entry(uint32_t);
uint32_t bb_qmix_codepage_resolve_entry(uint32_t);
uint32_t bb_qmix_codepage_language_entry(uint32_t);
uint32_t bb_qmix_codepage_clear_entry(void);
uint32_t bb_qmix_codepage_bootstrap_entry(void);
uint32_t bb_qmix_environment_capture_entry(void);
uint32_t bb_qmix_exit_append_entry(uint32_t);
uint32_t bb_qmix_exit_register_entry(uint32_t);
uint32_t bb_qmix_exit_initialize_entry(void);
uint32_t bb_qmix_crt_reallocate_entry(uint32_t, uint32_t);
uint32_t bb_qmix_crt_allocation_size_entry(uint32_t);

/* QMIXER unary/binary math reporting and callbacks. */
double bb_qmix_math_domain_unary_entry(uint32_t, uint32_t, uint32_t, uint32_t);
double bb_qmix_math_domain_binary_entry(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
double bb_qmix_math_exception_unary_entry(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
double bb_qmix_math_exception_binary_entry(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
double bb_qmix_math_report_entry(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

/* QMIXER result normalization and floating exception records. */
uint32_t bb_qmix_math_ieee_exception_entry(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_math_result_normalize_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_fpu_raise_entry(uint32_t);
double bb_qmix_math_fraction_scale_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_math_exponent_entry(uint32_t, uint32_t);
double bb_qmix_math_fraction_entry(uint32_t, uint32_t, uint32_t);

/* QMIXER numeric formatting, rounding and extended packing. */
uint32_t bb_qmix_format_decimal_insert_entry(uint32_t);
uint32_t bb_qmix_format_trim_entry(uint32_t);
uint32_t bb_qmix_format_nonnegative_entry(uint32_t);
uint32_t bb_qmix_format_parse_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_format_scientific_entry(uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_format_fixed_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_format_general_entry(uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_format_scientific_cached_entry(uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_format_fixed_cached_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_format_number_entry(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_format_shift_entry(uint32_t, uint32_t);
uint32_t bb_qmix_format_digits_round_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_format_double_extended_entry(uint32_t, uint32_t);

/* QMIXER extended binary conversion and decimal record adapters. */
uint32_t bb_qmix_convert_tail_zero_entry(uint32_t, uint32_t);
uint32_t bb_qmix_convert_increment_entry(uint32_t, uint32_t);
uint32_t bb_qmix_convert_round_entry(uint32_t, uint32_t);
uint32_t bb_qmix_convert_copy_entry(uint32_t, uint32_t);
uint32_t bb_qmix_convert_clear_entry(uint32_t);
uint32_t bb_qmix_convert_zero_entry(uint32_t);
uint32_t bb_qmix_convert_shift_entry(uint32_t, uint32_t);
uint32_t bb_qmix_convert_binary_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_convert_double_entry(uint32_t, uint32_t);
uint32_t bb_qmix_convert_float_entry(uint32_t, uint32_t);
uint32_t bb_qmix_convert_parse_double_entry(uint32_t, uint32_t);
uint32_t bb_qmix_convert_parse_float_entry(uint32_t, uint32_t);
uint32_t bb_qmix_convert_decimal_record_entry(uint32_t, uint32_t);
uint32_t bb_qmix_convert_add_entry(uint32_t, uint32_t, uint32_t);

/* QMIXER decimal arithmetic, scaling and digit generation. */
uint32_t bb_qmix_decimal_add_entry(uint32_t, uint32_t);
uint32_t bb_qmix_decimal_shift_left_entry(uint32_t);
uint32_t bb_qmix_decimal_shift_right_entry(uint32_t);
uint32_t bb_qmix_decimal_accumulate_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_decimal_generate_entry(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_decimal_classify_entry(uint32_t, uint32_t);
uint32_t bb_qmix_decimal_multiply_entry(uint32_t, uint32_t);
uint32_t bb_qmix_decimal_scale_entry(uint32_t, uint32_t, uint32_t);

/* QMIXER numeric parsing and character helpers. */
uint32_t bb_qmix_parse_character_type_entry(uint32_t, uint32_t);
uint32_t bb_qmix_parse_lowercase_entry(uint32_t);
uint32_t bb_qmix_parse_extended_entry(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

/* QMIXER NLS adapters and zeroed allocation. */
uint32_t bb_qmix_nls_character_type_entry(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_nls_map_string_entry(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_nls_bounded_length_entry(uint32_t, uint32_t);
uint32_t bb_qmix_nls_calloc_entry(uint32_t, uint32_t);

/* QMIXER numeric rounding, parity and extended division. */
double bb_qmix_divide_round_entry(uint32_t, uint32_t);
uint32_t bb_qmix_divide_parity_entry(uint32_t, uint32_t);
double bb_qmix_divide_extended_entry(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

/* QMIXER register/memory division ABI. */
uint32_t bb_qmix_divide_register_dispatch_entry(void);
uint32_t bb_qmix_divide_pair_entry(void);
uint32_t bb_qmix_divide_pair_reverse_entry(void);
uint32_t bb_qmix_divide_memory_float_entry(uint32_t);
uint32_t bb_qmix_divide_memory_double_entry(uint32_t, uint32_t);
uint32_t bb_qmix_divide_memory_word_entry(uint32_t);
uint32_t bb_qmix_divide_memory_integer_entry(uint32_t);
uint32_t bb_qmix_divide_memory_float_reverse_entry(uint32_t);
uint32_t bb_qmix_divide_memory_double_reverse_entry(uint32_t, uint32_t);
uint32_t bb_qmix_divide_memory_word_reverse_entry(uint32_t);
uint32_t bb_qmix_divide_memory_integer_reverse_entry(uint32_t);
uint32_t bb_qmix_divide_pair_preserved_entry(void);
uint32_t bb_qmix_divide_pair_reverse_preserved_entry(void);

/* QMIXER remainder ABI. */
uint32_t bb_qmix_remainder_arguments_entry(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_remainder_register_entry(void);
uint32_t bb_qmix_remainder_nearest_arguments_entry(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_remainder_nearest_register_entry(void);
uint32_t bb_qmix_remainder_alias_entry(void);
uint32_t bb_qmix_remainder_nearest_alias_entry(void);

/* QMIXER signed-16-bit multi-tap delay ABI. */
uint32_t bb_qmix_tap_delay_factory_entry(uint32_t);
uint32_t bb_qmix_tap_delay_factory_cleanup_entry(void);
uint32_t bb_qmix_tap_delay_constructor_entry(uint32_t);
uint32_t bb_qmix_tap_delay_filter_cleanup_entry(void);
uint32_t bb_qmix_tap_delay_base_cleanup_entry(void);
uint32_t bb_qmix_tap_delay_delete_entry(uint32_t);
uint32_t bb_qmix_tap_delay_destructor_entry(void);
uint32_t bb_qmix_tap_delay_destructor_cleanup_entry(void);
uint32_t bb_qmix_tap_delay_process_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_tap_delay_silent_entry(void);
uint32_t bb_qmix_tap_delay_clear_entry(void);

/* QMIXER signal routing ABI. */
uint32_t bb_qmix_signal_abort_entry(void);
uint32_t bb_qmix_signal_raise_entry(uint32_t);
uint32_t bb_qmix_signal_lookup_entry(uint32_t);

/* QMIXER overlap-safe copy ABI. */
uint32_t bb_qmix_crt_memmove_entry(uint32_t, uint32_t, uint32_t);

/* QMIXER math adapter ABI. */
uint32_t bb_qmix_math_power_register_entry(void);
uint32_t bb_qmix_math_load_double_entry(uint32_t, uint32_t);
uint32_t bb_qmix_math_power_special_entry(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_math_record_error_entry(uint32_t, uint32_t, uint32_t);

/* QMIXER public and common math wrapper ABI. */
uint32_t bb_qmix_math_public_power_memory_entry(uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_math_public_log_memory_entry(uint32_t, uint32_t);
uint32_t bb_qmix_math_public_log10_memory_entry(uint32_t, uint32_t);
uint32_t bb_qmix_math_public_exp_memory_entry(uint32_t, uint32_t);
uint32_t bb_qmix_math_public_power_register_entry(void);
uint32_t bb_qmix_math_public_log_register_entry(void);
uint32_t bb_qmix_math_public_log10_register_entry(void);
uint32_t bb_qmix_math_public_exp_register_entry(void);
uint32_t bb_qmix_math_wrapper_power_register_entry(void);
uint32_t bb_qmix_math_wrapper_unary_register_entry(void);
uint32_t bb_qmix_math_wrapper_power_memory_entry(uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_math_wrapper_unary_memory_entry(uint32_t, uint32_t);

/* QMIXER calculation leaf ABI. */
uint32_t bb_qmix_calculation_power_entry(void);
uint32_t bb_qmix_calculation_exp_entry(void);
uint32_t bb_qmix_calculation_power_singularity_entry(void);
uint32_t bb_qmix_calculation_log_checked_entry(void);
uint32_t bb_qmix_calculation_log_zero_entry(void);
uint32_t bb_qmix_calculation_log_infinity_entry(void);
uint32_t bb_qmix_calculation_log_scaled_entry(void);
uint32_t bb_qmix_calculation_power_one_entry(void);
uint32_t bb_qmix_calculation_power_zero_entry(void);
uint32_t bb_qmix_calculation_power_underflow_entry(void);
uint32_t bb_qmix_calculation_domain_entry(void);
uint32_t bb_qmix_calculation_power_overflow_entry(void);
uint32_t bb_qmix_calculation_exp_infinity_entry(void);
uint32_t bb_qmix_calculation_parity_entry(void);
uint32_t bb_qmix_calculation_nan_unary_entry(void);
uint32_t bb_qmix_calculation_nan_binary_entry(void);
uint32_t bb_qmix_calculation_nan_pair_entry(void);
uint32_t bb_qmix_calculation_domain_record_entry(void);
uint32_t bb_qmix_calculation_mark_domain_entry(void);
uint32_t bb_qmix_calculation_negate_entry(void);

/* QMIXER numeric dispatch/finalization ABI. */
uint32_t bb_qmix_numeric_result_finalize_memory_entry(void);
uint32_t bb_qmix_numeric_result_finalize_entry(void);
uint32_t bb_qmix_numeric_dispatch_unary_entry(void);
uint32_t bb_qmix_numeric_dispatch_binary_entry(void);

/* QMIXER numeric startup and CRT control/reporting helpers. */
uint32_t bb_qmix_numeric_bootstrap_entry(void);
uint32_t bb_qmix_numeric_finalize_entry(void);
uint32_t bb_qmix_numeric_tables_entry(void);
uint32_t bb_qmix_math_errno_entry(uint32_t);
uint32_t bb_qmix_math_name_entry(uint32_t);
uint32_t bb_qmix_math_error_kind_entry(uint32_t);
uint32_t bb_qmix_fpu_status_entry(void);
uint32_t bb_qmix_fpu_clear_entry(void);
uint32_t bb_qmix_fpu_word_entry(uint32_t, uint32_t);
uint32_t bb_qmix_math_classify_entry(uint32_t, uint32_t);
uint32_t bb_qmix_fpu_default_entry(void);
uint32_t bb_qmix_fpu_divide_probe_entry(void);
uint32_t bb_qmix_fpu_divide_detect_entry(void);
uint32_t bb_qmix_math_callback_entry(uint32_t);
uint32_t bb_qmix_fpu_control_entry(uint32_t, uint32_t);
uint32_t bb_qmix_fpu_control_safe_entry(uint32_t, uint32_t);
uint32_t bb_qmix_fpu_decode_entry(uint32_t);
uint32_t bb_qmix_fpu_encode_entry(uint32_t);

/* QMIXER host math and temporary x87 integer boundary. */
uint32_t bb_qmix_audio_round_entry(uint32_t);
uint32_t bb_qmix_float_integer_entry(void);
double bb_qmix_crt_floor_entry(uint32_t, uint32_t);
double bb_qmix_crt_hypot_entry(uint32_t, uint32_t, uint32_t, uint32_t);
double bb_qmix_crt_hypot_operation_entry(uint32_t, uint32_t, uint32_t, uint32_t,
                                       uint32_t);

/* QMIXER errors, exceptions and string/pointer helpers. */
uint32_t bb_qmix_exception_raise_entry(uint32_t, uint32_t);
uint32_t bb_qmix_string_compare_bounded_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_pure_virtual_failure_entry(void);
uint32_t bb_qmix_string_find_last_entry(uint32_t, uint32_t);
uint32_t bb_qmix_fatal_error_entry(uint32_t);
uint32_t bb_qmix_exception_filter_entry(uint32_t);
uint32_t bb_qmix_quick_exit_entry(uint32_t);
uint32_t bb_qmix_error_banner_entry(void);
uint32_t bb_qmix_error_report_entry(uint32_t);
uint32_t bb_qmix_memory_read_probe_entry(uint32_t, uint32_t);
uint32_t bb_qmix_memory_write_probe_entry(uint32_t, uint32_t);
uint32_t bb_qmix_code_probe_entry(uint32_t);
uint32_t bb_qmix_error_message_box_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_string_copy_bounded_entry(uint32_t, uint32_t, uint32_t);

/* QMIXER queues, rendering and spatial/voice helpers retain the original ABI. */
uint32_t bb_qmix_channel_bind_entry(uint32_t);
uint32_t bb_qmix_cone_set_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_distance_set_entry(uint32_t);
uint32_t bb_qmix_distance_update_entry(void);
uint32_t bb_qmix_voice_queue_entry(uint32_t);
uint32_t bb_qmix_voice_start_entry(uint32_t);
uint32_t bb_qmix_channel_stop_entry(uint32_t);
uint32_t bb_qmix_chain_stop_entry(uint32_t, uint32_t);
uint32_t bb_qmix_channel_drain_entry(void);
uint32_t bb_qmix_channel_render_entry(uint32_t);
uint32_t bb_qmix_spatial_update_entry(void);
uint32_t bb_qmix_channel_progress_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_cone_get_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_motion_constructor_entry(void);
uint32_t bb_qmix_motion_tick_entry(uint32_t);
uint32_t bb_qmix_motion_filter_entry(uint32_t, uint32_t);
uint32_t bb_qmix_pool_take_entry(void);
uint32_t bb_qmix_decoder_unavailable_entry(void);
uint32_t bb_qmix_voice_constructor_entry(uint32_t);
uint32_t bb_qmix_voice_destructor_entry(void);
uint32_t bb_qmix_voice_notify_entry(uint32_t);
uint32_t bb_qmix_voice_matches_entry(uint32_t);
uint32_t bb_qmix_wave_contains_entry(uint32_t, uint32_t);
uint32_t bb_qmix_voice_render_entry(uint32_t, uint32_t);
uint32_t bb_qmix_voice_progress_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_vector_scale_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_vector_subtract_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_vector_add_entry(uint32_t, uint32_t, uint32_t);
double bb_qmix_vector_dot_entry(uint32_t, uint32_t);
uint32_t bb_qmix_vector_cross_entry(uint32_t, uint32_t, uint32_t);
double bb_qmix_vector_length_entry(uint32_t);

/* QMIXER methods retain ECX=this and their original stack cleanup. */
uint32_t bb_qmix_current_voice_entry(void);
uint32_t bb_qmix_channel_enable_entry(uint32_t);
uint32_t bb_qmix_pan_rate_entry(uint32_t);
uint32_t bb_qmix_channel_volume_entry(uint32_t);
uint32_t bb_qmix_channel_frequency_entry(uint32_t);
uint32_t bb_qmix_channel_position_entry(uint32_t);
uint32_t bb_qmix_source_position_entry(uint32_t);
uint32_t bb_qmix_source_velocity_entry(uint32_t);
uint32_t bb_qmix_position_update_entry(uint32_t);
uint32_t bb_qmix_gain_update_entry(void);
uint32_t bb_qmix_mark_stop_entry(void);
uint32_t bb_qmix_source_position_get_entry(uint32_t);
uint32_t bb_qmix_source_velocity_get_entry(uint32_t);
uint32_t bb_qmix_distance_get_entry(uint32_t);
uint32_t bb_qmix_queued_duration_entry(void);
uint32_t bb_qmix_error_get_entry(void);
uint32_t bb_qmix_error_set_entry(uint32_t);
uint32_t bb_qmix_base_constructor_entry(uint32_t);
uint32_t bb_qmix_default_true_entry(uint32_t);
uint32_t bb_qmix_default_false_entry(void);
uint32_t bb_qmix_default_zero_entry(void);
uint32_t bb_qmix_deleting_destructor_entry(uint32_t);
uint32_t bb_qmix_base_destructor_entry(void);
uint32_t bb_qmix_activate_entry(void);
uint32_t bb_qmix_enabled_set_entry(uint32_t);
uint32_t bb_qmix_default_position_entry(uint32_t);
uint32_t bb_qmix_volume_store_entry(uint32_t);
uint32_t bb_qmix_position_store_entry(uint32_t);
uint32_t bb_qmix_pan_rate_store_entry(uint32_t);
uint32_t bb_qmix_frequency_store_entry(uint32_t);
uint32_t bb_qmix_increment_calc_entry(uint32_t);
uint32_t bb_qmix_default_reset_entry(void);
uint32_t bb_qmix_default_set_entry(uint32_t);
uint32_t bb_qmix_default_position_get_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_pause_entry(void);
uint32_t bb_qmix_restart_entry(void);
uint32_t bb_qmix_default_pump_entry(void);
uint32_t bb_qmix_deactivate_entry(void);
uint32_t bb_qmix_voice_prepare_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_voice_duration_entry(void);
uint32_t bb_qmix_volume_invalidate_entry(void);
uint32_t bb_qmix_position_invalidate_entry(void);
uint32_t bb_qmix_session_validate_entry(void);

/* Game-specific sound cues, selectors and lifecycle retain the original ABI. */
uint32_t bb_sound_bank_replace_entry(uint32_t);
uint32_t bb_sound_bank_load_entry(uint32_t);
uint32_t bb_sound_state_clear_entry(uint32_t);
uint32_t bb_sound_states_clear_entry(void);
uint32_t bb_sound_state_activate_entry(uint32_t);
uint32_t bb_sound_state_active_entry(uint32_t);
uint32_t bb_sound_state_count_entry(void);
uint32_t bb_sound_selection_update_entry(void);
uint32_t bb_sound_current_stop_entry(void);
uint32_t bb_sound_bank_stop_entry(void);
uint32_t bb_sound_flags_reset_entry(void);
uint32_t bb_sound_cues_reset_entry(void);
uint32_t bb_sound_mode_set_entry(uint32_t);
uint32_t bb_sound_transition_entry(
    uint32_t, uint32_t, uint32_t, uint32_t,
    uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_sound_selector_456920_entry(void);
uint32_t bb_sound_selector_456960_entry(void);
uint32_t bb_sound_selector_4569a0_entry(void);
uint32_t bb_sound_selector_4569e0_entry(void);
uint32_t bb_sound_selector_456a20_entry(void);
uint32_t bb_sound_selector_456a50_entry(void);
uint32_t bb_sound_cue_456aa0_entry(uint32_t);
uint32_t bb_sound_cue_456d20_entry(uint32_t);
uint32_t bb_sound_cue_456ee0_entry(uint32_t);
uint32_t bb_sound_cue_4573d0_entry(uint32_t);
uint32_t bb_sound_cue_457590_entry(uint32_t);
uint32_t bb_sound_cue_457820_entry(uint32_t);
uint32_t bb_sound_cue_4579e0_entry(uint32_t, uint32_t);
uint32_t bb_sound_selector_457bf0_entry(void);
uint32_t bb_sound_selector_457c80_entry(void);
uint32_t bb_sound_selector_457cc0_entry(void);
uint32_t bb_sound_selector_457d30_entry(void);
uint32_t bb_sound_selector_457d60_entry(void);
uint32_t bb_sound_selector_457d90_entry(void);
uint32_t bb_sound_selector_457de0_entry(void);
uint32_t bb_sound_selector_457e20_entry(void);
uint32_t bb_sound_selector_457e50_entry(void);
uint32_t bb_sound_selector_457e80_entry(void);
uint32_t bb_sound_selector_457ec0_entry(uint32_t);
uint32_t bb_sound_selector_457f50_entry(void);
uint32_t bb_sound_selector_457f80_entry(void);
uint32_t bb_sound_selector_457ff0_entry(void);
uint32_t bb_sound_selector_458050_entry(void);
uint32_t bb_sound_cue_4580a0_entry(uint32_t, uint32_t);
uint32_t bb_sound_cue_4581c0_entry(uint32_t, uint32_t);
uint32_t bb_sound_cue_458350_entry(uint32_t, uint32_t);
uint32_t bb_sound_cue_4584d0_entry(uint32_t);
uint32_t bb_sound_cue_458660_entry(uint32_t, uint32_t);
uint32_t bb_sound_cue_458890_entry(uint32_t, uint32_t);
uint32_t bb_sound_cue_458ad0_entry(uint32_t, uint32_t);
uint32_t bb_sound_cue_458c90_entry(uint32_t, uint32_t);
uint32_t bb_sound_cue_458e10_entry(uint32_t);
uint32_t bb_sound_cue_458fa0_entry(uint32_t, uint32_t);
uint32_t bb_sound_cue_459070_entry(uint32_t, uint32_t);
uint32_t bb_sound_cue_4594a0_entry(uint32_t, uint32_t);
uint32_t bb_sound_cue_459620_entry(uint32_t, uint32_t);
uint32_t bb_sound_cue_4597a0_entry(uint32_t);
uint32_t bb_sound_init_entry(void);
uint32_t bb_sound_shutdown_entry(void);
uint32_t bb_sound_enable_entry(uint32_t);
uint32_t bb_sound_event_entry(uint32_t, uint32_t);
uint32_t bb_sound_property_entry(uint32_t, uint32_t);
uint32_t bb_sound_update_entry(void);

/* Timed sound-effect ramps and playback sequences retain the original ABI. */
uint32_t bb_sample_oscillation_step_entry(
    uint32_t, uint32_t, uint32_t, uint32_t,
    uint32_t, uint32_t);
uint32_t bb_sample_oscillation_init_entry(uint32_t, uint32_t);
uint32_t bb_sample_ramp_step_entry(
    uint32_t, uint32_t, uint32_t, uint32_t,
    uint32_t);
uint32_t bb_sample_random_ramp_step_entry(
    uint32_t, uint32_t, uint32_t, uint32_t,
    uint32_t, uint32_t, uint32_t);
uint32_t bb_sample_loop_fade_step_entry(
    uint32_t, uint32_t, uint32_t, uint32_t,
    uint32_t, uint32_t, uint32_t, uint32_t,
    uint32_t, uint32_t);
uint32_t bb_sample_sequence_init_entry(
    uint32_t, uint32_t, uint32_t, uint32_t,
    uint32_t, uint32_t, uint32_t, uint32_t,
    uint32_t);
uint32_t bb_sample_sequence_stop_entry(uint32_t);
uint32_t bb_sample_sequence_volume_entry(uint32_t, uint32_t);
uint32_t bb_sample_sequence_pan_entry(uint32_t, uint32_t);
uint32_t bb_sample_sequence_pitch_entry(uint32_t, uint32_t);
uint32_t bb_sample_sequence_loops_entry(uint32_t, uint32_t);
uint32_t bb_sample_continuous_step_entry(uint32_t);
uint32_t bb_sample_crossfade_step_entry(uint32_t);

/* Sample bank, session, channel and effect entries keep their original ABI. */
uint32_t bb_sample_bank_load_entry(uint32_t);
uint32_t bb_sample_bank_free_entry(uint32_t);
uint32_t bb_sample_status_text_entry(void);
uint32_t bb_sample_session_init_entry(uint32_t, uint32_t);
uint32_t bb_sample_session_shutdown_entry(void);
uint32_t bb_sample_channels_refresh_entry(void);
uint32_t bb_sample_channel_id_entry(uint32_t, uint32_t);
uint32_t bb_sample_instance_count_entry(uint32_t, uint32_t);
uint32_t bb_sample_channel_count_entry(void);
uint32_t bb_sample_channel_play_entry(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_sample_channel_submit_entry(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_sample_channel_stop_entry(uint32_t);
uint32_t bb_sample_channel_active_entry(uint32_t);
uint32_t bb_sample_device_text_entry(void);
uint32_t bb_sample_channel_pan_entry(uint32_t, uint32_t);
uint32_t bb_sample_channel_volume_entry(uint32_t, uint32_t);
uint32_t bb_sample_channel_pitch_entry(uint32_t, uint32_t);
uint32_t bb_sample_channels_release_entry(void);
uint32_t bb_sample_bank_stop_entry(uint32_t);
uint32_t bb_sample_session_pause_entry(void);
uint32_t bb_sample_session_resume_entry(void);
uint32_t bb_sample_session_enable_entry(uint32_t);
uint32_t bb_sample_session_options_entry(uint32_t, uint32_t);
uint32_t bb_sample_effect_reset_entry(uint32_t);
uint32_t bb_sample_effects_reset_entry(void);
uint32_t bb_sample_effects_init_entry(void);
uint32_t bb_sample_effects_stop_entry(uint32_t);
uint32_t bb_sample_effect_reserve_entry(uint32_t);
uint32_t bb_sample_effect_release_entry(uint32_t);
uint32_t bb_sample_effect_bind_entry(uint32_t, uint32_t);
uint32_t bb_sample_effect_channel_entry(uint32_t);
uint32_t bb_sample_random_range_entry(uint32_t, uint32_t);
uint32_t bb_sample_random_clock_seed_entry(void);
double bb_sample_pitch_scale_entry(uint32_t);
uint32_t bb_sample_random_seed_entry(uint32_t);

/* Register-based resource mixer entries are dispatched by address, not as
 * ordinary C calls. The timer callback retains its stdcall five-word cleanup. */
uint32_t bb_audio_resource_volume_entry(uint32_t);
uint32_t bb_audio_resource_init_entry(uint32_t);
uint32_t bb_audio_timer_start_entry(void);
uint32_t bb_audio_buffer_create_entry(void);
uint32_t bb_audio_timer_callback_entry(uint32_t, uint32_t, uint32_t, uint32_t,
                                     uint32_t);
uint32_t bb_audio_resource_update_entry(void);
uint32_t bb_audio_buffer_gap_entry(void);
uint32_t bb_audio_voice_mix_entry(void);
uint32_t bb_audio_voice_reset_entry(void);
uint32_t bb_audio_voice_play_entry(uint32_t resource, uint32_t unused,
                                 uint32_t flags, uint32_t pan,
                                 uint32_t attenuation);
uint32_t bb_audio_read_integer_entry(void);
uint32_t bb_audio_voice_duration_entry(uint32_t);
uint32_t bb_audio_voice_stop_entry(uint32_t);
uint32_t bb_audio_voices_stop_entry(void);
uint32_t bb_audio_resource_shutdown_entry(void);
uint32_t bb_audio_timer_stop_entry(void);
uint32_t bb_audio_decode_init_entry(void);
uint32_t bb_audio_bits_read_entry(void);
uint32_t bb_audio_decode_block_entry(void);
uint32_t bb_audio_decode_residual_entry(void);
uint32_t bb_audio_bits_skip_entry(void);
uint32_t bb_audio_interpolate_entry(void);
uint32_t bb_audio_synthesize_entry(void);
uint32_t bb_audio_coefficients_entry(void);

uint32_t bb_sound_play_entry(uint32_t);
uint32_t bb_sound_position_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_sample_play_entry(uint32_t);
uint32_t bb_sample_position_entry(uint32_t, uint32_t, uint32_t);
uint32_t BB_THISCALL bb_sound_resource_play_entry(void *, uint32_t);
uint32_t BB_THISCALL bb_sound_resource_pan_entry(void *, uint32_t);
uint32_t BB_THISCALL bb_sound_resource_volume_entry(void *, uint32_t);
uint32_t BB_THISCALL bb_sound_resource_voice_entry(void *);
uint32_t bb_sample_lookup_entry(uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_sample_schedule_entry(uint32_t, uint32_t, uint32_t, uint32_t);
double bb_sample_gain_entry(int32_t);

uint32_t BB_THISCALL bb_message_construct_entry(void *);
uint32_t BB_THISCALL bb_message_destructor_entry(void *);
uint32_t BB_THISCALL bb_message_destroy_entry(void *);
uint32_t BB_THISCALL bb_message_append_entry(void *, uint32_t,
                                                           uint32_t);
uint32_t BB_THISCALL bb_message_current_entry(void *, uint32_t);
uint32_t BB_THISCALL bb_message_draw_entry(void *);
uint32_t BB_THISCALL bb_message_next_entry(void *);
uint32_t BB_THISCALL bb_message_tick_entry(void *);
uint32_t BB_THISCALL bb_message_input_entry(void *, uint32_t,
                                                          uint32_t);
uint32_t BB_THISCALL bb_message_self_entry(void *);

uint32_t BB_THISCALL bb_notification_clear_entry(void *);
uint32_t BB_THISCALL bb_notification_draw_entry(void *);
uint32_t BB_THISCALL bb_notification_tick_entry(void *);
uint32_t BB_THISCALL
bb_notification_number_entry(void *, uint32_t, uint32_t);
uint32_t BB_THISCALL
bb_notification_entity_entry(void *, uint32_t, uint32_t);
uint32_t BB_THISCALL
bb_notification_enqueue_entry(void *, uint32_t, uint32_t, uint32_t);
uint32_t BB_THISCALL
bb_faction_attack_alert_entry(void *, uint32_t, uint32_t);

int32_t bb_random(void);
uint64_t bb_random_registers(void);
uint32_t bb_random_seed_entry(uint32_t);
uint32_t bb_road_shape_entry(uint32_t, uint32_t);
uint32_t bb_road_prune_near_entry(uint32_t, uint32_t);
uint32_t bb_road_prune_entry(uint32_t, uint32_t);
uint32_t BB_THISCALL bb_terrain_fill_entry(void *, uint32_t,
                                                         uint32_t, uint32_t);
uint32_t BB_THISCALL bb_terrain_radius_entry(void *, uint32_t);
void bb_terrain_open_all_entry(void);
void bb_terrain_block_all_entry(void);
uint32_t BB_THISCALL bb_terrain_blend_entry(void *, uint32_t,
                                                          uint32_t);
uint32_t BB_THISCALL bb_terrain_blend_near_entry(void *, uint32_t,
                                                               uint32_t);
uint32_t BB_THISCALL bb_terrain_blend_area_entry(void *, uint32_t,
                                                               uint32_t);
uint32_t BB_THISCALL
bb_terrain_set_entry(void *, uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t BB_THISCALL
bb_terrain_transition_entry(void *, uint32_t, uint32_t, uint32_t, uint32_t,
                            uint32_t);

typedef void(BB_THISCALL * bb_draw_sprite_fn)(void *, int, int,
                                                            int, int);
typedef void(BB_THISCALL * bb_present_fn)(void *);

void bb_display_construct_entry(void);
void bb_display_destroy_entry(void);
void bb_display_mode_callback_entry(void);
void bb_display_invalidate_entry(void);
uint32_t bb_display_invalidate(uint32_t);
uint32_t BB_THISCALL bb_display_initialize(void *, void *,
                                                         uint32_t);
uint32_t BB_THISCALL bb_display_primary(void *);
uint32_t BB_THISCALL bb_display_release(void *);
void bb_render_queue_construct_entry(void);
void bb_render_queue_reset_entry(void);
void bb_render_queue_advance_entry(void);
void bb_render_queue_compact_entry(void);
void bb_render_queue_pending_clear_entry(void);
void bb_render_queue_insert_entry(void);
void bb_render_queue_both_entry(void);
void bb_render_queue_pending_add_entry(void);
void bb_render_queue_translate_entry(void);
void bb_render_queue_merge_entry(void);
uint32_t bb_render_region(int32_t, int32_t, int32_t, int32_t, void *, uint32_t,
                          uint32_t);
void bb_render_queue_process_entry(void);
void bb_render_queue_scroll_entry(void);
int32_t BB_THISCALL bb_render_queue_advance(void *);
uint32_t BB_THISCALL bb_render_queue_compact(void *);
uint32_t BB_THISCALL bb_render_queue_scroll(void *);
uint32_t BB_THISCALL bb_render_queue_process(void *, void *,
                                                           uint32_t, uint32_t);
void bb_render_queue_insert(void *, int32_t, int32_t, int32_t, int32_t,
                            uint32_t);
void bb_render_queue_both(void *, int32_t, int32_t, int32_t, int32_t);
void bb_render_queue_merge(void *);
void bb_render_queue_pending_add(void *, int32_t, int32_t, int32_t, int32_t,
                                 uint32_t);
void bb_fixed_multiply_entry(void);
uint32_t bb_fixed_multiply(int32_t, int32_t);
uint64_t bb_fixed_multiply_registers(int32_t, int32_t);
void bb_fixed_divide_entry(void);
uint32_t bb_fixed_divide(int32_t, int32_t);
uint64_t bb_fixed_divide_registers(int32_t, int32_t);
void bb_entity_begin_entry(void);
uint32_t BB_THISCALL bb_entity_begin(void *);
void bb_entity_project_entry(void);
uint32_t BB_THISCALL bb_entity_project(void *, uint32_t,
                                                     uint32_t);
uint32_t bb_terrain_visible(void);
uint32_t bb_render_frame(void);
uint64_t bb_terrain_dirty(uint32_t, uint32_t);
void bb_terrain_dirty_entry(void);
uint32_t bb_render_background(void);
uint32_t bb_render_overlays(void);
void bb_entity_dirty_entry(void);
void bb_entity_set_sprite_entry(void);
void bb_entity_bar_entry(void);
void bb_entity_selection_entry(void);
void bb_entity_reflection_entry(void);
void bb_entity_dirty(void *);
void bb_entity_set_sprite(void *, void *);
uint64_t bb_entity_dirty_c(void *, uint32_t, uint32_t);
uint64_t bb_entity_set_sprite_c(void *, uint32_t, uint32_t);
uint32_t bb_entity_bar_c(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                         uint32_t, uint32_t);
uint32_t BB_THISCALL bb_entity_selection(void *);
uint32_t BB_THISCALL bb_entity_reflection(void *);
uint32_t bb_entity_sprite(void *);
uint32_t BB_THISCALL bb_entity_draw(void *);
uint32_t BB_THISCALL bb_entity_render_finish(void *);
void bb_entity_sort_entry(void);
uint32_t BB_THISCALL bb_entity_sort(void *, void *);
void *BB_THISCALL bb_entity_at(void *, uint32_t, uint32_t);
void *BB_THISCALL bb_entity_at_next(void *, void *, uint32_t,
                                                  uint32_t);
void *BB_THISCALL bb_entity_outer_at(void *, uint32_t, uint32_t);
void *BB_THISCALL bb_entity_near(void *, uint32_t, uint32_t,
                                               uint32_t);
void *BB_THISCALL bb_entity_near_next(void *, void *, uint32_t,
                                                    uint32_t, uint32_t);
uint32_t bb_entity_distance(uint32_t, uint32_t, uint32_t, uint32_t);
void *BB_THISCALL bb_entity_nearest(void *, uint32_t, uint32_t,
                                                  uint32_t, uint32_t);
void *BB_THISCALL bb_entity_assign_target(void *, void *);
uint64_t bb_entity_occupancy_add_c(void *, uint32_t, uint32_t);
uint64_t bb_entity_occupancy_remove_c(void *, uint32_t, uint32_t);
void bb_entity_occupancy_add_entry(void);
void bb_entity_occupancy_remove_entry(void);
void bb_entity_force_dirty_entry(void);
uint32_t BB_THISCALL bb_entity_set_ability(void *, uint32_t);
uint32_t BB_THISCALL bb_entity_has_ability(void *, uint32_t);
void bb_entity_manager_construct_entry(void);
void bb_entity_construct_entry(void);
void bb_entity_pool_allocate_entry(void);
void bb_entity_manager_clear_entry(void);
uint32_t BB_THISCALL bb_entity_manager_clear(void *);
uint32_t BB_THISCALL bb_entity_manager_reset(void *);
void *BB_THISCALL bb_entity_create(void *, uint32_t, uint32_t,
                                                uint32_t, uint32_t, uint32_t);
uint32_t BB_THISCALL bb_entity_remove(void *, void *);
void bb_entity_remove_at_entry(void);
uint32_t BB_THISCALL bb_entity_remove_at(void *, uint32_t, uint32_t);
uint32_t BB_THISCALL bb_entity_clear_references(void *, void *);
uint32_t BB_THISCALL bb_entity_destroy(void *, void *);
uint32_t BB_THISCALL bb_entity_remove_pending(void *);
void bb_entity_data_construct_entry(void);
void bb_entity_speed_entry(void);
void bb_entity_target_entry(void);
uint64_t bb_entity_speed_c(void *);
uint64_t bb_entity_target_c(void *, uint32_t, uint32_t);
uint32_t BB_THISCALL bb_entity_profession(void *, uint32_t);
uint32_t BB_THISCALL bb_entity_upgrade_parts(void *);
uint32_t BB_THISCALL bb_entity_refresh_tiles(void *);
uint32_t BB_THISCALL bb_entity_register_tiles(void *);
uint32_t BB_THISCALL bb_entity_update_tile(void *);
uint32_t bb_entity_data_construct(void *);
uint32_t bb_entity_force_dirty_c(void *, uint32_t);
uint32_t BB_THISCALL bb_entity_initialize(void *, uint32_t,
                                                        uint32_t, uint32_t,
                                                        uint32_t);
void bb_entity_first_name_entry(void);
void bb_entity_last_name_entry(void);
uint32_t BB_THISCALL bb_entity_name_title(void *, char *);
uint32_t BB_THISCALL bb_entity_name(void *, char *);
uint32_t BB_THISCALL bb_entity_birth(void *, void *, void *);
uint32_t BB_THISCALL bb_entity_clear_subtype(void *);
uint32_t BB_THISCALL bb_entity_subtype(void *, uint32_t);
uint32_t BB_THISCALL bb_entity_sound(void *, uint32_t);
void bb_entity_blockers_add_entry(void);
void bb_entity_blockers_remove_entry(void);
void bb_entity_move_start_entry(void);
void bb_entity_move_stop_entry(void);
void bb_entity_recruit_entry(void);
void bb_entity_evade_entry(void);
void bb_entity_command_entry(void);
void bb_entity_walk_entry(void);
void bb_entity_feedback_entry(void);
uint32_t bb_entity_blockers_c(void *, uint32_t, uint32_t);
uint64_t bb_entity_move_start_c(void *, uint32_t, uint32_t);
uint64_t bb_entity_move_stop_c(void *);
uint32_t BB_THISCALL bb_entity_command_position(void *);
uint32_t BB_THISCALL bb_entity_move_goal(void *, uint32_t, uint32_t);
uint32_t BB_THISCALL bb_entity_move_near(void *, uint8_t *, uint8_t *);
uint32_t BB_THISCALL bb_entity_move(void *, uint32_t, uint32_t);
uint32_t BB_THISCALL bb_entity_move_escape(void *, uint32_t, uint32_t);
uint32_t BB_THISCALL bb_entity_reset_move(void *);
uint32_t BB_THISCALL bb_entity_command_sound(void *, uint32_t);
uint32_t bb_entity_walk_c(void *, uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_entity_evade_c(void *, void *, uint32_t);
uint32_t bb_entity_feedback_c(void *, void *, uint32_t, uint32_t, uint32_t);
uint32_t bb_entity_recruit_c(void *, void *, uint32_t);
uint32_t bb_entity_command_c(void *, void *, uint32_t, uint32_t, uint32_t);
uint32_t BB_THISCALL bb_entity_route(void *, uint32_t, uint32_t);
uint32_t BB_THISCALL bb_entity_release_tiles(void *);

uint32_t BB_THISCALL bb_entity_resume(void *);
uint32_t BB_THISCALL bb_entity_place_tile(void *, uint32_t,
                                                        uint32_t);
uint32_t BB_THISCALL bb_entity_place(void *, uint32_t, uint32_t);
uint32_t bb_entity_approach_c(void *, void *, uint32_t, uint32_t);
void bb_entity_approach_entry(void);
uint32_t bb_entity_check_arrival_c(void *, uint32_t);
void bb_entity_check_arrival_entry(void);
uint32_t BB_THISCALL bb_entity_action_begin(void *);
uint64_t bb_entity_action_c(void *, uint32_t, uint32_t, uint32_t, uint32_t);
void bb_entity_action_entry(void);
uint32_t BB_THISCALL bb_entity_cargo(void *, void *);
uint64_t bb_entity_secondary_c(void *, void *);
void bb_entity_secondary_entry(void);
uint32_t BB_THISCALL bb_entity_path_step(void *);
uint32_t BB_THISCALL bb_entity_destination_step(void *);
uint32_t BB_THISCALL bb_entity_heal(void *, uint32_t);
uint32_t BB_THISCALL bb_entity_idle_sound(void *);
uint32_t BB_THISCALL bb_entity_arrive(void *);

uint64_t bb_faction_cash_c(void *, uint32_t, uint32_t);
uint64_t bb_faction_scroll_c(void *, uint32_t);
void bb_faction_initialize_c(void *, uint32_t);
uint32_t BB_THISCALL bb_faction_buildable(void *);
uint32_t BB_THISCALL bb_faction_count_add(void *, uint32_t,
                                                        uint32_t, uint32_t);
uint32_t BB_THISCALL bb_faction_count_remove(void *, uint32_t,
                                                           uint32_t, uint32_t);
uint32_t BB_THISCALL bb_faction_manager_reset(void *);
uint32_t BB_THISCALL bb_faction_tick(void *);
uint32_t bb_faction_enable(void);
void *BB_THISCALL bb_faction_subconstruct(void *);
void *BB_THISCALL bb_faction_construct(void *);
void *BB_THISCALL bb_faction_manager_construct(void *);
void bb_faction_cash_entry(void);
void bb_faction_scroll_entry(void);
void bb_faction_initialize_entry(void);
void bb_faction_enable_entry(void);
void bb_faction_construct_entry(void);
void bb_faction_manager_construct_entry(void);
uint64_t bb_faction_recount_c(void *);
uint64_t bb_faction_strength_c(void *);
uint32_t bb_faction_selection_clear_c(void *, uint32_t);
uint32_t bb_faction_group_command_c(void *, uint32_t, uint32_t, uint32_t,
                                    uint32_t, uint32_t);
uint32_t bb_faction_buildings_c(void *, uint32_t);
uint32_t BB_THISCALL bb_faction_group_cache(void *, uint32_t);
uint32_t BB_THISCALL bb_faction_group_refresh(void *, uint32_t);
uint32_t BB_THISCALL bb_faction_stage(void *, uint32_t);
uint32_t BB_THISCALL bb_faction_group_ready(void *, uint32_t);
uint32_t BB_THISCALL bb_faction_plan_seed(void *);
uint32_t BB_THISCALL bb_faction_plan(void *);
uint32_t BB_THISCALL bb_faction_workforce(void *);
uint32_t BB_THISCALL bb_faction_military(void *);
uint32_t BB_THISCALL bb_faction_ai(void *);
void bb_faction_recount_entry(void);
void bb_faction_strength_entry(void);
void bb_faction_selection_clear_entry(void);
void bb_faction_group_command_entry(void);
void bb_faction_buildings_entry(void);
void bb_faction_idle_entry(void);
uint32_t BB_THISCALL bb_faction_selection_remove(void *, void *);
uint32_t BB_THISCALL bb_faction_selection_add(void *, void *,
                                                           uint32_t);
uint32_t BB_THISCALL bb_faction_group_remove(void *, void *);
uint32_t bb_faction_group_restore_c(void *, uint32_t, uint32_t);
uint32_t bb_faction_attack_c(void *, uint32_t, uint32_t, uint32_t);
uint32_t bb_faction_recruit_c(void *, uint32_t, uint32_t);
uint32_t BB_THISCALL bb_faction_can_build(void *, uint32_t);
uint32_t BB_THISCALL bb_faction_build_allowed(void *, uint32_t);
uint32_t BB_THISCALL bb_faction_construction(void *, uint32_t,
                                                          void *, uint32_t);
void *bb_entity_build(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
void bb_faction_group_restore_entry(void);
void bb_faction_recruit_entry(void);
void bb_faction_attack_entry(void);
uint32_t BB_THISCALL bb_faction_group_cancel(void *, void *);
uint32_t BB_THISCALL bb_faction_flag(void *, uint32_t, uint32_t);
uint32_t BB_THISCALL bb_entity_can_work(void *, uint32_t);
uint32_t BB_THISCALL bb_entity_can_train(void *, uint32_t);
uint32_t bb_entity_activity_c(void *, uint32_t, void *, uint32_t);
void bb_entity_activity_entry(void);
uint32_t BB_THISCALL bb_entity_feedback_delay(void *, uint32_t,
                                                            uint32_t);
uint32_t BB_THISCALL bb_entity_feedback_queue(void *, uint32_t,
                                                            void *);
uint32_t BB_THISCALL bb_entity_partner_link(void *, void *);
uint32_t BB_THISCALL bb_entity_partner_begin(void *, void *);
uint32_t BB_THISCALL bb_entity_partner_name(void *, void *);
uint32_t BB_THISCALL bb_entity_partner_clear(void *);
void *BB_THISCALL bb_entity_partner_find(void *);
void *BB_THISCALL bb_entity_home_nearest(void *);
void *BB_THISCALL bb_entity_child_site(void *);
uint32_t bb_placement_route(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                            uint32_t);
uint32_t bb_placement_reachable(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_placement_preview_clear(void);
uint32_t bb_placement_connect(uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_placement_probe(uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_placement_clear(uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_entity_attack_tile_c(void *, uint32_t, uint32_t, uint32_t);
void bb_entity_attack_tile_entry(void);
uint32_t BB_THISCALL bb_entity_stat(void *, uint32_t, uint32_t);
uint32_t BB_THISCALL bb_entity_cast(void *, uint32_t, uint32_t,
                                               uint32_t);
uint32_t BB_THISCALL bb_command_enqueue(void *, const void *);
uint64_t bb_command_pop_c(void *, uint32_t);
void bb_command_pop_entry(void);
uint32_t BB_THISCALL bb_command_collect(void *);
uint32_t BB_THISCALL bb_command_ready(void *);
uint32_t BB_THISCALL bb_command_pump(void *);
uint32_t BB_THISCALL bb_command_tick(void *);
uint32_t BB_THISCALL bb_command_sync(void *, uint32_t);
uint32_t BB_THISCALL bb_command_dispatch(void *, const void *);

uint32_t BB_THISCALL bb_entity_contact(void *, void *);
uint32_t bb_entity_reveal_c(void *, uint32_t, uint32_t);
void bb_entity_reveal_entry(void);
void *BB_THISCALL bb_entity_city_name(void *);
uint32_t BB_THISCALL bb_entity_treatment_fee(void *, uint32_t);
uint32_t BB_THISCALL bb_entity_event_feedback(void *, uint32_t,
                                                          uint32_t);

uint32_t bb_entity_kind_tick_c(void *, uint32_t);
void bb_entity_kind_tick_entry(void);
uint32_t BB_THISCALL bb_entity_pool_tick(void *);
uint32_t BB_THISCALL bb_entity_decision_tick(void *);
uint32_t BB_THISCALL bb_entity_work_auto(void *);
uint32_t BB_THISCALL bb_entity_work_retry(void *);
uint32_t bb_entity_human_ai_c(void *, uint32_t);
void bb_entity_human_ai_entry(void);
void bb_entity_main_entry(void);
uint32_t BB_THISCALL bb_entity_area_damage_entry(
    void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
void bb_entity_attack_begin_entry(void);
void bb_entity_attack_tick_entry(void);
uint32_t BB_THISCALL bb_entity_damage_entry(void *, uint32_t,
                                                       uint32_t);
void bb_entity_subtype_clear_entry(void);
void bb_entity_attack_range_entry(void);
void bb_entity_target_moved_entry(void);
uint32_t BB_THISCALL bb_entity_target_building(void *);
uint32_t BB_THISCALL bb_entity_target_last(void *);
uint32_t BB_THISCALL bb_entity_target_weak(void *);
uint32_t BB_THISCALL bb_entity_target_strong(void *);
uint32_t BB_THISCALL bb_entity_target_strong_alt(void *);
uint32_t BB_THISCALL bb_entity_enemy_command(void *);
uint32_t BB_THISCALL bb_entity_enemy_evade(void *);
uint32_t BB_THISCALL bb_entity_doctor_find(void *);
uint32_t BB_THISCALL bb_entity_mage_offense(void *);
uint32_t BB_THISCALL bb_entity_mage_support(void *);
uint32_t BB_THISCALL bb_entity_idle_action(void *);
void *BB_THISCALL bb_entity_work_wood(void *);
void *BB_THISCALL bb_entity_work_stone(void *);
void *BB_THISCALL bb_entity_work_hunt(void *);
void *BB_THISCALL bb_entity_need_food(void *);
void *BB_THISCALL bb_entity_need_water(void *);
void *BB_THISCALL bb_entity_order_site(void *);
void *BB_THISCALL bb_entity_work_repair(void *);
void *BB_THISCALL bb_entity_work_clear(void *);
void *BB_THISCALL bb_entity_work_field(void *);
uint32_t BB_THISCALL bb_entity_pair_allowed(void *, void *, uint32_t);
uint32_t BB_THISCALL bb_entity_clown_action(void *);
uint32_t BB_THISCALL bb_entity_morale_shock(void *);
uint32_t BB_THISCALL bb_entity_home_idle(void *);

struct bb_entity_adjust_result {
  uint32_t eax, edx, ecx, flags;
};
void bb_entity_adjust_c(void *, uint32_t, uint32_t, uint32_t, uint32_t,
                        struct bb_entity_adjust_result *);
uint32_t bb_entity_adjust(void *, uint32_t, uint32_t, uint32_t);
void bb_entity_adjust_health_entry(void);
void bb_entity_adjust_109_entry(void);
void bb_entity_adjust_113_entry(void);
void bb_entity_adjust_111_entry(void);
void bb_entity_adjust_10d_entry(void);
void bb_entity_adjust_10f_entry(void);
void bb_entity_adjust_100_entry(void);
void bb_entity_adjust_101_entry(void);
void bb_entity_adjust_11b_entry(void);
void bb_entity_adjust_resources_entry(void);
uint32_t BB_THISCALL bb_entity_can_consume(void *, uint32_t,
                                                         uint32_t);
uint32_t BB_THISCALL bb_entity_can_steal(void *, uint32_t,
                                                       uint32_t);
uint32_t BB_THISCALL bb_entity_consume(void *, uint32_t,
                                                     uint32_t);
uint32_t BB_THISCALL bb_entity_steal(void *, uint32_t, uint32_t);
uint32_t bb_entity_trade_c(void *, uint32_t, uint32_t, uint32_t, uint32_t,
                           uint32_t);
uint32_t bb_entity_transfer_c(void *, uint32_t, uint32_t, uint32_t);
void bb_entity_trade_entry(void);
void bb_entity_transfer_entry(void);

uint32_t BB_THISCALL bb_display_lock(void *);
uint32_t BB_THISCALL bb_display_unlock(void *);
uint32_t BB_THISCALL bb_display_clear(void *);
uint32_t BB_THISCALL bb_display_flip(void *);
uint32_t BB_THISCALL bb_display_reset(void *);
uint32_t BB_THISCALL bb_display_wait(void *, int32_t);
void bb_display_viewport_entry(void);
void bb_display_intersect_entry(void);
uint32_t BB_THISCALL bb_display_viewport(void *, int32_t, int32_t,
                                                       int32_t, int32_t);
uint32_t BB_THISCALL
bb_display_intersect(void *, int32_t, int32_t, int32_t, int32_t);
void bb_display_wait_entry(void);
void bb_display_restore_entry(void);
uint32_t BB_THISCALL bb_display_scroll(void *, int32_t, int32_t);
uint32_t BB_THISCALL bb_display_shade(void *, int32_t, int32_t,
                                                    int32_t, int32_t);
uint32_t BB_THISCALL bb_display_shade_rect(void *, int32_t,
                                                         int32_t, int32_t,
                                                         int32_t,
                                                         const unsigned char *);

void bb_loading_progress(void);
void bb_loading_progress_entry(void);
void bb_cd_play_entry(void);
void bb_cd_stop_entry(void);
void bb_cd_resume_entry(void);
uint32_t bb_save_encode(const unsigned char *, uint32_t, uint32_t, void *);
uint32_t bb_save_decode(unsigned char *, uint32_t, uint32_t, void *, uint32_t);
void bb_entity_relocate_entry(void);
uint32_t bb_entity_relocate(uint32_t *, uint32_t, uint32_t *);
void bb_archive_view_entry(void);
void bb_archive_read_entry(void);
void bb_archive_clear_entry(void);
void bb_archive_directory_entry(void);
void bb_archive_find_name_entry(void);
void bb_archive_find_index_entry(void);
void bb_archive_open_entry(void);
void bb_archive_close_entry(void);
void bb_archive_member_entry(void);
void bb_archive_release_entry(void);
void bb_archive_seek_entry(void);
void bb_archive_construct_entry(void);
void bb_archive_name_entry(void);
void bb_archive_destroy_entry(void);
void bb_archive_view_destroy_entry(void);
void bb_archive_missing_entry(void);
void bb_archive_view_delete_entry(void);
void bb_archive_buffer_entry(void);
void bb_archive_add_entry(void);
void bb_archive_replace_entry(void);
void bb_archive_refresh_entry(void);
void bb_entity_sprite_entry(void);
uint32_t bb_entity_translate_c(void *, uint32_t, uint32_t, uint32_t);
void bb_entity_translate_entry(void);
void bb_sprite_construct_entry(void);
uint32_t BB_THISCALL bb_sprite_blank(void *, int32_t, int32_t,
                                                   uint32_t);
uint32_t BB_THISCALL bb_sprite_decode(void *);
uint32_t BB_THISCALL bb_sprite_load(void *, const char *);
uint32_t bb_surface_pixel(void *, uint32_t);
uint32_t bb_surface_color_key(void *, uint32_t);
void *bb_bitmap_load(void *, const char *, int32_t, int32_t, uint32_t *,
                     uint32_t *);
uint32_t BB_THISCALL bb_sprite_bitmap(void *, const char *,
                                                    uint32_t);
void *BB_THISCALL bb_sprite_create_blank(void *, int32_t, int32_t,
                                                       uint32_t);
void *BB_THISCALL bb_sprite_create_file(void *, const char *,
                                                      uint32_t);
uint32_t BB_THISCALL bb_sprite_remap(void *);
uint32_t BB_THISCALL bb_sprite_restore_mfb(void *);
uint32_t BB_THISCALL bb_sprite_restore(void *);
void bb_sprite_remove_entry(void);
void bb_sprite_destroy_entry(void);
void bb_font_construct_entry(void);
void bb_font_destroy_entry(void);
void bb_font_spacing_entry(void);
void *bb_palette_load(void *, const char *);
void bb_palette_ready_entry(void);
uint32_t BB_THISCALL bb_palette_ready(void *);
uint32_t BB_THISCALL
bb_palette_nearest(void *, uint32_t, unsigned char, unsigned char);
uint32_t BB_THISCALL bb_palette_remap(void *);
uint32_t BB_THISCALL bb_palette_shade(void *);
uint32_t BB_THISCALL bb_palette_mix(void *);
uint32_t BB_THISCALL bb_palette_reflect(void *);
uint32_t BB_THISCALL bb_palette_fade(void *, uint32_t);
uint32_t BB_THISCALL bb_palette_install(void *, const char *);
uint32_t BB_THISCALL bb_palette_black(void *);
uint32_t BB_THISCALL bb_palette_cycle(void *);
uint32_t BB_THISCALL bb_font_load(void *, void *, const char *);
uint32_t BB_THISCALL bb_font_draw(void *, const char *, int32_t,
                                                int32_t, uint32_t);
uint32_t BB_THISCALL bb_font_draw_wrapped(void *, const char *,
                                                        int32_t, int32_t,
                                                        int32_t, uint32_t,
                                                        void *);
uint32_t BB_THISCALL bb_font_bounds(void *, const char *, int32_t,
                                                  int32_t, uint32_t, int32_t *,
                                                  int32_t *, int32_t *,
                                                  int32_t *);
uint32_t BB_THISCALL
bb_font_bounds_wrapped(void *, const char *, int32_t, int32_t, int32_t,
                       uint32_t, int32_t *, int32_t *, int32_t *, int32_t *);
uint32_t BB_THISCALL bb_sprite_blend(void *, void *, uint32_t,
                                                   uint32_t, void *,
                                                   const int32_t *, uint32_t,
                                                   unsigned char);
uint32_t BB_THISCALL bb_sprite_draw_region(void *, int32_t,
                                                         int32_t, uint32_t,
                                                         uint32_t, int32_t,
                                                         int32_t, void *);
uint32_t bb_bitmap_copy(void *, void *, int32_t, int32_t, uint32_t, uint32_t);
uint32_t BB_THISCALL bb_sprite_clear(void *);
uint32_t BB_THISCALL bb_sprite_copy_back(void *);
void bb_sprite_anchor_entry(void);
void bb_sprite_center_entry(void);
uint32_t BB_THISCALL bb_sprite_draw(void *, int32_t, int32_t,
                                                  int32_t, uint32_t);
uint32_t BB_THISCALL bb_sprite_cursor_erase(void *);
uint32_t BB_THISCALL bb_sprite_cursor_draw(void *, int32_t,
                                                         int32_t, uint32_t);

/* Reconstructed QMIXER PCM and accumulator entries. */
uint32_t bb_qmix_decoder_base_constructor_entry(void);
uint32_t bb_qmix_decoder_base_destructor_entry(void);
uint32_t bb_qmix_pcm_remaining_entry(void);
uint32_t bb_qmix_pcm_progress_entry(uint32_t);
uint32_t bb_qmix_pcm_mono8_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_pcm_mono16_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_pcm_stereo8_entry(uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_pcm_stereo16_entry(uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_pcm_reset_entry(void);
uint32_t bb_qmix_pcm_advance_entry(uint32_t);
uint32_t bb_qmix_resample_mono8_entry(uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_resample_stereo8_entry(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_resample_mono16_entry(uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_resample_stereo16_entry(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_split_stereo8_entry(uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_split_stereo16_entry(uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_angle_quantize_entry(uint32_t);
uint32_t bb_qmix_angle_expand_entry(uint32_t);
uint32_t bb_qmix_ramp_constructor_entry(void);
uint32_t bb_qmix_ramp_set_entry(uint32_t, uint32_t);
uint32_t bb_qmix_ramp_mono8_entry(uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_ramp_mono16_entry(uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_accumulator_feed_entry(uint32_t);
uint32_t bb_qmix_accumulator_mono8_entry(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_accumulator_mono16_entry(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_accumulator_zero_extend_entry(uint32_t);
uint32_t bb_qmix_accumulator_merge_entry(uint32_t);
uint32_t bb_qmix_accumulator_stereo_output_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_accumulator_flush_entry(uint32_t);
uint32_t bb_qmix_accumulator_silent_entry(void);
uint32_t bb_qmix_accumulator_clear_entry(void);
uint32_t bb_qmix_mix_mono8_store_entry(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_mix_mono8_add_entry(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_mix_mono16_store_entry(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_mix_mono16_add_entry(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
double bb_qmix_accumulator_magnitude_entry(uint32_t, uint32_t);
uint32_t bb_qmix_mix_float_add_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_mix_float_scale_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_clip_stereo16_entry(uint32_t, uint32_t, uint32_t, uint32_t);

/* Reconstructed QMIXER session and render pipeline entries. */
uint32_t bb_qmix_session_options_entry(uint32_t, uint32_t);
uint32_t bb_qmix_session_render_entry(uint32_t, uint32_t);
uint32_t bb_qmix_listener_invalidate_entry(void);
uint32_t bb_qmix_listener_position_entry(uint32_t);
uint32_t bb_qmix_listener_orientation_entry(uint32_t, uint32_t);
uint32_t bb_qmix_listener_velocity_entry(uint32_t);
uint32_t bb_qmix_listener_default_entry(uint32_t);
uint32_t bb_qmix_listener_speed_entry(uint32_t);
uint32_t bb_qmix_listener_gain_entry(uint32_t);
uint32_t bb_qmix_session_default_entry(void);
uint32_t bb_qmix_listener_position_get_entry(uint32_t);
uint32_t bb_qmix_listener_orientation_get_entry(uint32_t, uint32_t);
uint32_t bb_qmix_listener_velocity_get_entry(uint32_t);
uint32_t bb_qmix_listener_speed_get_entry(uint32_t);
double bb_qmix_listener_gain_get_entry(void);
uint32_t bb_qmix_listener_transform_entry(uint32_t, uint32_t);
uint32_t bb_qmix_timing_begin_entry(void);
uint32_t bb_qmix_timing_end_entry(uint32_t);
uint32_t bb_qmix_session_option_bits_entry(uint32_t, uint32_t);
uint32_t bb_qmix_device_invalidate_entry(void);
uint32_t bb_qmix_device_mode_entry(uint32_t);
uint32_t bb_qmix_device_flags_entry(uint32_t);
uint32_t bb_qmix_device_pan_table_entry(void);
uint32_t bb_qmix_device_volume_entry(uint32_t);
uint32_t bb_qmix_device_position_entry(uint32_t);
uint32_t bb_qmix_device_rate_entry(uint32_t);
uint32_t bb_qmix_device_distance_entry(uint32_t);
uint32_t bb_qmix_device_refresh_entry(uint32_t);
uint32_t bb_qmix_device_transition_entry(uint32_t);
uint32_t bb_qmix_device_gains_entry(uint32_t);
uint32_t bb_qmix_device_mono16_entry(uint32_t, uint32_t);
uint32_t bb_qmix_device_dual16_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_device_stereo16_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_device_mono8_entry(uint32_t, uint32_t);
uint32_t bb_qmix_device_dual8_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_device_stereo8_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_device_flush_entry(void);
uint32_t bb_qmix_mixer_default_entry(uint32_t);
uint32_t bb_qmix_mixer_noop_entry(void);
uint32_t bb_qmix_mixer_base_delete_entry(uint32_t);
uint32_t bb_qmix_mixer_delete_entry(uint32_t);
uint32_t bb_qmix_mixer_configure_entry(uint32_t, uint32_t);
uint32_t bb_qmix_mixer_flags_entry(uint32_t);
uint32_t bb_qmix_mixer_output_entry(uint32_t, uint32_t);
uint32_t bb_qmix_mixer_effects_entry(uint32_t);
uint32_t bb_qmix_mixer_flush_entry(uint32_t);
uint32_t bb_qmix_mixer_silent_entry(void);
uint32_t bb_qmix_mixer_clear_entry(void);
uint32_t bb_qmix_filter_base_delete_entry(uint32_t);
uint32_t bb_qmix_filter_default_entry(uint32_t);
uint32_t bb_qmix_filter_process_entry(uint32_t, uint32_t);
uint32_t bb_qmix_filter_noop_entry(uint32_t);
uint32_t bb_qmix_filter_attach_entry(uint32_t);
uint32_t bb_qmix_filter_detach_entry(uint32_t);
uint32_t bb_qmix_filter_silent_entry(void);
uint32_t bb_qmix_filter_clear_entry(void);
uint32_t bb_qmix_filter_samples_entry(uint32_t, uint32_t, uint32_t, uint32_t);

/* Reconstructed QMIXER integer and short sample mixing entries. */
uint32_t bb_qmix_integer_backend_name_entry(void);
uint32_t bb_qmix_integer_ramp_constructor_entry(void);
uint32_t bb_qmix_integer_ramp_set_entry(uint32_t, uint32_t);
uint32_t bb_qmix_integer_ramp_mono8_entry(uint32_t, uint32_t, uint32_t,
                                          uint32_t);
uint32_t bb_qmix_integer_ramp_mono16_entry(uint32_t, uint32_t, uint32_t,
                                           uint32_t);
uint32_t bb_qmix_integer_accumulator_delete_entry(uint32_t);
uint32_t bb_qmix_integer_accumulator_feed_entry(uint32_t);
uint32_t bb_qmix_integer_accumulator_mono8_entry(uint32_t, uint32_t, uint32_t,
                                                 uint32_t, uint32_t);
uint32_t bb_qmix_integer_accumulator_mono16_entry(uint32_t, uint32_t, uint32_t,
                                                  uint32_t, uint32_t);
uint32_t bb_qmix_integer_accumulator_zero_extend_entry(uint32_t);
uint32_t bb_qmix_integer_accumulator_merge_entry(uint32_t);
uint32_t bb_qmix_integer_accumulator_stereo_output_entry(uint32_t, uint32_t,
                                                         uint32_t);
uint32_t bb_qmix_integer_accumulator_flush_entry(uint32_t);
uint32_t bb_qmix_integer_accumulator_silent_entry(void);
uint32_t bb_qmix_integer_accumulator_clear_entry(void);
uint32_t bb_qmix_integer_mix_mono8_store_entry(uint32_t, uint32_t, uint32_t,
                                               uint32_t, uint32_t);
uint32_t bb_qmix_integer_mix_mono8_add_entry(uint32_t, uint32_t, uint32_t,
                                             uint32_t, uint32_t);
uint32_t bb_qmix_integer_mix_mono16_store_entry(uint32_t, uint32_t, uint32_t,
                                                uint32_t, uint32_t);
uint32_t bb_qmix_integer_mix_mono16_add_entry(uint32_t, uint32_t, uint32_t,
                                              uint32_t, uint32_t);
uint32_t bb_qmix_integer_accumulator_magnitude_entry(uint32_t, uint32_t);
uint32_t bb_qmix_integer_mix_add_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_integer_clip_stereo16_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_short_backend_name_entry(void);
uint32_t bb_qmix_short_ramp_constructor_entry(void);
uint32_t bb_qmix_short_ramp_set_entry(uint32_t, uint32_t);
uint32_t bb_qmix_short_ramp_mono8_entry(uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_short_ramp_mono16_entry(uint32_t, uint32_t, uint32_t,
                                         uint32_t);
uint32_t bb_qmix_short_accumulator_delete_entry(uint32_t);
uint32_t bb_qmix_short_accumulator_feed_entry(uint32_t);
uint32_t bb_qmix_short_accumulator_mono8_entry(uint32_t, uint32_t, uint32_t,
                                               uint32_t, uint32_t);
uint32_t bb_qmix_short_accumulator_mono16_entry(uint32_t, uint32_t, uint32_t,
                                                uint32_t, uint32_t);
uint32_t bb_qmix_short_accumulator_zero_extend_entry(uint32_t);
uint32_t bb_qmix_short_accumulator_merge_entry(uint32_t);
uint32_t bb_qmix_short_accumulator_stereo_output_entry(uint32_t, uint32_t,
                                                       uint32_t);
uint32_t bb_qmix_short_accumulator_flush_entry(uint32_t);
uint32_t bb_qmix_short_accumulator_silent_entry(void);
uint32_t bb_qmix_short_accumulator_clear_entry(void);
uint32_t bb_qmix_short_mix_mono8_store_entry(uint32_t, uint32_t, uint32_t,
                                             uint32_t, uint32_t);
uint32_t bb_qmix_short_mix_mono8_add_entry(uint32_t, uint32_t, uint32_t,
                                           uint32_t, uint32_t);
uint32_t bb_qmix_short_mix_mono16_store_entry(uint32_t, uint32_t, uint32_t,
                                              uint32_t, uint32_t);
uint32_t bb_qmix_short_mix_mono16_add_entry(uint32_t, uint32_t, uint32_t,
                                            uint32_t, uint32_t);
uint32_t bb_qmix_short_mix_add_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_short_accumulator_magnitude_entry(uint32_t, uint32_t);
uint32_t bb_qmix_short_find_signal_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_short_interleave_stereo16_entry(uint32_t, uint32_t, uint32_t,
                                                 uint32_t);

/* Reconstructed QMIXER object construction, factories and cleanup entries. */
uint32_t bb_qmix_device_constructor_entry(uint32_t, uint32_t);
uint32_t bb_qmix_device_unwind_base_entry(void);
uint32_t bb_qmix_device_base_destructor_entry(void);
uint32_t bb_qmix_device_base_delete_entry(uint32_t);
uint32_t bb_qmix_mixer_constructor_entry(uint32_t);
uint32_t bb_qmix_mixer_unwind_base_entry(void);
uint32_t bb_qmix_mixer_base_destructor_entry(void);
uint32_t bb_qmix_mixer_destructor_entry(void);
uint32_t bb_qmix_mixer_cleanup_base_entry(void);
uint32_t bb_qmix_mixer_device_create_entry(void);
uint32_t bb_qmix_mixer_device_free_failed_entry(void);
uint32_t bb_qmix_integer_mixer_create_entry(void);
uint32_t bb_qmix_integer_mixer_free_failed_entry(void);
uint32_t bb_qmix_short_mixer_create_entry(void);
uint32_t bb_qmix_short_mixer_free_failed_entry(void);
uint32_t bb_qmix_float_mixer_create_entry(void);
uint32_t bb_qmix_float_mixer_free_failed_entry(void);
uint32_t bb_qmix_float_accumulator_constructor_entry(uint32_t);
uint32_t bb_qmix_float_accumulator_unwind_base_entry(void);
uint32_t bb_qmix_accumulator_base_destructor_entry(void);
uint32_t bb_qmix_accumulator_base_delete_entry(uint32_t);
uint32_t bb_qmix_float_accumulator_delete_entry(uint32_t);
uint32_t bb_qmix_float_accumulator_destructor_entry(void);
uint32_t bb_qmix_float_accumulator_cleanup_base_entry(void);
uint32_t bb_qmix_float_ramp_create_entry(void);
uint32_t bb_qmix_float_ramp_free_failed_entry(void);
uint32_t bb_qmix_float_accumulator_create_entry(uint32_t);
uint32_t bb_qmix_float_accumulator_free_failed_entry(void);
uint32_t bb_qmix_integer_accumulator_create_entry(uint32_t);
uint32_t bb_qmix_integer_accumulator_free_failed_entry(void);
uint32_t bb_qmix_integer_accumulator_constructor_entry(uint32_t);
uint32_t bb_qmix_integer_accumulator_unwind_base_entry(void);
uint32_t bb_qmix_integer_accumulator_destructor_entry(void);
uint32_t bb_qmix_integer_accumulator_cleanup_base_entry(void);
uint32_t bb_qmix_integer_ramp_create_entry(void);
uint32_t bb_qmix_integer_ramp_free_failed_entry(void);
uint32_t bb_qmix_short_accumulator_constructor_entry(uint32_t);
uint32_t bb_qmix_short_accumulator_unwind_base_entry(void);
uint32_t bb_qmix_short_accumulator_destructor_entry(void);
uint32_t bb_qmix_short_accumulator_cleanup_base_entry(void);
uint32_t bb_qmix_short_ramp_create_entry(void);
uint32_t bb_qmix_short_ramp_free_failed_entry(void);
uint32_t bb_qmix_short_accumulator_create_entry(uint32_t);
uint32_t bb_qmix_short_accumulator_free_failed_entry(void);
uint32_t bb_qmix_float_backend_name_entry(void);
uint32_t bb_qmix_filter_create_entry(uint32_t);
uint32_t bb_qmix_filter_free_failed_entry(void);
uint32_t bb_qmix_filter_constructor_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_filter_unwind_base_entry(void);
uint32_t bb_qmix_filter_unwind_intermediate_entry(void);
uint32_t bb_qmix_filter_delete_entry(uint32_t);
uint32_t bb_qmix_filter_intermediate_destructor_entry(void);
uint32_t bb_qmix_filter_intermediate_cleanup_base_entry(void);
uint32_t bb_qmix_filter_intermediate_delete_entry(uint32_t);
uint32_t bb_qmix_filter_intermediate_delete_base_entry(void);
uint32_t bb_qmix_filter_base_destructor_entry(void);
uint32_t bb_qmix_filter_destructor_entry(void);
uint32_t bb_qmix_filter_cleanup_intermediate_entry(void);

/* Reconstructed QMIXER short recursive and convolution filter entries entries. */
uint32_t bb_qmix_short_iir_create_entry(uint32_t);
uint32_t bb_qmix_short_iir_free_failed_entry(void);
uint32_t bb_qmix_short_iir_constructor_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_short_iir_unwind_base_entry(void);
uint32_t bb_qmix_short_iir_unwind_intermediate_entry(void);
uint32_t bb_qmix_short_iir_delete_entry(uint32_t);
uint32_t bb_qmix_short_iir_destructor_entry(void);
uint32_t bb_qmix_short_iir_cleanup_intermediate_entry(void);
uint32_t bb_qmix_short_iir_process_entry(uint32_t, uint32_t);
uint32_t bb_qmix_short_iir_silent_entry(void);
uint32_t bb_qmix_short_iir_clear_entry(void);
uint32_t bb_qmix_short_iir_packed_samples_entry(uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_short_iir_scalar_samples_entry(uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_short_biquad_create_entry(uint32_t);
uint32_t bb_qmix_short_biquad_free_failed_entry(void);
uint32_t bb_qmix_short_biquad_constructor_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_short_biquad_unwind_base_entry(void);
uint32_t bb_qmix_short_biquad_unwind_intermediate_entry(void);
uint32_t bb_qmix_short_biquad_delete_entry(uint32_t);
uint32_t bb_qmix_short_biquad_destructor_entry(void);
uint32_t bb_qmix_short_biquad_cleanup_intermediate_entry(void);
uint32_t bb_qmix_short_biquad_process_entry(uint32_t, uint32_t);
uint32_t bb_qmix_short_biquad_scalar_samples_entry(uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_short_biquad_packed_samples_entry(uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_short_biquad_silent_entry(void);
uint32_t bb_qmix_short_biquad_clear_entry(void);
uint32_t bb_qmix_short_fir_create_entry(uint32_t);
uint32_t bb_qmix_short_fir_free_failed_entry(void);
uint32_t bb_qmix_short_fir_constructor_entry(uint32_t, uint32_t);
uint32_t bb_qmix_short_fir_unwind_base_entry(void);
uint32_t bb_qmix_short_fir_unwind_intermediate_entry(void);
uint32_t bb_qmix_short_fir_delete_entry(uint32_t);
uint32_t bb_qmix_short_fir_destructor_entry(void);
uint32_t bb_qmix_short_fir_cleanup_intermediate_entry(void);
uint32_t bb_qmix_short_fir_process_entry(uint32_t, uint32_t);
uint32_t bb_qmix_short_fir_dot_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_short_fir_silent_entry(void);
uint32_t bb_qmix_short_fir_clear_entry(void);

/* Reconstructed QMIXER float and wide-integer filter entries. */
uint32_t bb_qmix_float_biquad_create_entry(uint32_t);
uint32_t bb_qmix_float_biquad_free_failed_entry(void);
uint32_t bb_qmix_float_biquad_constructor_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_float_biquad_unwind_base_entry(void);
uint32_t bb_qmix_float_biquad_unwind_intermediate_entry(void);
uint32_t bb_qmix_float_biquad_delete_entry(uint32_t);
uint32_t bb_qmix_float_biquad_destructor_entry(void);
uint32_t bb_qmix_float_biquad_cleanup_intermediate_entry(void);
uint32_t bb_qmix_float_biquad_process_entry(uint32_t, uint32_t);
uint32_t bb_qmix_float_biquad_gain_attach_entry(uint32_t);
uint32_t bb_qmix_float_biquad_gain_detach_entry(uint32_t);
uint32_t bb_qmix_float_biquad_silent_entry(void);
uint32_t bb_qmix_float_biquad_clear_entry(void);
uint32_t bb_qmix_float_biquad_samples_entry(uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_float_fir_create_entry(uint32_t);
uint32_t bb_qmix_float_fir_free_failed_entry(void);
uint32_t bb_qmix_float_fir_constructor_entry(uint32_t);
uint32_t bb_qmix_float_fir_unwind_base_entry(void);
uint32_t bb_qmix_float_fir_unwind_intermediate_entry(void);
uint32_t bb_qmix_float_fir_delete_entry(uint32_t);
uint32_t bb_qmix_float_fir_destructor_entry(void);
uint32_t bb_qmix_float_fir_cleanup_intermediate_entry(void);
uint32_t bb_qmix_float_fir_process_entry(uint32_t, uint32_t);
double bb_qmix_float_fir_dot_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_float_fir_silent_entry(void);
uint32_t bb_qmix_float_fir_clear_entry(void);
uint32_t bb_qmix_integer_iir_create_entry(uint32_t);
uint32_t bb_qmix_integer_iir_free_failed_entry(void);
uint32_t bb_qmix_integer_iir_constructor_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_integer_iir_unwind_base_entry(void);
uint32_t bb_qmix_integer_iir_unwind_intermediate_entry(void);
uint32_t bb_qmix_integer_iir_delete_entry(uint32_t);
uint32_t bb_qmix_integer_iir_destructor_entry(void);
uint32_t bb_qmix_integer_iir_cleanup_intermediate_entry(void);
uint32_t bb_qmix_integer_iir_process_entry(uint32_t, uint32_t);
uint32_t bb_qmix_integer_iir_silent_entry(void);
uint32_t bb_qmix_integer_iir_clear_entry(void);
uint32_t bb_qmix_integer_iir_samples_entry(uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_integer_biquad_create_entry(uint32_t);
uint32_t bb_qmix_integer_biquad_free_failed_entry(void);
uint32_t bb_qmix_integer_biquad_constructor_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_integer_biquad_unwind_base_entry(void);
uint32_t bb_qmix_integer_biquad_unwind_intermediate_entry(void);
uint32_t bb_qmix_integer_biquad_delete_entry(uint32_t);
uint32_t bb_qmix_integer_biquad_destructor_entry(void);
uint32_t bb_qmix_integer_biquad_cleanup_intermediate_entry(void);
uint32_t bb_qmix_integer_biquad_process_entry(uint32_t, uint32_t);
uint32_t bb_qmix_integer_biquad_silent_entry(void);
uint32_t bb_qmix_integer_biquad_clear_entry(void);
uint32_t bb_qmix_integer_biquad_samples_entry(uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_integer_fir_create_entry(uint32_t);
uint32_t bb_qmix_integer_fir_free_failed_entry(void);
uint32_t bb_qmix_integer_fir_constructor_entry(uint32_t, uint32_t);
uint32_t bb_qmix_integer_fir_unwind_base_entry(void);
uint32_t bb_qmix_integer_fir_unwind_intermediate_entry(void);
uint32_t bb_qmix_integer_fir_delete_entry(uint32_t);
uint32_t bb_qmix_integer_fir_destructor_entry(void);
uint32_t bb_qmix_integer_fir_cleanup_intermediate_entry(void);
uint32_t bb_qmix_integer_fir_process_entry(uint32_t, uint32_t);
uint32_t bb_qmix_integer_fir_silent_entry(void);
uint32_t bb_qmix_integer_fir_clear_entry(void);
uint32_t bb_qmix_integer_fir_dot_entry(uint32_t, uint32_t, uint32_t);

/* Reconstructed QMIXER float and wide-integer delay entries. */
uint32_t bb_qmix_float_delay_create_entry(uint32_t);
uint32_t bb_qmix_float_delay_free_failed_entry(void);
uint32_t bb_qmix_float_delay_constructor_entry(uint32_t);
uint32_t bb_qmix_float_delay_unwind_base_entry(void);
uint32_t bb_qmix_float_delay_unwind_intermediate_entry(void);
uint32_t bb_qmix_float_delay_delete_entry(uint32_t);
uint32_t bb_qmix_delay_base_destructor_entry(void);
uint32_t bb_qmix_delay_base_cleanup_entry(void);
uint32_t bb_qmix_delay_base_delete_entry(uint32_t);
uint32_t bb_qmix_delay_base_delete_cleanup_entry(void);
uint32_t bb_qmix_float_delay_destructor_entry(void);
uint32_t bb_qmix_float_delay_cleanup_intermediate_entry(void);
uint32_t bb_qmix_float_delay_process_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_float_delay_multiply_entry(uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_float_delay_add_entry(uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_float_delay_silent_entry(void);
uint32_t bb_qmix_float_delay_clear_entry(void);
uint32_t bb_qmix_integer_delay_create_entry(uint32_t);
uint32_t bb_qmix_integer_delay_free_failed_entry(void);
uint32_t bb_qmix_integer_delay_constructor_entry(uint32_t, uint32_t);
uint32_t bb_qmix_integer_delay_unwind_base_entry(void);
uint32_t bb_qmix_integer_delay_unwind_intermediate_entry(void);
uint32_t bb_qmix_integer_delay_delete_entry(uint32_t);
uint32_t bb_qmix_integer_delay_destructor_entry(void);
uint32_t bb_qmix_integer_delay_cleanup_intermediate_entry(void);
uint32_t bb_qmix_integer_delay_process_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_integer_delay_silent_entry(void);
uint32_t bb_qmix_integer_delay_clear_entry(void);
uint32_t bb_qmix_integer_delay_multiply_entry(uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_integer_delay_add_entry(uint32_t, uint32_t, uint32_t, uint32_t);

/* Reconstructed QMIXER decoder sources and device teardown entries. */
uint32_t bb_qmix_decoder_base_delete_entry(uint32_t);
uint32_t bb_qmix_pcm_constructor_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_pcm_constructor_cleanup_entry(void);
uint32_t bb_qmix_pcm_delete_entry(uint32_t);
uint32_t bb_qmix_pcm_destructor_entry(void);
uint32_t bb_qmix_pcm_destructor_cleanup_entry(void);
uint32_t bb_qmix_codec_constructor_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_codec_constructor_cleanup_entry(void);
uint32_t bb_qmix_codec_delete_entry(uint32_t);
uint32_t bb_qmix_codec_destructor_entry(void);
uint32_t bb_qmix_codec_destructor_cleanup_entry(void);
uint32_t bb_qmix_codec_remaining_entry(void);
uint32_t bb_qmix_codec_progress_entry(uint32_t);
uint32_t bb_qmix_codec_mono8_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_codec_mono16_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_codec_stereo8_entry(uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_codec_stereo16_entry(uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_codec_refill_entry(void);
uint32_t bb_qmix_codec_reset_entry(void);
uint32_t bb_qmix_codec_advance_entry(uint32_t);
uint32_t bb_qmix_callback_constructor_entry(uint32_t, uint32_t);
uint32_t bb_qmix_callback_constructor_cleanup_entry(void);
uint32_t bb_qmix_callback_progress_entry(uint32_t);
uint32_t bb_qmix_callback_delete_entry(uint32_t);
uint32_t bb_qmix_callback_destructor_entry(void);
uint32_t bb_qmix_callback_destructor_cleanup_entry(void);
uint32_t bb_qmix_callback_remaining_entry(void);
uint32_t bb_qmix_callback_mono8_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_callback_mono16_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_callback_stereo8_entry(uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_callback_stereo16_entry(uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_callback_reset_entry(void);
uint32_t bb_qmix_callback_advance_entry(uint32_t);
uint32_t bb_qmix_callback_refill_entry(void);
uint32_t bb_qmix_decoder_create_entry(uint32_t);
uint32_t bb_qmix_callback_free_failed_entry(void);
uint32_t bb_qmix_pcm_free_failed_entry(void);
uint32_t bb_qmix_codec_free_failed_entry(void);
uint32_t bb_qmix_device_delete_entry(uint32_t);
uint32_t bb_qmix_device_destructor_entry(void);
uint32_t bb_qmix_device_destructor_cleanup_entry(void);

/* Reconstructed QMIXER wave loading and cleanup entries. */
uint32_t bb_qmix_wave_load_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_wave_data_cleanup_entry(void);
uint32_t bb_qmix_wave_format_cleanup_entry(void);
uint32_t bb_qmix_wave_stream_cleanup_entry(void);
uint32_t bb_qmix_wave_resource_cleanup_entry(void);
uint32_t bb_qmix_wave_alloc_cleanup_entry(void);
uint32_t bb_qmix_wave_descriptor_cleanup_entry(void);
uint32_t bb_qmix_wave_stream_close_entry(void);
uint32_t bb_qmix_wave_resource_close_entry(void);
uint32_t bb_qmix_wave_descriptor_close_entry(void);
uint32_t bb_qmix_wave_pointer_free_entry(void);
uint32_t bb_qmix_wave_stream_read_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_wave_message_pump_entry(void);
uint32_t bb_qmix_wave_free_entry(uint32_t);

/* Reconstructed QMIXER session startup, channel lifecycle and backend entries. */
uint32_t bb_qmix_session_constructor_entry(void);
uint32_t bb_qmix_session_constructor_cleanup_entry(void);
uint32_t bb_qmix_session_delete_entry(uint32_t);
uint32_t bb_qmix_session_destructor_entry(void);
uint32_t bb_qmix_session_destructor_cleanup_entry(void);
uint32_t bb_qmix_session_initialize_entry(uint32_t);
uint32_t bb_qmix_session_channel_free_failed_entry(void);
uint32_t bb_qmix_session_backend_select_entry(uint32_t);
uint32_t bb_qmix_channel_constructor_entry(uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_channel_pending_pool_cleanup_entry(void);
uint32_t bb_qmix_channel_pending_list_cleanup_entry(void);
uint32_t bb_qmix_channel_pool_cleanup_entry(void);
uint32_t bb_qmix_channel_list_cleanup_entry(void);
uint32_t bb_qmix_channel_constructor_cleanup_entry(void);
uint32_t bb_qmix_channel_delete_entry(uint32_t);
uint32_t bb_qmix_list_destructor_entry(void);
uint32_t bb_qmix_list_pool_cleanup_entry(void);
uint32_t bb_qmix_channel_destructor_entry(void);
uint32_t bb_qmix_channel_pending_cleanup_entry(void);
uint32_t bb_qmix_channel_voices_cleanup_entry(void);
uint32_t bb_qmix_channel_base_cleanup_entry(void);
uint32_t bb_qmix_pool_release_entry(void);
uint32_t bb_qmix_profile_integer_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_session_base_constructor_entry(void);
uint32_t bb_qmix_session_base_pool_cleanup_entry(void);
uint32_t bb_qmix_session_base_list_cleanup_entry(void);
uint32_t bb_qmix_session_base_delete_entry(uint32_t);
uint32_t bb_qmix_session_base_destructor_entry(void);
uint32_t bb_qmix_session_base_destructor_cleanup_entry(void);
uint32_t bb_qmix_session_validate_format_entry(uint32_t);
uint32_t bb_qmix_session_configure_format_entry(uint32_t, uint32_t);
uint32_t bb_qmix_session_allocate_channels_entry(uint32_t);
uint32_t bb_qmix_session_default_backend_entry(uint32_t);
uint32_t bb_qmix_session_default_orientation_entry(uint32_t, uint32_t);
uint32_t bb_qmix_session_default_position_entry(uint32_t);
uint32_t bb_qmix_session_default_velocity_entry(uint32_t);
uint32_t bb_qmix_session_channel_get_entry(uint32_t);
uint32_t bb_qmix_session_default_speed_entry(uint32_t);
uint32_t bb_qmix_session_default_rate_entry(uint32_t);
uint32_t bb_qmix_session_default_profile_entry(void);
uint32_t bb_qmix_session_active_channels_entry(void);
uint32_t bb_qmix_session_default_profile_end_entry(uint32_t);
uint32_t bb_qmix_session_activate_entry(uint32_t);
uint32_t bb_qmix_session_default_orientation_get_entry(uint32_t, uint32_t);
uint32_t bb_qmix_session_default_position_get_entry(uint32_t);
uint32_t bb_qmix_session_default_velocity_get_entry(uint32_t);
double bb_qmix_session_default_speed_get_entry(void);
uint32_t bb_qmix_session_default_gain_entry(uint32_t);
uint32_t bb_qmix_session_flush_entry(uint32_t, uint32_t);
uint32_t bb_qmix_session_open_channels_entry(uint32_t, uint32_t);
uint32_t bb_qmix_session_set_pan_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_cpu_id_available_entry(void);
uint32_t bb_qmix_cpu_mmx_available_entry(void);
uint32_t bb_qmix_integer_factory_initialize_entry(void);
uint32_t bb_qmix_short_factory_initialize_entry(void);
uint32_t bb_qmix_float_factory_initialize_entry(void);

/* Reconstructed QMIXER session exports, channel lifecycle and backend entries. */
uint32_t bb_qmix_export_activate_entry(uint32_t, uint32_t);
uint32_t bb_qmix_export_close_channel_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_export_close_session_entry(uint32_t);
uint32_t bb_qmix_export_configure_init_entry(uint32_t);
uint32_t bb_qmix_export_enable_entry(uint32_t, uint32_t);
uint32_t bb_qmix_export_enable_channel_entry(uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_export_flush_channel_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_export_free_wave_entry(uint32_t, uint32_t);
uint32_t bb_qmix_export_get_direct_sound_entry(uint32_t, uint32_t);
uint32_t bb_qmix_export_get_distance_mapping_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_export_get_error_text_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_export_get_frequency_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_export_get_info_entry(uint32_t);
uint32_t bb_qmix_export_get_last_error_entry(void);
uint32_t bb_qmix_export_get_listener_orientation_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_export_get_listener_position_entry(uint32_t, uint32_t);
uint32_t bb_qmix_export_get_listener_velocity_entry(uint32_t, uint32_t);
uint32_t bb_qmix_export_get_options_entry(uint32_t, uint32_t);
uint32_t bb_qmix_export_get_pan_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_export_get_pan_rate_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_export_get_position_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_export_get_room_size_entry(uint32_t, uint32_t);
uint32_t bb_qmix_export_get_source_cone_entry(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_export_get_source_position_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_export_get_source_velocity_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_export_get_speed_of_sound_entry(uint32_t, uint32_t);
uint32_t bb_qmix_export_get_play_position_entry(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_export_get_volume_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_export_init_entry(void);
uint32_t bb_qmix_export_is_channel_done_entry(uint32_t, uint32_t);
uint32_t bb_qmix_export_open_channel_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_export_open_wave_entry(uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_export_pause_channel_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_export_play_entry(uint32_t);
uint32_t bb_qmix_export_pump_entry(void);
uint32_t bb_qmix_export_restart_channel_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_export_set_distance_mapping_entry(uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_export_set_frequency_entry(uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_export_set_listener_orientation_entry(uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_export_set_listener_position_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_export_set_listener_velocity_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_export_set_options_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_export_set_pan_entry(uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_export_set_pan_rate_entry(uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_export_set_position_entry(uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_export_set_remix_buffer_size_entry(uint32_t, uint32_t);
uint32_t bb_qmix_export_set_room_size_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_export_set_source_cone_entry(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_export_set_source_position_entry(uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_export_set_source_velocity_entry(uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_export_set_speaker_placement_entry(uint32_t, uint32_t);
uint32_t bb_qmix_export_set_speed_of_sound_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_export_set_volume_entry(uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_export_start_profile_entry(uint32_t);
uint32_t bb_qmix_export_stop_channel_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_export_stop_profile_entry(uint32_t, uint32_t, uint32_t);
uint32_t bb_qmix_export_unlock_entry(void);
uint32_t bb_qmix_session_play_entry(uint32_t);
uint32_t bb_qmix_session_play_cleanup_entry(void);
uint32_t bb_qmix_export_activate_unlock_entry(void);
uint32_t bb_qmix_export_close_channel_unlock_entry(void);
uint32_t bb_qmix_export_enable_channel_unlock_entry(void);
uint32_t bb_qmix_export_flush_channel_unlock_entry(void);
uint32_t bb_qmix_export_free_wave_unlock_entry(void);
uint32_t bb_qmix_export_get_direct_sound_unlock_entry(void);
uint32_t bb_qmix_export_get_distance_mapping_unlock_entry(void);
uint32_t bb_qmix_export_get_frequency_unlock_entry(void);
uint32_t bb_qmix_export_get_listener_orientation_unlock_entry(void);
uint32_t bb_qmix_export_get_listener_position_unlock_entry(void);
uint32_t bb_qmix_export_get_listener_velocity_unlock_entry(void);
uint32_t bb_qmix_export_get_options_unlock_entry(void);
uint32_t bb_qmix_export_get_pan_rate_unlock_entry(void);
uint32_t bb_qmix_export_get_position_unlock_entry(void);
uint32_t bb_qmix_export_get_room_size_unlock_entry(void);
uint32_t bb_qmix_export_get_source_cone_unlock_entry(void);
uint32_t bb_qmix_export_get_source_position_unlock_entry(void);
uint32_t bb_qmix_export_get_source_velocity_unlock_entry(void);
uint32_t bb_qmix_export_get_speed_of_sound_unlock_entry(void);
uint32_t bb_qmix_export_get_play_position_unlock_entry(void);
uint32_t bb_qmix_export_get_volume_unlock_entry(void);
uint32_t bb_qmix_export_is_channel_done_unlock_entry(void);
uint32_t bb_qmix_export_open_channel_unlock_entry(void);
uint32_t bb_qmix_export_pause_channel_unlock_entry(void);
uint32_t bb_qmix_export_play_unlock_entry(void);
uint32_t bb_qmix_export_restart_channel_unlock_entry(void);
uint32_t bb_qmix_export_set_distance_mapping_unlock_entry(void);
uint32_t bb_qmix_export_set_frequency_unlock_entry(void);
uint32_t bb_qmix_export_set_listener_orientation_unlock_entry(void);
uint32_t bb_qmix_export_set_listener_position_unlock_entry(void);
uint32_t bb_qmix_export_set_listener_velocity_unlock_entry(void);
uint32_t bb_qmix_export_set_options_unlock_entry(void);
uint32_t bb_qmix_export_set_pan_unlock_entry(void);
uint32_t bb_qmix_export_set_pan_rate_unlock_entry(void);
uint32_t bb_qmix_export_set_position_unlock_entry(void);
uint32_t bb_qmix_export_set_remix_buffer_size_unlock_entry(void);
uint32_t bb_qmix_export_set_room_size_unlock_entry(void);
uint32_t bb_qmix_export_set_source_cone_unlock_entry(void);
uint32_t bb_qmix_export_set_source_position_unlock_entry(void);
uint32_t bb_qmix_export_set_source_velocity_unlock_entry(void);
uint32_t bb_qmix_export_set_speaker_placement_unlock_entry(void);
uint32_t bb_qmix_export_set_speed_of_sound_unlock_entry(void);
uint32_t bb_qmix_export_set_volume_unlock_entry(void);
uint32_t bb_qmix_export_start_profile_unlock_entry(void);
uint32_t bb_qmix_export_stop_channel_unlock_entry(void);
uint32_t bb_qmix_export_stop_profile_unlock_entry(void);
uint32_t bb_qmix_voice_select_channel_entry(uint32_t, uint32_t);
uint32_t bb_qmix_session_oldest_channel_entry(void);
uint32_t bb_qmix_session_soonest_channel_entry(void);
uint32_t bb_qmix_session_idle_channel_entry(void);
uint32_t bb_qmix_session_play_free_failed_entry(void);
uint32_t bb_qmix_session_play_release_entry(void);

#endif
