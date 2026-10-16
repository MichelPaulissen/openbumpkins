/* SPDX-License-Identifier: GPL-3.0-only */
#include "crt_locale_models.h"
#include "address.h"
#include "crt_bootstrap.h"
#include "crt_locale.h"
#define A(n) BB_U32(r[3] + 8u + (n) * 4u)
extern void bb_crt_locale_map_c(uint32_t *);
extern void bb_crt_locale_type_c(uint32_t *);
extern void bb_crt_locale_compare_c(uint32_t *);
static void locale_2(uint32_t *r) {
  r[7] = bb_crt_locale_mbtowc_entry((uint16_t *)(uintptr_t)A(0),
                                    (const char *)(uintptr_t)A(1), A(2));
}
static void locale_3(uint32_t *r) {
  r[7] = bb_crt_locale_wcstombs_entry((char *)(uintptr_t)A(0),
                                      (const uint16_t *)(uintptr_t)A(1), A(2));
}
static void locale_4(uint32_t *r) {
  r[7] = bb_address_encode(
      bb_crt_locale_getenv_entry((const char *)(uintptr_t)A(0)));
}
static void locale_5(uint32_t *r) {
  r[7] = bb_crt_locale_collate_entry((const char *)(uintptr_t)A(0),
                                     (const char *)(uintptr_t)A(1), A(2));
}
static void locale_6(uint32_t *r) {
  r[7] = bb_crt_locale_convert_environment_entry();
}
static void locale_8(uint32_t *r) {
  r[7] = bb_crt_locale_setenv_entry((char *)(uintptr_t)A(0), A(1));
}
static void locale_9(uint32_t *r) {
  r[7] = bb_crt_locale_findenv_entry((const char *)(uintptr_t)A(0), A(1));
}
static void locale_10(uint32_t *r) {
  r[7] = bb_address_encode(
      bb_crt_locale_copy_environment_entry((const void *)(uintptr_t)A(0)));
}
static void locale_11(uint32_t *r) {
  r[7] = bb_address_encode(
      bb_crt_locale_find_character_entry((const char *)(uintptr_t)A(0), A(1)));
}
static void locale_12(uint32_t *r) { r[7] = bb_crt_boot_lead_entry(A(0)); }
static void locale_13(uint32_t *r) {
  r[7] = bb_crt_boot_byte_type_entry(A(0), A(1), A(2));
}
static void locale_14(uint32_t *r) {
  r[7] = bb_address_encode(bb_crt_boot_environment_entry());
}
static void locale_15(uint32_t *r) {
  r[7] = bb_address_encode(bb_crt_boot_arguments_entry());
}
static void locale_16(uint32_t *r) {
  bb_crt_boot_parse_arguments_entry(
      (const uint8_t *)(uintptr_t)A(0), (uint32_t *)(uintptr_t)A(1),
      (uint8_t *)(uintptr_t)A(2), (uint32_t *)(uintptr_t)A(3),
      (uint32_t *)(uintptr_t)A(4));
}
static void locale_17(uint32_t *r) {
  r[7] = bb_address_encode(bb_crt_boot_capture_environment_entry());
}
static void locale_18(uint32_t *r) { r[7] = bb_crt_boot_codepage_entry(A(0)); }
static void locale_19(uint32_t *r) {
  r[7] = bb_crt_boot_system_codepage_entry(A(0));
}
static void locale_20(uint32_t *r) {
  r[7] = bb_crt_boot_codepage_locale_entry(A(0));
}
static void locale_21(uint32_t *r) {
  (void)r;
  bb_crt_boot_clear_codepage_entry();
}
static void locale_22(uint32_t *r) {
  (void)r;
  bb_crt_boot_initialize_codepage_entry();
}
static const struct bb_model_target targets[] = {
    {0x0047c1c0u, bb_crt_locale_map_c, 7, 0, 0},  /* bb_crt_locale_map_entry */
    {0x0047e2c0u, bb_crt_locale_type_c, 6, 0, 0}, /* bb_crt_locale_type_entry */
    {0x0047e460u, locale_2, 3, 0, 0}, /* bb_crt_locale_mbtowc_entry */
    {0x00480140u, locale_3, 3, 0, 0}, /* bb_crt_locale_wcstombs_entry */
    {0x00480510u, locale_4, 1, 0, 0}, /* bb_crt_locale_getenv_entry */
    {0x00480d90u, locale_5, 3, 0, 0}, /* bb_crt_locale_collate_entry */
    {0x00480e00u, locale_6, 0, 0,
     0}, /* bb_crt_locale_convert_environment_entry */
    {0x00480ed0u, bb_crt_locale_compare_c, 7, 0,
     0},                              /* bb_crt_locale_compare_entry */
    {0x004813a0u, locale_8, 2, 0, 0}, /* bb_crt_locale_setenv_entry */
    {0x00481710u, locale_9, 2, 0, 0}, /* bb_crt_locale_findenv_entry */
    {0x004817b0u, locale_10, 1, 0,
     0}, /* bb_crt_locale_copy_environment_entry */
    {0x004818b0u, locale_11, 2, 0, 0}, /* bb_crt_locale_find_character_entry */
    {0x00475650u, locale_12, 1, 0, 0}, /* bb_crt_boot_lead_entry */
    {0x00475670u, locale_13, 3, 0, 0}, /* bb_crt_boot_byte_type_entry */
    {0x004756e0u, locale_14, 0, 0, 0}, /* bb_crt_boot_environment_entry */
    {0x00475820u, locale_15, 0, 0, 0}, /* bb_crt_boot_arguments_entry */
    {0x004758f0u, locale_16, 5, 0, 0}, /* bb_crt_boot_parse_arguments_entry */
    {0x00475cb0u, locale_17, 0, 0,
     0}, /* bb_crt_boot_capture_environment_entry */
    {0x00475f10u, locale_18, 1, 0, 0}, /* bb_crt_boot_codepage_entry */
    {0x00476240u, locale_19, 1, 0, 0}, /* bb_crt_boot_system_codepage_entry */
    {0x004762d0u, locale_20, 1, 0, 0}, /* bb_crt_boot_codepage_locale_entry */
    {0x00476370u, locale_21, 0, 0, 0}, /* bb_crt_boot_clear_codepage_entry */
    {0x004763f0u, locale_22, 0, 0,
     0}, /* bb_crt_boot_initialize_codepage_entry */
};
const struct bb_model_target *bb_crt_locale_model_find(void *unused,
                                                       uint32_t id) {
  (void)unused;
  for (unsigned n = 0; n < sizeof targets / sizeof *targets; n++)
    if (targets[n].identity == id)
      return &targets[n];
  return NULL;
}
