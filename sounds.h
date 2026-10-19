// sounds.h
#pragma once
#include <Arduino.h>
#include "config.h"

extern Config sys;
extern unsigned long soundEnabledAt;
extern float yukiEnergy;
extern volatile bool ttsPlaying;

// ======================== ENGINE PRIMITIVES ========================

inline void _pulse(int toneDelay, int pulseWidth) {
  if (ttsPlaying) return;   // don't fight the voice — LEDC owns the pin during speech
  digitalWrite(SPK_PIN, LOW);
  delayMicroseconds(pulseWidth);
  digitalWrite(SPK_PIN, HIGH);
  delayMicroseconds(toneDelay);
}

inline void playSound(int toneDelay, int duration) {
  if (!sys.soundOn) return;
  if (!sys.beepsOn) return;
  if (millis() < soundEnabledAt) return;
  float n = sys.soundVolume / 100.0f;
  int pw = max(1, (int)(1.0f + n * 14.0f));
  LOGI("SND","playSound td=%d dur=%d pw=%d", toneDelay, duration, pw);
  for (int i = 0; i < duration; i++) _pulse(toneDelay, pw);
}

// Volume scaler: adjusts any pulseWidth by the master volume setting
inline int _adjPW(int pw) {
  float n = sys.soundVolume / 100.0f;
  float f = 0.05f + n * 2.95f;
  return max(1, min(20, (int)(pw * f)));
}

inline void playSoundV(int toneDelay, int duration, int pulseWidth) {
  if (!sys.soundOn) return;
  if (!sys.beepsOn) return;
  if (millis() < soundEnabledAt) return;
  int pw = _adjPW(pulseWidth);
  LOGI("SND","playSoundV td=%d dur=%d pw=%d rawPW=%d", toneDelay, duration, pw, pulseWidth);
  for (int i = 0; i < duration; i++) _pulse(toneDelay, pw);
}

inline void playSoundRaw(int toneDelay, int duration) {
  for (int i = 0; i < duration; i++) _pulse(toneDelay, 5);
}

inline void playSweep(int startDelay, int endDelay, int duration, int pulseWidth) {
  if (!sys.soundOn) return;
  if (!sys.beepsOn) return;
  if (millis() < soundEnabledAt) return;
  int pw = _adjPW(pulseWidth);
  LOGI("SND","playSweep s=%d e=%d dur=%d pw=%d", startDelay, endDelay, duration, pw);
  float step = (float)(endDelay - startDelay) / duration;
  for (int i = 0; i < duration; i++) {
    _pulse(max(10, startDelay + (int)(step * i)), pw);
  }
}

inline void playTrill(int delayA, int delayB, int cyclesPerNote, int totalPairs, int pulseWidth) {
  if (!sys.soundOn) return;
  if (!sys.beepsOn) return;
  if (millis() < soundEnabledAt) return;
  int pw = _adjPW(pulseWidth);
  LOGI("SND","playTrill a=%d b=%d cpn=%d pairs=%d pw=%d", delayA, delayB, cyclesPerNote, totalPairs, pw);
  for (int p = 0; p < totalPairs; p++) {
    for (int c = 0; c < cyclesPerNote; c++) _pulse(max(10, delayA), pw);
    for (int c = 0; c < cyclesPerNote; c++) _pulse(max(10, delayB), pw);
  }
}

// WIGGLE — slow triangle-wave wobble between base ± offset, 2 full cycles over duration
inline void playWiggle(int baseDelay, int offset, int duration) {
  if (!sys.soundOn) return;
  if (!sys.beepsOn) return;
  if (millis() < soundEnabledAt) return;
  int pw = _adjPW(3);
  LOGI("SND","playWiggle base=%d off=%d dur=%d pw=%d", baseDelay, offset, duration, pw);
  int halfCycle = duration / 4;
  if (halfCycle < 1) halfCycle = 1;
  for (int i = 0; i < duration; i++) {
    int pos = i % (halfCycle * 2);
    int wobble = (pos < halfCycle)
      ? (offset * pos / halfCycle)
      : (offset * (halfCycle * 2 - pos) / halfCycle);
    _pulse(max(10, baseDelay + wobble), pw);
  }
}

inline void play_scented_tone(int baseDelay, int duration) {
  if (!sys.soundOn) return;
  if (!sys.beepsOn) return;
  if (millis() < soundEnabledAt) return;
  int m = baseDelay;
  if (sys.soundScents) {
    if (sys.affinity > 75)       m -= 60;
    else if (sys.affinity > 25)  m -= 30;
    else if (sys.affinity < -75) m += 100;
    else if (sys.affinity < -25) m += 50;
  }
  playSound(max(10, m), duration);
}

