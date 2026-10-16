/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_MODULE_NAMES_H
#define BB_MODULE_NAMES_H
enum { BB_NATIVE_LIBRARY_COUNT = 9 };
struct NativeLibrary {
  const char *name;
};
extern const struct NativeLibrary bb_native_libraries[BB_NATIVE_LIBRARY_COUNT];
const char *bb_library_name(const char *);
#endif
