/* SPDX-License-Identifier: GPL-3.0-only */
#include "allocation_models.h"
#include "crt_bootstrap.h"
#include "crt_debug_heap.h"
#include "crt_heap.h"
#include "game_calls.h"
static uint32_t arg(uint32_t *r, unsigned n) {
  return BB_U32(r[3] + 8u + n * 4u);
}
extern void bb_crt_debug_print_data_c(uint32_t *);
static void entry_0(uint32_t *r) { r[7] = bb_crt_heap_initialize_entry(); }
static void entry_1(uint32_t *r) { r[7] = bb_crt_heap_retry_entry(arg(r, 0)); }
static void entry_2(uint32_t *r) {
  r[7] = bb_address_encode(bb_crt_heap_malloc_entry(arg(r, 0)));
}
static void entry_3(uint32_t *r) {
  r[7] =
      bb_address_encode(bb_crt_heap_malloc_retry_entry(arg(r, 0), arg(r, 1)));
}
static void entry_4(uint32_t *r) {
  r[7] = bb_address_encode(bb_crt_heap_allocate_entry(arg(r, 0)));
}
static void entry_5(uint32_t *r) { r[7] = bb_crt_heap_permission_entry(); }
static void entry_6(uint32_t *r) {
  r[7] = bb_address_encode(
      bb_crt_heap_expand_entry((void *)(uintptr_t)arg(r, 0), arg(r, 1)));
}
static void entry_7(uint32_t *r) {
  r[7] = bb_address_encode(
      bb_crt_heap_reallocate_entry((void *)(uintptr_t)arg(r, 0), arg(r, 1)));
}
static void entry_8(uint32_t *r) {
  bb_crt_heap_free_entry((void *)(uintptr_t)arg(r, 0));
}
static void entry_9(uint32_t *r) { r[7] = bb_crt_heap_check_entry(); }
static void entry_10(uint32_t *r) {
  r[7] = bb_address_encode(bb_crt_heap_region_create_entry());
}
static void entry_11(uint32_t *r) {
  bb_crt_heap_region_release_entry((void *)(uintptr_t)arg(r, 0));
}
static void entry_12(uint32_t *r) { bb_crt_heap_decommit_entry(arg(r, 0)); }
static void entry_13(uint32_t *r) {
  r[7] = bb_address_encode(bb_crt_heap_find_entry(
      (void *)(uintptr_t)arg(r, 0), (void *)(uintptr_t)arg(r, 1),
      (void *)(uintptr_t)arg(r, 2)));
}
static void entry_14(uint32_t *r) {
  bb_crt_heap_block_free_entry((void *)(uintptr_t)arg(r, 0),
                               (void *)(uintptr_t)arg(r, 1),
                               (void *)(uintptr_t)arg(r, 2));
}
static void entry_15(uint32_t *r) {
  r[7] = bb_address_encode(bb_crt_heap_block_allocate_entry(arg(r, 0)));
}
static void entry_16(uint32_t *r) {
  r[7] = bb_address_encode(bb_crt_heap_page_allocate_entry(
      (void *)(uintptr_t)arg(r, 0), arg(r, 1), arg(r, 2)));
}
static void entry_17(uint32_t *r) {
  r[7] = bb_crt_heap_block_resize_entry(
      (void *)(uintptr_t)arg(r, 0), (void *)(uintptr_t)arg(r, 1),
      (void *)(uintptr_t)arg(r, 2), arg(r, 3));
}
static void entry_18(uint32_t *r) { r[7] = bb_crt_heap_pool_check_entry(); }
static void entry_19(uint32_t *r) {
  r[7] = bb_address_encode(bb_crt_malloc_entry(arg(r, 0)));
}
static void entry_20(uint32_t *r) {
  r[7] = bb_address_encode(bb_crt_malloc_debug_entry(
      arg(r, 0), arg(r, 1), (void *)(uintptr_t)arg(r, 2), arg(r, 3)));
}
static void entry_21(uint32_t *r) {
  r[7] = bb_address_encode(bb_crt_malloc_retry_entry(arg(r, 0), arg(r, 1)));
}
static void entry_22(uint32_t *r) {
  r[7] = bb_address_encode(
      bb_crt_malloc_debug_retry_entry(arg(r, 0), arg(r, 1), arg(r, 2),
                                      (void *)(uintptr_t)arg(r, 3), arg(r, 4)));
}
static void entry_23(uint32_t *r) {
  r[7] = bb_address_encode(bb_crt_debug_allocate_entry(
      arg(r, 0), arg(r, 1), (void *)(uintptr_t)arg(r, 2), arg(r, 3)));
}
static void entry_24(uint32_t *r) {
  r[7] = bb_address_encode(bb_crt_calloc_entry(arg(r, 0), arg(r, 1)));
}
static void entry_25(uint32_t *r) {
  r[7] = bb_address_encode(
      bb_crt_calloc_debug_entry(arg(r, 0), arg(r, 1), arg(r, 2),
                                (void *)(uintptr_t)arg(r, 3), arg(r, 4)));
}
static void entry_26(uint32_t *r) {
  r[7] = bb_address_encode(bb_crt_reallocate_debug_entry(
      (void *)(uintptr_t)arg(r, 0), arg(r, 1), arg(r, 2),
      (void *)(uintptr_t)arg(r, 3), arg(r, 4)));
}
static void entry_27(uint32_t *r) {
  r[7] = bb_address_encode(bb_crt_debug_resize_entry(
      (void *)(uintptr_t)arg(r, 0), arg(r, 1), arg(r, 2),
      (void *)(uintptr_t)arg(r, 3), arg(r, 4), arg(r, 5)));
}
static void entry_28(uint32_t *r) {
  bb_crt_free_entry((void *)(uintptr_t)arg(r, 0));
}
static void entry_29(uint32_t *r) {
  bb_crt_free_debug_entry((void *)(uintptr_t)arg(r, 0), arg(r, 1));
}
static void entry_30(uint32_t *r) {
  r[7] = bb_crt_size_debug_entry((void *)(uintptr_t)arg(r, 0), arg(r, 1));
}
static void entry_31(uint32_t *r) {
  r[7] = bb_crt_debug_check_bytes_entry((void *)(uintptr_t)arg(r, 0), arg(r, 1),
                                        arg(r, 2));
}
static void entry_32(uint32_t *r) { r[7] = bb_crt_debug_check_memory_entry(); }
static void entry_33(uint32_t *r) {
  r[7] = bb_crt_debug_set_flags_entry(arg(r, 0));
}
static void entry_34(uint32_t *r) {
  r[7] = bb_crt_debug_valid_pointer_entry((void *)(uintptr_t)arg(r, 0),
                                          arg(r, 1), arg(r, 2));
}
static void entry_35(uint32_t *r) {
  r[7] = bb_crt_debug_valid_heap_entry((void *)(uintptr_t)arg(r, 0));
}
static void entry_36(uint32_t *r) {
  bb_crt_debug_checkpoint_entry((void *)(uintptr_t)arg(r, 0));
}
static void entry_37(uint32_t *r) {
  bb_crt_debug_dump_objects_entry((void *)(uintptr_t)arg(r, 0));
}
static void entry_38(uint32_t *r) { bb_crt_debug_print_data_c(r); }
static void entry_39(uint32_t *r) { r[7] = bb_crt_debug_dump_leaks_entry(); }
static void entry_40(uint32_t *r) {
  r[7] = bb_address_encode(bb_crt_boot_new_entry(arg(r, 0)));
}
static void entry_41(uint32_t *r) {
  bb_crt_boot_delete_entry((void *)(uintptr_t)arg(r, 0));
}
void bb_crt_debug_print_data_entry(const void *p) {
  BB_GAME_CALL(0x0046f040u, 0, bb_address_encode(p));
}
static const struct bb_model_target targets[] = {
    {0x00476410u, entry_0, 0, 0, 0},    {0x004767d0u, entry_1, 1, 0, 0},
    {0x00476810u, entry_2, 1, 0, 0},    {0x00476840u, entry_3, 2, 0, 0},
    {0x004768e0u, entry_4, 1, 0, 0},    {0x00476950u, entry_5, 0, 0, 0},
    {0x00476970u, entry_6, 2, 0, 0},    {0x00476a40u, entry_7, 2, 0, 0},
    {0x00476c50u, entry_8, 1, 0, 0},    {0x00476cc0u, entry_9, 0, 0, 0},
    {0x00476d40u, entry_10, 0, 0, 0},   {0x00476f50u, entry_11, 1, 0, 0},
    {0x00476fe0u, entry_12, 1, 0, 0},   {0x00477160u, entry_13, 3, 0, 0},
    {0x00477210u, entry_14, 3, 0, 0},   {0x004772a0u, entry_15, 1, 0, 0},
    {0x00477770u, entry_16, 3, 0, 0},   {0x00477a70u, entry_17, 4, 0, 0},
    {0x00477c30u, entry_18, 0, 0, 0},   {0x0046d660u, entry_19, 1, 0, 0},
    {0x0046d690u, entry_20, 4, 0, 0},   {0x0046d6c0u, entry_21, 2, 0, 0},
    {0x0046d6f0u, entry_22, 5, 0, 0},   {0x0046d760u, entry_23, 4, 0, 0},
    {0x0046daa0u, entry_24, 2, 0, 0},   {0x0046dad0u, entry_25, 5, 0, 0},
    {0x0046db40u, entry_26, 5, 0, 0},   {0x0046db80u, entry_27, 6, 0, 0},
    {0x0046e110u, entry_28, 1, 0, 0},   {0x0046e130u, entry_29, 2, 0, 0},
    {0x0046e560u, entry_30, 2, 0, 0},   {0x0046e6d0u, entry_31, 3, 0, 0},
    {0x0046e760u, entry_32, 0, 0, 0},   {0x0046eae0u, entry_33, 1, 0, 0},
    {0x0046eb10u, entry_34, 3, 0, 0},   {0x0046eb70u, entry_35, 1, 0, 0},
    {0x0046ec40u, entry_36, 1, 0, 0},   {0x0046ed80u, entry_37, 1, 0, 0},
    {0x0046f040u, entry_38, 1, 128, 0}, {0x0046f140u, entry_39, 0, 0, 0},
    {0x0046c060u, entry_40, 1, 0, 0},   {0x0046c110u, entry_41, 1, 0, 0},
};
const struct bb_model_target *bb_allocation_model_find(void *ctx, uint32_t id) {
  (void)ctx;
  for (unsigned n = 0; n < sizeof targets / sizeof *targets; n++)
    if (targets[n].identity == id)
      return &targets[n];
  return NULL;
}