// ======================== EMOTION SOUNDS ========================

// HAPPY — Rising sweep + bright flourish, ~600ms total
inline void sound_happy() {
  if (!sys.soundOn) return;
  playSweep(300, 110, 800, 7);
  delay(30);
  playSweep(160, 90, 500, 9);
  delay(20);
  playSoundV(100, 300, 7);
}

// SAD — Slow descending sweeps with gaps, ~1.2s total
inline void sound_sad() {
  if (!sys.soundOn) return;
  playSweep(280, 900, 1200, 5);
  delay(100);
  playSweep(700, 1800, 1000, 4);
  delay(120);
  playSweep(1400, 2500, 700, 3);
  delay(80);
  playSoundV(2800, 100, 2);
}

// ANGRY — Loud percussive bursts, ~700ms total
inline void sound_angry() {
  if (!sys.soundOn) return;
  playSoundV(250, 250, 15);  delay(25);
  playSoundV(220, 200, 18);  delay(20);
  playSoundV(190, 150, 20);  delay(15);
  playSoundV(160, 400, 15);
}

// SURPRISED — Fast rising sweep + brief tail, ~300ms total
inline void sound_surprised() {
  if (!sys.soundOn) return;
  playSweep(800, 80, 600, 12);
  delay(5);
  playSweep(100, 70, 150, 6);
}

// LAUGHING — Alternating trills, energy-dependent, ~700ms total
inline void sound_laughing() {
  if (!sys.soundOn) return;
  float ef = 1.0f - ((yukiEnergy - 0.2f) / 0.8f);
  int lag = (int)(ef * 60);
  playTrill(230, 170, 12, 4 + lag/15, 7); delay(15 + lag);
  playTrill(190, 150, 10, 3 + lag/15, 6); delay(10 + lag);
  playSweep(220, 140, 200, 5);
}

// SLEEPY — Very slow low-frequency pulses, ~1.5s total
inline void sound_sleepy() {
  if (!sys.soundOn) return;
  playSoundV(4000, 40, 3);  delay(200);
  playSoundV(3500, 30, 3);  delay(180);
  playSoundV(4200, 25, 2);  delay(250);
  playSoundV(4500, 20, 2);
}

// SASSY — Flat note with abrupt cutoff, ~300ms total
inline void sound_sassy() {
  if (!sys.soundOn) return;
  playSoundV(400, 300, 8);
  delay(15);
  for (int i = 0; i < 5; i++) {
    _pulse(180, _adjPW(12)); delayMicroseconds(600);
  }
}

// CONFUSED — Wandering sweeps with pauses, ~600ms total
inline void sound_confused() {
  if (!sys.soundOn) return;
  playSweep(600, 380, 400, 6);  delay(30);
  playSweep(380, 600, 300, 5);  delay(25);
  playSweep(550, 400, 200, 4);
}

// FLUSTERED — Rapid trill that decelerates, ~500ms total
inline void sound_flustered() {
  if (!sys.soundOn) return;
  float ef = 1.0f - ((yukiEnergy - 0.2f) / 0.8f);
  int drag = (int)(ef * 40);
  playTrill(290, 230, 12 + drag/8, 3, 8); delay(10 + drag);
  playTrill(260, 210, 15 + drag/8, 2, 7); delay(8 + drag);
  playSweep(310, 380, 150, 6);
}

// CURIOUS — Rising sweep, ~400ms total
inline void sound_curious() {
  if (!sys.soundOn) return;
  playSweep(550, 280, 600, 6);
  delay(25);
  playSoundV(320, 100, 4);
}

// TEASING — Bouncy trill, ~350ms total
inline void sound_teasing() {
  if (!sys.soundOn) return;
  playTrill(340, 240, 10, 4, 7);   delay(10);
  playSoundV(270, 100, 6);
}

// WINK — Short up-then-down, ~200ms total
inline void sound_wink() {
  if (!sys.soundOn) return;
  playSweep(320, 180, 200, 6);
  delay(6);
  playSweep(180, 260, 150, 4);
}

// BLUSHING — Two soft stutter notes, ~350ms total
inline void sound_blushing() {
  if (!sys.soundOn) return;
  playSoundV(380, 150, 4);  delay(15);
  playSoundV(380, 100, 3);  delay(10);
  playSweep(340, 260, 200, 3);
}

// SHY — Three very quiet notes, ~350ms total
inline void sound_shy() {
  if (!sys.soundOn) return;
  playSoundV(460, 200, 3);  delay(30);
  playSoundV(460, 120, 2);  delay(20);
  playSoundV(460, 60, 2);
}

