/* SPDX-License-Identifier: GPL-3.0-only */
#include "native.h"
#include <string.h>
static I32 W RegDeleteKeyA(Handle key, const char *name) { return 0; }
static I32 W RegCloseKey(Handle key) { return 0; }
static I32 W RegOpenKeyExA(Handle key, const char *name, U32 options,
                           U32 access, void *output) {
  U32 value = 0xbb0001;
  memcpy(output, &value, 4);
  return 0;
}
static I32 W RegEnumValueA(Handle key, U32 index, char *name, U32 *name_size,
                           U32 *reserved, U32 *type, unsigned char *data,
                           U32 *data_size) {
  static const char *names[] = {
      "Audio",   "CD",        "InstallDir",  "InstallDrive", "Link",
      "Misc",    "Missions",  "MissionText", "Speech",       "Video",
      "Working", "Installed", "Language"};
  /* The pinned no-installation patch uses archive 0 for mission text and
   * speech in every UI language. */
  static const char *values[] = {
      "audio.box", ".\\",          "\\",           "Z:",          "",
      "misc.box",  "missions.box", "mistext0.box", "speech0.box", "video.box",
      ".\\"};
  if (index >= 13)
    return 259;
  if (!name_size || !data_size)
    return 87;
  size_t length = strlen(names[index]);
  if (!name || *name_size <= length)
    return 234;
  memcpy(name, names[index], length + 1);
  *name_size = (U32)length;
  if (index >= 11) {
    if (type)
      *type = 4;
    if (!data) {
      *data_size = 4;
      return 0;
    }
    if (*data_size < 4)
      return 234;
    U32 value = index == 11 ? 1 : bb_language;
    memcpy(data, &value, 4);
    *data_size = 4;
  } else {
    if (type)
      *type = 1;
    size_t size = strlen(values[index]) + 1;
    if (!data) {
      *data_size = (U32)size;
      return 0;
    }
    if (*data_size < size)
      return 234;
    memcpy(data, values[index], size);
    *data_size = (U32)size;
  }
  return 0;
}
static U32 W GetPrivateProfileIntA(const char *section, const char *key,
                                   I32 fallback, const char *filename) {
  return bb_profile_integer(section, key, fallback, filename);
}
void *bb_settings_api(const char *name) {
  static const struct ApiEntry table[] = {
      API_ENTRY(RegDeleteKeyA), API_ENTRY(RegCloseKey),
      API_ENTRY(RegOpenKeyExA), API_ENTRY(RegEnumValueA),
      API_ENTRY(GetPrivateProfileIntA)};
  API_RETURN(table);
}
#ifdef BB_MODEL_RUNTIME
#include "settings_models.inc"
#endif
