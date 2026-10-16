/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BUMPKINS_STATIC_INITIALIZERS_H
#define BUMPKINS_STATIC_INITIALIZERS_H
#include <stdint.h>

#define BB_STATIC_INITIALIZERS(X) \
  X(0x402e20u, bb_static_402e20_entry, bb_global_402e50_entry, bb_global_402e40_entry) \
  X(0x402df0u, bb_static_402df0_entry, bb_global_402e60_entry, bb_global_402e10_entry) \
  X(0x402dc0u, bb_static_402dc0_entry, bb_global_402e70_entry, bb_global_402de0_entry) \
  X(0x402d90u, bb_static_402d90_entry, bb_global_402e80_entry, bb_global_402db0_entry) \
  X(0x402d60u, bb_static_402d60_entry, bb_global_402e90_entry, bb_global_402d80_entry) \
  X(0x402d30u, bb_static_402d30_entry, bb_global_402ea0_entry, bb_global_402d50_entry) \
  X(0x402d00u, bb_static_402d00_entry, bb_global_402eb0_entry, bb_global_402d20_entry) \
  X(0x402cd0u, bb_static_402cd0_entry, bb_global_402ec0_entry, bb_global_402cf0_entry) \
  X(0x402ca0u, bb_static_402ca0_entry, bb_global_402ed0_entry, bb_global_402cc0_entry) \
  X(0x402c70u, bb_static_402c70_entry, bb_global_402ee0_entry, bb_global_402c90_entry) \
  X(0x402c40u, bb_static_402c40_entry, bb_global_402ef0_entry, bb_global_402c60_entry) \
  X(0x402c10u, bb_static_402c10_entry, bb_global_402f00_entry, bb_global_402c30_entry) \
  X(0x402be0u, bb_static_402be0_entry, bb_global_402f10_entry, bb_global_402c00_entry) \
  X(0x402bb0u, bb_static_402bb0_entry, bb_global_402f20_entry, bb_global_402bd0_entry) \
  X(0x402b80u, bb_static_402b80_entry, bb_global_402f30_entry, bb_global_402ba0_entry) \
  X(0x402b50u, bb_static_402b50_entry, bb_global_402f40_entry, bb_global_402b70_entry) \
  X(0x402b20u, bb_static_402b20_entry, bb_global_402f50_entry, bb_global_402b40_entry) \
  X(0x402af0u, bb_static_402af0_entry, bb_global_402f60_entry, bb_global_402b10_entry) \
  X(0x402ac0u, bb_static_402ac0_entry, bb_global_402f70_entry, bb_global_402ae0_entry) \
  X(0x402ab0u, bb_static_402ab0_entry, bb_global_402f80_entry, 0x0u) \
  X(0x402aa0u, bb_static_402aa0_entry, bb_global_402f90_entry, 0x0u) \
  X(0x402a90u, bb_static_402a90_entry, bb_global_402fa0_entry, 0x0u) \
  X(0x402a80u, bb_static_402a80_entry, bb_global_402fc0_entry, 0x0u) \
  X(0x402a70u, bb_static_402a70_entry, bb_global_402fe0_entry, 0x0u) \
  X(0x402a60u, bb_static_402a60_entry, bb_global_402ff0_entry, 0x0u) \
  X(0x402a30u, bb_static_402a30_entry, bb_global_403000_entry, bb_global_402a50_entry) \
  X(0x402a00u, bb_static_402a00_entry, bb_global_403010_entry, bb_global_402a20_entry) \
  X(0x4029d0u, bb_static_4029d0_entry, bb_global_403020_entry, bb_global_4029f0_entry) \
  X(0x4029a0u, bb_static_4029a0_entry, bb_global_403030_entry, bb_global_4029c0_entry) \
  X(0x402990u, bb_static_402990_entry, bb_global_403040_entry, 0x0u) \
  X(0x402980u, bb_static_402980_entry, bb_global_403050_entry, 0x0u) \
  X(0x402970u, bb_static_402970_entry, bb_global_403060_entry, 0x0u) \
  X(0x402960u, bb_static_402960_entry, bb_global_403070_entry, 0x0u) \
  X(0x402950u, bb_static_402950_entry, bb_global_403080_entry, 0x0u) \
  X(0x402940u, bb_static_402940_entry, bb_global_403090_entry, 0x0u) \
  X(0x402900u, bb_static_402900_entry, bb_global_4030a0_entry, bb_global_402920_entry) \
  X(0x4028f0u, bb_static_4028f0_entry, bb_global_4030b0_entry, 0x0u) \
  X(0x4028e0u, bb_static_4028e0_entry, bb_global_4030c0_entry, 0x0u) \
  X(0x4028b0u, bb_static_4028b0_entry, bb_global_4030d0_entry, bb_global_4028d0_entry) \
  X(0x402880u, bb_static_402880_entry, bb_global_4030e0_entry, bb_global_4028a0_entry) \
  X(0x402870u, bb_static_402870_entry, bb_global_4030f0_entry, 0x0u) \
  X(0x402860u, bb_static_402860_entry, bb_global_403100_entry, 0x0u) \
  X(0x402850u, bb_static_402850_entry, bb_global_403110_entry, 0x0u) \
  X(0x402840u, bb_static_402840_entry, bb_global_403120_entry, 0x0u) \
  X(0x402830u, bb_static_402830_entry, bb_global_403130_entry, 0x0u) \
  X(0x402820u, bb_static_402820_entry, bb_global_403140_entry, 0x0u) \
  X(0x402810u, bb_static_402810_entry, bb_global_403150_entry, 0x0u) \
  X(0x402800u, bb_static_402800_entry, bb_global_403160_entry, 0x0u) \
  X(0x4027f0u, bb_static_4027f0_entry, bb_global_403170_entry, 0x0u) \
  X(0x4027e0u, bb_static_4027e0_entry, bb_global_403180_entry, 0x0u)