// DREAM — Slow descending sweep, ~1.5s total
inline void sound_dream() {
  if (!sys.soundOn) return;
  playSweep(280, 1800, 1500, 4);
  delay(100);
  playSweep(1400, 2800, 1000, 3);
  delay(80);
  playSoundV(3000, 100, 2);
}

// SLEEP_FAREWELL — Descending sweep fading, ~1s total
inline void sound_sleep_farewell() {
  if (!sys.soundOn) return;
  playSweep(400, 1200, 1000, 5);
  delay(80);
  playSweep(900, 2000, 800, 4);
  delay(100);
  playSoundV(2400, 80, 3);
}

// HEARTBEAT — Lub-dub
inline void sound_heartbeat() {
  if (!sys.soundOn) return;
  int pace = 210 - (sys.affinity / 2) - (int)((1.0f - yukiEnergy) * 90);
  pace = constrain(pace, 50, 400);
  playSoundV(750, 250, 15);  delay(pace / 3);
  playSoundV(500, 150, 12);  delay(pace);
  playSoundV(750, 250, 15);  delay(pace / 3);
  playSoundV(500, 150, 12);
}

// PURR — Low alternating vibration, ~600ms total
inline void sound_purr() {
  if (!sys.soundOn) return;
  for (int r = 0; r < 3; r++) {
    playTrill(1300, 1050, 6, 10, 2);
    delay(40);
  }
}

// HUM — Random wandering sweeps, ~1s total
inline void sound_hum() {
  if (!sys.soundOn) return;
  int r1 = random(360, 520), r2 = random(300, 460);
  playSweep(r1, r2, 400, 3);  delay(random(40, 80));
  int r3 = random(290, 440), r4 = random(400, 540);
  playSweep(r3, r4, 300, 3);  delay(random(30, 60));
  playSoundV(random(320, 500), 150, 2);
}

// LEVEL_UP — Clean ascending sequence, ~1s total
inline void sound_level_up() {
  if (!sys.soundOn) return;
  playSoundV(450, 150, 7);  delay(15);
  playSoundV(360, 150, 8);  delay(15);
  playSoundV(280, 150, 9);  delay(15);
  playSoundV(220, 150, 10); delay(15);
  playSoundV(170, 180, 10); delay(20);
  playSoundV(120, 300, 10);
}

// CONFIRM — Two-note chime, ~300ms total
inline void sound_confirm() {
  if (!sys.soundOn) return;
  play_scented_tone(440, 250);  delay(20);
  play_scented_tone(280, 300);
}

// BLIP — Quick tone, ~100ms total
inline void sound_blip() {
  if (!sys.soundOn) return;
  play_scented_tone(300, 100);  delay(6);
  play_scented_tone(240, 60);
}

// NOTIFY — Gentle chime, ~400ms total
inline void sound_notify() {
  if (!sys.soundOn) return;
  play_scented_tone(360, 200);  delay(40);
  play_scented_tone(270, 250);  delay(15);
  play_scented_tone(320, 120);
}

// ERROR — Descending harsh, ~500ms total
inline void sound_error() {
  if (!sys.soundOn) return;
  playSoundV(380, 250, 12);  delay(15);
  playSoundV(550, 200, 14);  delay(12);
  playSoundV(800, 300, 16);
}

// QUESTION — Rising ending, ~400ms total
inline void sound_question() {
  if (!sys.soundOn) return;
  play_scented_tone(440, 180);  delay(15);
  play_scented_tone(340, 150);  delay(10);
  playSweep(320, 180, 300, 6);
}

// IDEA — Rising bright sweep, ~500ms total
inline void sound_idea() {
  if (!sys.soundOn) return;
  play_scented_tone(500, 200);  delay(20);
  play_scented_tone(380, 180);  delay(15);
  playSweep(320, 150, 400, 7);
}

// NOTICED_CONCERN — Soft gentle query, ~300ms total
inline void sound_noticed_concern() {
  if (!sys.soundOn) return;
  if (millis() < soundEnabledAt) return;
  playSweep(440, 320, 300, 4);
  delay(20);
  playSoundV(340, 100, 3);
}

// WELCOME_BACK — Warm rising sweep, ~800ms total
inline void sound_welcome_back() {
  if (!sys.soundOn) return;
  if (millis() < soundEnabledAt) return;
  playSweep(600, 210, 900, 6);
  delay(20);
  playSoundV(220, 120, 5);
  delay(12);
  playSoundV(160, 250, 4);
}

