/* SPDX-License-Identifier: GPL-3.0-only */
#include "qmixer_models.h"
#include "qmixer_vtables.h"
#include <string.h>

/* The portable representation contains four-byte logical method identities,
 * even on hosts whose function pointers or long doubles have another size. */
#define BB_QMIX_SLOT(identity, entry) identity
#define BB_QMIX_TABLE(name, count, ...)                                        \
  static const uint32_t name##_identities[count] = {__VA_ARGS__};
#include "qmixer_method_tables.h"
#undef BB_QMIX_TABLE
#undef BB_QMIX_SLOT

void bb_qmix_model_tables_initialize(unsigned char *base) {
#define TABLE(address, name, count)                                            \
  memcpy(base + (address - 0x10000000u), name##_identities,                    \
         sizeof(name##_identities));
  BB_QMIX_VTABLES(TABLE)
#undef TABLE
}