#define BB_STATIC_REGISTRATIONS(X) \
  X(0x402e30u, bb_static_402e30_entry, bb_global_402e40_entry) \
  X(0x402e00u, bb_static_402e00_entry, bb_global_402e10_entry) \
  X(0x402dd0u, bb_static_402dd0_entry, bb_global_402de0_entry) \
  X(0x402da0u, bb_static_402da0_entry, bb_global_402db0_entry) \
  X(0x402d70u, bb_static_402d70_entry, bb_global_402d80_entry) \
  X(0x402d40u, bb_static_402d40_entry, bb_global_402d50_entry) \
  X(0x402d10u, bb_static_402d10_entry, bb_global_402d20_entry) \
  X(0x402ce0u, bb_static_402ce0_entry, bb_global_402cf0_entry) \
  X(0x402cb0u, bb_static_402cb0_entry, bb_global_402cc0_entry) \
  X(0x402c80u, bb_static_402c80_entry, bb_global_402c90_entry) \
  X(0x402c50u, bb_static_402c50_entry, bb_global_402c60_entry) \
  X(0x402c20u, bb_static_402c20_entry, bb_global_402c30_entry) \
  X(0x402bf0u, bb_static_402bf0_entry, bb_global_402c00_entry) \
  X(0x402bc0u, bb_static_402bc0_entry, bb_global_402bd0_entry) \
  X(0x402b90u, bb_static_402b90_entry, bb_global_402ba0_entry) \
  X(0x402b60u, bb_static_402b60_entry, bb_global_402b70_entry) \
  X(0x402b30u, bb_static_402b30_entry, bb_global_402b40_entry) \
  X(0x402b00u, bb_static_402b00_entry, bb_global_402b10_entry) \
  X(0x402ad0u, bb_static_402ad0_entry, bb_global_402ae0_entry) \
  X(0x402a40u, bb_static_402a40_entry, bb_global_402a50_entry) \
  X(0x402a10u, bb_static_402a10_entry, bb_global_402a20_entry) \
  X(0x4029e0u, bb_static_4029e0_entry, bb_global_4029f0_entry) \
  X(0x4029b0u, bb_static_4029b0_entry, bb_global_4029c0_entry) \
  X(0x402910u, bb_static_402910_entry, bb_global_402920_entry) \
  X(0x4028c0u, bb_static_4028c0_entry, bb_global_4028d0_entry) \
  X(0x402890u, bb_static_402890_entry, bb_global_4028a0_entry)

#define INITIALIZER(address, name, child, callback) int32_t name(void);
BB_STATIC_INITIALIZERS(INITIALIZER)
#undef INITIALIZER
#define REGISTRATION(address, name, callback) int32_t name(void);
BB_STATIC_REGISTRATIONS(REGISTRATION)
#undef REGISTRATION
int bb_static_lifecycle_owns_address(uintptr_t address);
#endif