// WAKE_STRETCH — Slow rise, ~1s total
inline void sound_wake_stretch() {
  if (!sys.soundOn) return;
  playSweep(550, 130, 1200, 5);
  delay(15);
  playSoundV(140, 120, 4);
  delay(15);
  playSoundV(115, 200, 4);
}

// WAKE_STRETCH_SHORT — Quick stretch, ~400ms total
inline void sound_wake_stretch_short() {
  if (!sys.soundOn) return;
  playSweep(400, 150, 600, 5);
  delay(8);
  playSoundV(160, 60, 4);
}

// BOOT_STRETCH — Boot sequence, ignores mute but respects bootSound
inline void sound_boot_stretch() {
  if (!sys.bootSound) return;
  for (int d = 500; d >= 120; d -= 22) {
    playSoundRaw(d, 6); delay(5);
  }
  playSoundRaw(100, 80);
}

// TICKLE_TONE — High rapid chirps, ~300ms total
inline void sound_tickle_tone() {
  if (!sys.soundOn) return;
  playTrill(200, 150, 8, 6, 7);  delay(10);
  playTrill(180, 130, 6, 5, 6);
}

// TERMINAL — Flat sharp bursts, ~250ms total
inline void sound_terminal() {
  if (!sys.soundOn) return;
  playSoundV(550, 180, 10);  delay(20);
  playSoundV(380, 150, 10);
}

// SHUTDOWN — Descending, ~400ms total
inline void sound_shutdown() {
  playSweep(900, 1600, 350, 6);
  delay(30);
  playSweep(1400, 2200, 250, 5);
}

// SUCCESS — Clean ascending melody, ~800ms total
inline void sound_success() {
  if (!sys.soundOn) return;
  int melody[] = {659, 784, 1047, 1319};
  for (int i = 0; i < 4; i++) {
    int tDelay = 1000000 / melody[i] / 2;
    playSound(tDelay / 10, 250);
    delay(20);
  }
}

// AI_REPLY — Personality-tailored, ~450ms total
inline void sound_ai_reply() {
  if (!sys.soundOn) return;
  if (sys.affinity > 60) {
    playSweep(360, 150, 600, 7);
    delay(10);
    playSoundV(150, 120, 5);
  } else if (sys.affinity < -50) {
    playSweep(400, 700, 500, 6);
    delay(10);
    playSoundV(650, 120, 4);
  } else {
    playSweep(400, 240, 450, 6);
    delay(8);
    playSoundV(260, 120, 5);
  }
}

// ======================== AMBIENT (subtle, shorter) ========================

inline void sound_ambient_happy() {
  if (!sys.soundOn) return;
  playSweep(320, 190, 250, 5);
}

inline void sound_ambient_sleepy() {
  if (!sys.soundOn) return;
  playSoundV(4200, 25, 2);  delay(150);
  playSoundV(3800, 20, 2);
}

inline void sound_ambient_idle() {
  if (!sys.soundOn) return;
  int t = random(360, 480);
  playSweep(t, t + random(50, 120), 150, 3);
}

inline void sound_ambient_sad() {
  if (!sys.soundOn) return;
  playSweep(700, 1400, 200, 3);
}

inline void sound_ambient_curious() {
  if (!sys.soundOn) return;
  playSweep(500, 350, 180, 5);
}

inline void sound_ambient_playful() {
  if (!sys.soundOn) return;
  playTrill(360, 270, 6, 4, 5);
}

inline void sound_ambient_affectionate() {
  if (!sys.soundOn) return;
  playSweep(400, 250, 200, 3);
  delay(12);
  playSoundV(300, 80, 3);
}

inline void sound_ambient_irritated() {
  if (!sys.soundOn) return;
  playSoundV(280, 100, 10); delay(10);
  playSoundV(250, 80, 12); delay(8);
  playSoundV(220, 120, 10);
}

inline void sound_ambient_state() {
  if (!sys.soundOn) return;
  if (!sys.ambientSounds) return;
  switch (currentEmotion) {
    case HAPPY:    sound_ambient_happy(); break;
    case SLEEPY:   sound_ambient_sleepy(); break;
    case SAD:      sound_ambient_sad(); break;
    case ANGRY:    sound_ambient_irritated(); break;
    case LOVE:     sound_ambient_affectionate(); break;
    case SURPRISED:
    case SHOCKED:
    case THINKING_FACE:
      sound_ambient_curious(); break;
    case SASSY:
    case CONFUSED:
    case TEASING:
      sound_ambient_playful(); break;
    default:       sound_ambient_idle(); break;
  }
  if (sys.affinity > 50) { delay(12); playSoundV(300, 60, 4); }
  else if (sys.affinity < -50) { delay(12); playSoundV(480, 60, 4); }
}
