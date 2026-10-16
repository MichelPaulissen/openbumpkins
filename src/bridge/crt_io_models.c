/* SPDX-License-Identifier: GPL-3.0-only */
#include "crt_io_models.h"
#include "address.h"
#include "crt_bootstrap.h"
#include "crt_descriptor.h"
#include "crt_locale.h"
#include "crt_output.h"
#include "crt_stream.h"
#include "crt_input.h"
#define A(n) BB_U32(r[3] + 8u + (n) * 4u)
extern void bb_crt_sprintf_c(uint32_t *);
extern void bb_crt_snprintf_c(uint32_t *);
extern void bb_crt_output_c(uint32_t *);
extern void bb_crt_sscanf_c(uint32_t *);
extern void bb_crt_input_c(uint32_t *);
static void scan_hex(uint32_t *r) { r[7] = bb_crt_scan_hex_entry(A(0)); }
static void scan_read(uint32_t *r) {
  r[7] = bb_crt_scan_read_entry((void *)(uintptr_t)A(0));
}
static void scan_unread(uint32_t *r) {
  bb_crt_scan_unread_entry(A(0), (void *)(uintptr_t)A(1));
}
static void scan_space(uint32_t *r) {
  r[7] = bb_crt_scan_space_entry((uint32_t *)(uintptr_t)A(0),
                                  (void *)(uintptr_t)A(1));
}
static void io_2(uint32_t *r) {
  r[7] = bb_crt_vsnprintf_entry((char *)(uintptr_t)A(0), A(1),
                                (const char *)(uintptr_t)A(2),
                                (const void *)(uintptr_t)A(3));
}
static void io_4(uint32_t *r) {
  bb_crt_write_char_entry(A(0), (void *)(uintptr_t)A(1),
                          (uint32_t *)(uintptr_t)A(2));
}
static void io_5(uint32_t *r) {
  bb_crt_write_repeat_entry(A(0), A(1), (void *)(uintptr_t)A(2),
                            (uint32_t *)(uintptr_t)A(3));
}
static void io_6(uint32_t *r) {
  bb_crt_write_string_entry((const char *)(uintptr_t)A(0), A(1),
                            (void *)(uintptr_t)A(2),
                            (uint32_t *)(uintptr_t)A(3));
}
static void io_7(uint32_t *r) {
  r[7] = bb_crt_argument_int_entry((uint32_t *)(uintptr_t)A(0));
}
static void io_8(uint32_t *r) {
  uint64_t value = bb_crt_argument_wide_entry((uint32_t *)(uintptr_t)A(0));
  r[7] = value;
  r[5] = value >> 32;
}
static void io_9(uint32_t *r) {
  r[7] = bb_crt_argument_short_entry((uint32_t *)(uintptr_t)A(0));
}
static void io_10(uint32_t *r) {
  r[7] = bb_crt_stream_close_entry((void *)(uintptr_t)A(0));
}
static void io_11(uint32_t *r) {
  r[7] = bb_address_encode(bb_crt_stream_shared_open_entry(
      (const char *)(uintptr_t)A(0), (const char *)(uintptr_t)A(1), A(2)));
}
static void io_12(uint32_t *r) {
  r[7] = bb_address_encode(bb_crt_stream_open_entry(
      (const char *)(uintptr_t)A(0), (const char *)(uintptr_t)A(1)));
}
static void io_13(uint32_t *r) {
  r[7] = bb_crt_stream_tell_entry((void *)(uintptr_t)A(0));
}
static void io_14(uint32_t *r) {
  r[7] = bb_crt_stream_seek_entry((void *)(uintptr_t)A(0), A(1), A(2));
}
static void io_15(uint32_t *r) {
  r[7] = bb_crt_stream_read_entry((void *)(uintptr_t)A(0), A(1), A(2),
                                  (void *)(uintptr_t)A(3));
}
static void io_16(uint32_t *r) {
  r[7] = bb_crt_stream_write_entry((const void *)(uintptr_t)A(0), A(1), A(2),
                                   (void *)(uintptr_t)A(3));
}
static void io_17(uint32_t *r) {
  r[7] = bb_crt_stream_getc_entry((void *)(uintptr_t)A(0));
}
static void io_18(uint32_t *r) {
  r[7] = bb_crt_stream_write_char_entry(A(0), (void *)(uintptr_t)A(1));
}
static void io_19(uint32_t *r) {
  bb_crt_stream_free_buffer_entry((void *)(uintptr_t)A(0));
}
static void io_20(uint32_t *r) {
  r[7] = bb_crt_stream_flush_entry((void *)(uintptr_t)A(0));
}
static void io_21(uint32_t *r) {
  r[7] = bb_crt_stream_flush_buffer_entry((void *)(uintptr_t)A(0));
}
static void io_22(uint32_t *r) { r[7] = bb_crt_stream_flush_all_entry(); }
static void io_23(uint32_t *r) { r[7] = bb_crt_stream_flush_table_entry(A(0)); }
static void io_24(uint32_t *r) {
  r[7] = bb_address_encode(bb_crt_stream_configure_entry(
      (const char *)(uintptr_t)A(0), (const char *)(uintptr_t)A(1), A(2),
      (void *)(uintptr_t)A(3)));
}
static void io_25(uint32_t *r) {
  r[7] = bb_address_encode(bb_crt_stream_acquire_entry());
}
static void io_26(uint32_t *r) {
  r[7] = bb_crt_stream_fill_entry((void *)(uintptr_t)A(0));
}
static void io_27(uint32_t *r) {
  bb_crt_stream_allocate_buffer_entry((void *)(uintptr_t)A(0));
}
static void io_28(uint32_t *r) {
  r[7] = bb_crt_stream_ungetc_entry(A(0), (void *)(uintptr_t)A(1));
}
static void io_29(uint32_t *r) { r[7] = bb_crt_descriptor_close_entry(A(0)); }
static void io_30(uint32_t *r) { r[7] = bb_crt_descriptor_initialize_entry(); }
static void io_31(uint32_t *r) {
  r[7] = bb_crt_descriptor_seek_entry(A(0), A(1), A(2));
}
static void io_32(uint32_t *r) {
  r[7] =
      bb_crt_descriptor_write_entry(A(0), (const void *)(uintptr_t)A(1), A(2));
}
static void io_33(uint32_t *r) {
  r[7] = bb_crt_descriptor_read_entry(A(0), (void *)(uintptr_t)A(1), A(2));
}
static void io_34(uint32_t *r) {
  r[7] = bb_crt_descriptor_terminal_entry(A(0));
}
static void io_35(uint32_t *r) { bb_crt_descriptor_error_entry(A(0)); }
static void io_36(uint32_t *r) { r[7] = bb_crt_descriptor_acquire_entry(); }
static void io_37(uint32_t *r) {
  r[7] = bb_crt_descriptor_set_handle_entry(A(0), A(1));
}
static void io_38(uint32_t *r) {
  r[7] = bb_crt_descriptor_free_handle_entry(A(0));
}
static void io_39(uint32_t *r) {
  r[7] = bb_crt_descriptor_get_handle_entry(A(0));
}
static void io_40(uint32_t *r) { r[7] = bb_crt_descriptor_commit_entry(A(0)); }
static void io_41(uint32_t *r) {
  r[7] = bb_crt_descriptor_open_entry((const char *)(uintptr_t)A(0), A(1), A(2),
                                      A(3));
}
static void io_42(uint32_t *r) {
  r[7] = bb_crt_descriptor_resize_entry(A(0), A(1));
}
static void io_43(uint32_t *r) {
  r[7] = bb_crt_descriptor_mode_entry(A(0), A(1));
}
static void io_44(uint32_t *r) {
  r[7] = bb_crt_locale_wctomb_entry((char *)(uintptr_t)A(0), A(1));
}
static void io_45(uint32_t *r) {
  (void)r;
  bb_crt_boot_stdio_entry();
}
static void io_46(uint32_t *r) {
  (void)r;
  bb_crt_boot_end_stdio_entry();
}
static void io_47(uint32_t *r) { r[7] = bb_crt_boot_close_streams_entry(); }
static const struct bb_model_target targets[] = {
    {0x0046d300u, bb_crt_sscanf_c, 2 | BB_MODEL_VARIADIC, 576, 0},
    {0x00473ef0u, bb_crt_input_c, 3, 512, 0},
    {0x004752a0u, scan_hex, 1, 0, 0},
    {0x00475310u, scan_read, 1, 0, 0},
    {0x00475360u, scan_unread, 2, 0, 0},
    {0x00475390u, scan_space, 2, 0, 0},
    {0x0046bae0u, bb_crt_sprintf_c, 2 | BB_MODEL_VARIADIC, 0,
     0}, /* bb_crt_sprintf_entry */
    {0x0047a140u, bb_crt_snprintf_c, 3 | BB_MODEL_VARIADIC, 0,
     0},                                     /* bb_crt_snprintf_entry */
    {0x0047a230u, io_2, 4, 0, 0},            /* bb_crt_vsnprintf_entry */
    {0x0046ffa0u, bb_crt_output_c, 3, 0, 0}, /* bb_crt_output_entry */
    {0x00470d30u, io_4, 3, 0, 0},            /* bb_crt_write_char_entry */
    {0x00470db0u, io_5, 4, 0, 0},            /* bb_crt_write_repeat_entry */
    {0x00470e00u, io_6, 4, 0, 0},            /* bb_crt_write_string_entry */
    {0x00470e60u, io_7, 1, 0, 0},            /* bb_crt_argument_int_entry */
    {0x00470e80u, io_8, 1, 0, 0},            /* bb_crt_argument_wide_entry */
    {0x00470eb0u, io_9, 1, 0, 0},            /* bb_crt_argument_short_entry */
    {0x0046bbd0u, io_10, 1, 0, 0},           /* bb_crt_stream_close_entry */
    {0x0046bcc0u, io_11, 3, 0, 0}, /* bb_crt_stream_shared_open_entry */
    {0x0046bdd0u, io_12, 2, 0, 0}, /* bb_crt_stream_open_entry */
    {0x0046c8f0u, io_13, 1, 0, 0}, /* bb_crt_stream_tell_entry */
    {0x0046cba0u, io_14, 3, 0, 0}, /* bb_crt_stream_seek_entry */
    {0x0046cf40u, io_15, 4, 0, 0}, /* bb_crt_stream_read_entry */
    {0x0046cd20u, io_16, 4, 0, 0}, /* bb_crt_stream_write_entry */
    {0x0046fb60u, io_17, 1, 0, 0}, /* bb_crt_stream_getc_entry */
    {0x0046fd00u, io_18, 2, 0, 0}, /* bb_crt_stream_write_char_entry */
    {0x00471720u, io_19, 1, 0, 0}, /* bb_crt_stream_free_buffer_entry */
    {0x004717b0u, io_20, 1, 0, 0}, /* bb_crt_stream_flush_entry */
    {0x00471830u, io_21, 1, 0, 0}, /* bb_crt_stream_flush_buffer_entry */
    {0x004718f0u, io_22, 0, 0, 0}, /* bb_crt_stream_flush_all_entry */
    {0x00471910u, io_23, 1, 0, 0}, /* bb_crt_stream_flush_table_entry */
    {0x00471a10u, io_24, 4, 0, 0}, /* bb_crt_stream_configure_entry */
    {0x00471dd0u, io_25, 0, 0, 0}, /* bb_crt_stream_acquire_entry */
    {0x00473770u, io_26, 1, 0, 0}, /* bb_crt_stream_fill_entry */
    {0x00479f60u, io_27, 1, 0, 0}, /* bb_crt_stream_allocate_buffer_entry */
    {0x0047e650u, io_28, 2, 0, 0}, /* bb_crt_stream_ungetc_entry */
    {0x00471610u, io_29, 1, 0, 0}, /* bb_crt_descriptor_close_entry */
    {0x00472bc0u, io_30, 0, 0, 0}, /* bb_crt_descriptor_initialize_entry */
    {0x00472ef0u, io_31, 3, 0, 0}, /* bb_crt_descriptor_seek_entry */
    {0x00473470u, io_32, 3, 0, 0}, /* bb_crt_descriptor_write_entry */
    {0x00473950u, io_33, 3, 0, 0}, /* bb_crt_descriptor_read_entry */
    {0x0047a020u, io_34, 1, 0, 0}, /* bb_crt_descriptor_terminal_entry */
    {0x0047a690u, io_35, 1, 0, 0}, /* bb_crt_descriptor_error_entry */
    {0x0047a750u, io_36, 0, 0, 0}, /* bb_crt_descriptor_acquire_entry */
    {0x0047a8a0u, io_37, 2, 0, 0}, /* bb_crt_descriptor_set_handle_entry */
    {0x0047a9a0u, io_38, 1, 0, 0}, /* bb_crt_descriptor_free_handle_entry */
    {0x0047aac0u, io_39, 1, 0, 0}, /* bb_crt_descriptor_get_handle_entry */
    {0x0047ab40u, io_40, 1, 0, 0}, /* bb_crt_descriptor_commit_entry */
    {0x0047ac20u, io_41, 4, 0, 0}, /* bb_crt_descriptor_open_entry */
    {0x0047e9f0u, io_42, 2, 0, 0}, /* bb_crt_descriptor_resize_entry */
    {0x00480740u, io_43, 2, 0, 0}, /* bb_crt_descriptor_mode_entry */
    {0x0047a070u, io_44, 2, 0, 0}, /* bb_crt_locale_wctomb_entry */
    {0x00471ef0u, io_45, 0, 0, 0}, /* bb_crt_boot_stdio_entry */
    {0x00472050u, io_46, 0, 0, 0}, /* bb_crt_boot_end_stdio_entry */
    {0x0047b1c0u, io_47, 0, 0, 0}, /* bb_crt_boot_close_streams_entry */
};
const struct bb_model_target *bb_crt_io_model_find(void *unused, uint32_t id) {
  (void)unused;
  for (unsigned n = 0; n < sizeof targets / sizeof *targets; n++)
    if (targets[n].identity == id)
      return &targets[n];
  return NULL;
}
