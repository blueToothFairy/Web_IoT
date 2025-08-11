#ifndef BUZZER_H
#define BUZZER_H

#include "config.h"

inline void Buzzer_Update(Level lvAll) {
  static uint32_t lastBeep = 0;
  if (lvAll == LV_DANG) {
    if (millis() - lastBeep > 2000) {
      for (int i=0;i<3;i++){ tone(BUZZER, 2000); delay(120); noTone(BUZZER); delay(120); }
      lastBeep = millis();
    }
  } else if (lvAll == LV_WARN) {
    if (millis() - lastBeep > 3000) {
      tone(BUZZER, 1500); delay(150); noTone(BUZZER);
      lastBeep = millis();
    }
  } else {
    noTone(BUZZER);
  }
}

#endif
