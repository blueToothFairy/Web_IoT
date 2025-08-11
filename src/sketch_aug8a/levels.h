#ifndef LEVELS_H
#define LEVELS_H

#include "config.h"

inline Level levelWithHysFloat(float v, float warn, float dang, float hys, Level prev) {
  if (isnan(v)) return prev;
  if (prev == LV_DANG) { if (v < dang*hys) return LV_WARN; return LV_DANG; }
  if (prev == LV_WARN) {
    if (v < warn*hys) return LV_OK;
    if (v >= dang)    return LV_DANG;
    return LV_WARN;
  }
  if (v >= dang) return LV_DANG;
  if (v >= warn) return LV_WARN;
  return LV_OK;
}

inline Level levelWithHysInt(int v, int warn, int dang, float hys, Level prev) {
  if (prev == LV_DANG) { if (v < (int)(dang*hys)) return LV_WARN; return LV_DANG; }
  if (prev == LV_WARN) {
    if (v < (int)(warn*hys)) return LV_OK;
    if (v >= dang)           return LV_DANG;
    return LV_WARN;
  }
  if (v >= dang) return LV_DANG;
  if (v >= warn) return LV_WARN;
  return LV_OK;
}

#endif
