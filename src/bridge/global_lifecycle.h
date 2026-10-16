/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#ifndef BUMPKINS_GLOBAL_LIFECYCLE_H
#define BUMPKINS_GLOBAL_LIFECYCLE_H
#include <stdint.h>
#define BB_GLOBAL_LIFECYCLE_ENTRIES(X) \
  X(0x4028a0u, bb_global_4028a0_entry) \
  X(0x4028d0u, bb_global_4028d0_entry) \
  X(0x402920u, bb_global_402920_entry) \
  X(0x4029c0u, bb_global_4029c0_entry) \
  X(0x4029f0u, bb_global_4029f0_entry) \
  X(0x402a20u, bb_global_402a20_entry) \
  X(0x402a50u, bb_global_402a50_entry) \
  X(0x402ae0u, bb_global_402ae0_entry) \
  X(0x402b10u, bb_global_402b10_entry) \
  X(0x402b40u, bb_global_402b40_entry) \
  X(0x402b70u, bb_global_402b70_entry) \
  X(0x402ba0u, bb_global_402ba0_entry) \
  X(0x402bd0u, bb_global_402bd0_entry) \
  X(0x402c00u, bb_global_402c00_entry) \
  X(0x402c30u, bb_global_402c30_entry) \
  X(0x402c60u, bb_global_402c60_entry) \
  X(0x402c90u, bb_global_402c90_entry) \
  X(0x402cc0u, bb_global_402cc0_entry) \
  X(0x402cf0u, bb_global_402cf0_entry) \
  X(0x402d20u, bb_global_402d20_entry) \
  X(0x402d50u, bb_global_402d50_entry) \
  X(0x402d80u, bb_global_402d80_entry) \
  X(0x402db0u, bb_global_402db0_entry) \
  X(0x402de0u, bb_global_402de0_entry) \
  X(0x402e10u, bb_global_402e10_entry) \
  X(0x402e40u, bb_global_402e40_entry) \
  X(0x402e50u, bb_global_402e50_entry) \
  X(0x402e60u, bb_global_402e60_entry) \
  X(0x402e70u, bb_global_402e70_entry) \
  X(0x402e80u, bb_global_402e80_entry) \
  X(0x402e90u, bb_global_402e90_entry) \
  X(0x402ea0u, bb_global_402ea0_entry) \
  X(0x402eb0u, bb_global_402eb0_entry) \
  X(0x402ec0u, bb_global_402ec0_entry) \
  X(0x402ed0u, bb_global_402ed0_entry) \
  X(0x402ee0u, bb_global_402ee0_entry) \
  X(0x402ef0u, bb_global_402ef0_entry) \
  X(0x402f00u, bb_global_402f00_entry) \
  X(0x402f10u, bb_global_402f10_entry) \
  X(0x402f20u, bb_global_402f20_entry) \
  X(0x402f30u, bb_global_402f30_entry) \
  X(0x402f40u, bb_global_402f40_entry) \
  X(0x402f50u, bb_global_402f50_entry) \
  X(0x402f60u, bb_global_402f60_entry) \
  X(0x402f70u, bb_global_402f70_entry) \
  X(0x402f80u, bb_global_402f80_entry) \
  X(0x402f90u, bb_global_402f90_entry) \
  X(0x402fa0u, bb_global_402fa0_entry) \
  X(0x402fc0u, bb_global_402fc0_entry) \
  X(0x402fe0u, bb_global_402fe0_entry) \
  X(0x402ff0u, bb_global_402ff0_entry) \
  X(0x403000u, bb_global_403000_entry) \
  X(0x403010u, bb_global_403010_entry) \
  X(0x403020u, bb_global_403020_entry) \
  X(0x403030u, bb_global_403030_entry) \
  X(0x403040u, bb_global_403040_entry) \
  X(0x403050u, bb_global_403050_entry) \
  X(0x403060u, bb_global_403060_entry) \
  X(0x403070u, bb_global_403070_entry) \
  X(0x403080u, bb_global_403080_entry) \
  X(0x403090u, bb_global_403090_entry) \
  X(0x4030a0u, bb_global_4030a0_entry) \
  X(0x4030b0u, bb_global_4030b0_entry) \
  X(0x4030c0u, bb_global_4030c0_entry) \
  X(0x4030d0u, bb_global_4030d0_entry) \
  X(0x4030e0u, bb_global_4030e0_entry) \
  X(0x4030f0u, bb_global_4030f0_entry) \
  X(0x403100u, bb_global_403100_entry) \
  X(0x403110u, bb_global_403110_entry) \
  X(0x403120u, bb_global_403120_entry) \
  X(0x403130u, bb_global_403130_entry) \
  X(0x403140u, bb_global_403140_entry) \
  X(0x403150u, bb_global_403150_entry) \
  X(0x403160u, bb_global_403160_entry) \
  X(0x403170u, bb_global_403170_entry) \
  X(0x403180u, bb_global_403180_entry)

#define DECLARE(address, name) void name(void);
BB_GLOBAL_LIFECYCLE_ENTRIES(DECLARE)
#undef DECLARE
void bb_shared_destroy_entry(void);
void bb_entity_manager_destroy_entry(void);
void *BB_THISCALL bb_cd_handle_construct_entry(void *);
void *BB_THISCALL bb_cd_track_construct_entry(void *);
void *BB_THISCALL bb_cd_tracks_construct_entry(void *);
void *BB_THISCALL bb_cd_bank_construct_entry(void *);
#endif
