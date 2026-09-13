// drawing.h
#pragma once

#include "debug.h"

static const char* emotionNames[] = {
  "NEUTRAL", "HAPPY", "SURPRISED", "SAD", "ANGRY", "THINKING_FACE",
  "SLEEPY", "FLUSTERED", "LAUGHING", "WINK", "CONFUSED", "LOVE",
  "SASSY", "SHOCKED", "SAD_EMBARRASSED", "SHY", "TEASING", "BLUSHING"
};

// Per-emotion variant selector (index set by UI/menu; 0-based)
extern uint8_t emotionVariantIndex[32]; // Shared across all files
extern unsigned long lastActivity;
extern float currentTemp;
extern int weatherCode;
inline uint8_t pickVariant(Emotion e, uint8_t count) {
  if (count == 0) return 0;
  return emotionVariantIndex[(int)e] % count;
}

inline uint32_t getBlinkInterval() {
  switch (currentEmotion) {
    case NEUTRAL:   return random(2000, 8000);
    case SLEEPY:    return random(6000, 14000);
    case SURPRISED:
    case SHOCKED:   return random(200, 600);
    case LAUGHING:  return random(300, 800);
    case ANGRY:     return random(3000, 6000);
    case CONFUSED:  return random(1000, 3000);
    case HAPPY:     return random(2500, 4000);
    default:        return random(1500, 5000);
  }
}

inline uint32_t getBlinkDuration() {
  // HAPPY uses a slightly longer closed-eye duration (80ms) to emphasize the "squeeze"
  if (currentEmotion == HAPPY) return 80;
  return 60;
}

void drawAesthetica() {
  yield(); // Keep rendering responsive on ESP8266 during heavy display updates

  // --- SCREEN SAVER: blank the OLED after 5 min idle (SOUND CFG menu toggle) ---
  static bool screenOff = false;
  {
    static char lastScrMsg[256] = "";
    bool msgChanged = strcmp(aiMsg, lastScrMsg) != 0;
    strncpy(lastScrMsg, aiMsg, sizeof(lastScrMsg) - 1);
    lastScrMsg[sizeof(lastScrMsg) - 1] = '\0';
    bool idle = (millis() - lastActivity) > 300000;   // 5 min without interaction
    if (sys.screenSaver && idle && !screenOff) {
      display.ssd1306_command(0xAE);   // display off
      screenOff = true;
    } else if (screenOff && (!idle || msgChanged || !sys.screenSaver)) {
      display.ssd1306_command(0xAF);   // display on
      screenOff = false;
    }
    if (screenOff) return;   // panel dark: skip all drawing and I2C pushes
  }
  display.clearDisplay();
  
  // --- MOOD STAIRCASE (Decay Logic) ---
  unsigned long currentEmotionEffectiveDuration = emotionDuration; // Base duration from config.h

  // Granular Emotion Duration (Visible in Serial)
  float multiplier = 1.0;
  if (sys.affinity > 0) {
    multiplier = 1.05; // 5% boost for any positive affinity
    if (sys.affinity > 60) multiplier = 1.25;
    // When she likes you, your anger/sadness doesn't stick — negative emotions fade faster
    if (currentEmotion == SAD || currentEmotion == ANGRY) multiplier *= 0.7;
  } else if (sys.affinity < 0) {
    // When she's distant, her negative moods linger — they match the moment
    if (currentEmotion == SAD || currentEmotion == ANGRY || currentEmotion == SASSY) {
      multiplier = 1.8;
    } else {
      multiplier = 0.9; // Other emotions slightly shorter when affinity is low
      if (sys.affinity < -60) multiplier = 0.6;
    }
  }
  currentEmotionEffectiveDuration = (unsigned long)(emotionDuration * multiplier);

  static Emotion heldEmotion = NEUTRAL;
  static unsigned long heldEmotionStart = 0;
  if (currentEmotion != heldEmotion || heldEmotionStart == 0) {
    heldEmotion = currentEmotion;
    heldEmotionStart = millis();
  }

  // Hard cap: repeated same tags cannot refresh one emotion forever.
  // SLEEPY is exempt — it persists until she actually sleeps or gets woken
  unsigned long durationCap = currentEmotionEffectiveDuration * 3;
  bool forceNeutralDecay = (currentEmotion != NEUTRAL && currentEmotion != SLEEPY && millis() - heldEmotionStart > durationCap);

  // Don't decay emotion during Yuki sleep — face stays SLEEPY. Also don't decay SLEEPY — it persists naturally.
  bool shouldDecay = !yukiSleeping && currentEmotion != SLEEPY &&
    (millis() - emotionSetTime > currentEmotionEffectiveDuration || forceNeutralDecay);
  if (shouldDecay) {
    LOGD("DRAW","Emotion Decay: %d | Affinity: %d | Duration Used: %lu", (int)currentEmotion, sys.affinity, currentEmotionEffectiveDuration);

    Emotion nextMood = NEUTRAL; // Default decay target for most emotions

    if (forceNeutralDecay) {
      nextMood = NEUTRAL;
    } else if (currentEmotion == NEUTRAL) { // If currently NEUTRAL, check for proactive mood shift based on affinity
      // (2) Decay Destination: NEUTRAL decay
      if (sys.affinity > 60) {
        nextMood = HAPPY;                          // Only drift to happy at genuinely high affinity
      } else if (sys.affinity > 30 && random(0, 100) < 40) {
        nextMood = (random(0, 2) == 0) ? HAPPY : WINK; // Warm but not automatic
      } else if (sys.affinity < -40 && random(0, 100) < 40) {
        nextMood = (random(0, 3) == 0) ? ANGRY : (random(0, 2) == 0 ? SAD : SASSY);
      } else {
        nextMood = NEUTRAL;                        // Default: hold neutral, don't assume happiness
      }
    } else { // Decay from a non-NEUTRAL emotion
    if (currentEmotion == LAUGHING) nextMood = HAPPY;
    else if (currentEmotion == HAPPY) nextMood = NEUTRAL;
    else if (currentEmotion == SHOCKED || currentEmotion == SURPRISED) nextMood = CONFUSED;
    else if (currentEmotion == BLUSHING || currentEmotion == FLUSTERED) nextMood = SHY;
    else if (currentEmotion == CONFUSED || currentEmotion == THINKING_FACE) nextMood = NEUTRAL;
    else if (currentEmotion == ANGRY) {
      if (sys.affinity > 60) nextMood = CONFUSED; // If affinity is very high, ANGRY decays to CONFUSED
      else nextMood = SAD; // Otherwise, decays to SAD (original logic)
    }
    else nextMood = NEUTRAL; // For any other non-explicitly handled emotion, it decays to NEUTRAL
    }

    currentEmotion = nextMood;
    emotionSetTime = millis(); // Reset timer for the next step in the staircase
    { uint8_t vc = 1; switch(nextMood) { case NEUTRAL: vc=6; break; case HAPPY: vc=3; break; case SURPRISED: vc=2; break; case SAD: vc=2; break; case ANGRY: vc=4; break; case THINKING_FACE: vc=2; break; case SLEEPY: vc=2; break; case FLUSTERED: vc=3; break; case LAUGHING: vc=3; break; case WINK: vc=3; break; case CONFUSED: vc=2; break; case LOVE: vc=6; break; case SASSY: vc=2; break; case SHOCKED: vc=4; break; case SHY: vc=2; break; case TEASING: vc=5; break; case BLUSHING: vc=5; break; default: vc=1; } emotionVariantIndex[(int)currentEmotion] = random(0, vc); }
  }

  // 1. Status Bar — rotates through info pages while visible
  if (millis() < statusBarVisibleUntil) {
    // Cycle pages every 1.5s
    if (millis() > statusBarPageSwitchTime) {
      statusBarPage = (statusBarPage + 1) % 3;
      statusBarPageSwitchTime = millis() + 1500;
    }
    display.drawFastHLine(0, 10, 128, WHITE);

    char bufL[8] = "", bufC[8] = "", bufR[8] = "";
    if (statusBarPage == 0) {
      // Page 0: Level | XP | WiFi
      snprintf(bufL, sizeof(bufL), "L%02d", sys.level);
      snprintf(bufC, sizeof(bufC), "X:%d", sys.xp);
      if (WiFi.status() == WL_CONNECTED) {
        strncpy(bufR, WiFi.SSID().c_str(), 4); bufR[4] = '\0';
      } else {
        strncpy(bufR, "OFF", sizeof(bufR));
      }
    } else if (statusBarPage == 1) {
      // Page 1: Uptime(min) | Temp | Time
      unsigned long mins = millis() / 60000;
      if (mins < 100) snprintf(bufL, sizeof(bufL), "%lum", mins);
      else snprintf(bufL, sizeof(bufL), "%luh", mins / 60);
      snprintf(bufC, sizeof(bufC), "%.0fC", currentTemp);
      int hr = timeClient.getHours();
      int mn = timeClient.getMinutes();
      snprintf(bufR, sizeof(bufR), "%02d:%02d", hr, mn);
    } else {
      // Page 2: Emotion | Affinity | RPG Gold
      const char* emoShort = "?";
      switch (currentEmotion) {
        case NEUTRAL: emoShort = "OK"; break;
        case HAPPY: emoShort = ":)"; break;
        case SAD: emoShort = ":("; break;
        case ANGRY: emoShort = "!"; break;
        case SLEEPY: emoShort = "zz"; break;
        case SURPRISED: emoShort = "o."; break;
        case LAUGHING: emoShort = "XD"; break;
        case LOVE: emoShort = "<3"; break;
        case CONFUSED: emoShort = "??"; break;
        case SASSY: emoShort = "~."; break;
        case SHY: emoShort = ",."; break;
        case SHOCKED: emoShort = "O!"; break;
        case FLUSTERED: emoShort = "~~"; break;
        case THINKING_FACE: emoShort = ".."; break;
        case WINK: emoShort = ";)"; break;
        case BLUSHING: emoShort = "*~"; break;
        default: emoShort = ".."; break;
      }
      snprintf(bufL, sizeof(bufL), "%s", emoShort);
      snprintf(bufC, sizeof(bufC), "A:%d", sys.affinity);
      snprintf(bufR, sizeof(bufR), "G:%d", sys.rpgGold);
    }
    display.setCursor(0, 0); display.print(bufL);
    display.setCursor(55, 0); display.print(bufC);
    display.setCursor(95, 0); display.print(bufR);
    display.drawLine(25, 12, 105, 12, WHITE);
  }
  
  // --- BITMAP FACE RENDERER ---
  Emotion displayEmotion = (millis() < microExpressionUntil) ? microEmotion : currentEmotion;
  
  // Safety Check: Only override the face if silentThoughtUntil is in the near future.
  // This prevents permanent "Thinking" face if the variable is corrupted.
  if (silentThoughtUntil != 0 && millis() < silentThoughtUntil && (silentThoughtUntil - millis() < 10000)) 
    displayEmotion = THINKING_FACE;

  bool showBlink = blinking;
  uint8_t variant = 0;
  if (showBlink) {
    display.drawBitmap(31, 0, epd_bitmap_blink, 66, 64, WHITE); // 66 wide -> x=31
  } else {
    switch(displayEmotion) {
      case HAPPY:
        variant = pickVariant(displayEmotion, 3);
        if (variant == 0)      display.drawBitmap(33, 0, epd_bitmap_happy, 61, 64, WHITE);
        else if (variant == 1) display.drawBitmap(28, 0, epd_bitmap_happy_v2, 72, 64, WHITE);
        else                   display.drawBitmap(20, 0, epd_bitmap_happy_v3, 80, 50, WHITE); // Suggesting 88 width, X=20
        break;
      case SAD:
        variant = pickVariant(displayEmotion, 2);
        if (variant == 0) display.drawBitmap(32, 0, epd_bitmap_sad, 64, 64, WHITE);
        else              display.drawBitmap(32, 0, epd_bitmap_sad_embarrassed, 64, 64, WHITE);
        break;
      case SURPRISED:
        variant = pickVariant(displayEmotion, 2);
        if (variant == 0) display.drawBitmap(28, 0, epd_bitmap_surprised, 72, 64, WHITE);
        else              display.drawBitmap(28, 0, epd_bitmap_surprised_v2, 72, 64, WHITE);
        break;
      case ANGRY:
        variant = pickVariant(displayEmotion, 4);
        if (variant == 0)      display.drawBitmap(31, 0, epd_bitmap_angry, 66, 64, WHITE);
        else if (variant == 1) display.drawBitmap(20, 0, epd_bitmap_angry_v2, 88, 64, WHITE);
        else if (variant == 2) display.drawBitmap(25, 0, epd_bitmap_angry_v3, 78, 64, WHITE);
        else                   display.drawBitmap(23, 0, epd_bitmap_angry_v4, 82, 64, WHITE);
        break;
      case THINKING_FACE:
        variant = pickVariant(displayEmotion, 2);
        if (variant == 0) display.drawBitmap(28, 0, epd_bitmap_thinking, 72, 64, WHITE);
        else              display.drawBitmap(28, 0, epd_bitmap_thinking_v2, 72, 64, WHITE);
        break;
      case SLEEPY:
        variant = pickVariant(displayEmotion, 2);
        if (variant == 0) display.drawBitmap(32, 0, epd_bitmap_sleepy, 64, 64, WHITE);
        else              display.drawBitmap(32, 0, epd_bitmap_sleepy_v2, 64, 64, WHITE);
        break;
      case FLUSTERED:
        variant = pickVariant(displayEmotion, 3);
        if (variant == 0)      display.drawBitmap(31, 0, epd_bitmap_flustered, 66, 64, WHITE);
        else if (variant == 1) display.drawBitmap(15, 0, epd_bitmap_flustered_v2, 98, 64, WHITE);
        else                   display.drawBitmap(20, 0, epd_bitmap_flustered_v3, 88, 64, WHITE);
        break;
      case LAUGHING:
        variant = pickVariant(displayEmotion, 3);
        if (variant == 0)      display.drawBitmap(28, 0, epd_bitmap_laughing, 72, 64, WHITE);
        else if (variant == 1) display.drawBitmap(28, 0, epd_bitmap_laughing_v2, 72, 64, WHITE);
        else                   display.drawBitmap(30, 0, epd_bitmap_laughing_heavy, 68, 64, WHITE);
        break;
      case WINK:
        variant = pickVariant(displayEmotion, 3);
        if (variant == 0)      display.drawBitmap(29, 0, epd_bitmap_wink, 69, 64, WHITE);
        else if (variant == 1) display.drawBitmap(23, 0, epd_bitmap_wink_v2, 82, 64, WHITE);
        else                   display.drawBitmap(23, 0, epd_bitmap_wink_v3, 82, 64, WHITE);
        break;
      case CONFUSED:
        variant = pickVariant(displayEmotion, 2);
        if (variant == 0) display.drawBitmap(28, 0, epd_bitmap_confused, 72, 64, WHITE); // Suggesting 88 width, X=20
        else              display.drawBitmap(28, 0, epd_bitmap_confused_v2, 72, 64, WHITE);
        break;
      case LOVE:
        variant = pickVariant(displayEmotion, 6);
        if (variant == 0)      display.drawBitmap(28, 0, epd_bitmap_love, 72, 64, WHITE);
        else if (variant == 1) display.drawBitmap(0, 0, epd_bitmap_love_v2, 128, 64, WHITE);
        else if (variant == 2) display.drawBitmap(15, 0, epd_bitmap_love_v3, 98, 64, WHITE);
        else if (variant == 3) display.drawBitmap(15, 0, epd_bitmap_love_v4, 98, 64, WHITE);
        else if (variant == 4) display.drawBitmap(20, 0, epd_bitmap_love_v5, 88, 64, WHITE);
        else                   display.drawBitmap(20, 0, epd_bitmap_love_v6, 88, 64, WHITE);
        break;
      case SASSY:
        variant = pickVariant(displayEmotion, 2);
        if (variant == 0) display.drawBitmap(31, 0, epd_bitmap_sassy, 66, 64, WHITE);
        else              display.drawBitmap(26, 0, epd_bitmap_sassy_v2, 76, 64, WHITE);
        break;
      case SHOCKED:
        variant = pickVariant(displayEmotion, 4);
        if (variant == 0)      display.drawBitmap(28, 0, epd_bitmap_shocked, 72, 64, WHITE);
        else if (variant == 1) display.drawBitmap(20, 0, epd_bitmap_shocked_v2, 88, 64, WHITE);
        else if (variant == 2) display.drawBitmap(20, 0, epd_bitmap_shocked_v3, 88, 64, WHITE);
        else                   display.drawBitmap(20, 0, epd_bitmap_shocked_v4, 88, 64, WHITE);
        break;
      case SAD_EMBARRASSED:
        display.drawBitmap(32, 0, epd_bitmap_sad_embarrassed, 64, 64, WHITE); // 64 wide -> x=32
        break;
      case SHY:
        variant = pickVariant(displayEmotion, 2);
        if (variant == 0) display.drawBitmap(29, 0, epd_bitmap_shy_flustered, 70, 64, WHITE);
        else              display.drawBitmap(28, 0, epd_bitmap_shy_fidget, 72, 64, WHITE);
        break;
      case TEASING:
        variant = pickVariant(displayEmotion, 5);
        if (variant == 0)      display.drawBitmap(30, 0, epd_bitmap_teasing, 68, 64, WHITE);
        else if (variant == 1) display.drawBitmap(20, 0, epd_bitmap_teasing_v2, 88, 64, WHITE);
        else if (variant == 2) display.drawBitmap(20, 0, epd_bitmap_teasing_v3, 88, 64, WHITE);
        else if (variant == 3) display.drawBitmap(20, 0, epd_bitmap_teasing_v4, 88, 64, WHITE);
        else                   display.drawBitmap(15, 0, epd_bitmap_teasing_v5, 98, 64, WHITE);
        break;
      case BLUSHING:
        variant = pickVariant(displayEmotion, 5);
        if (variant == 0)      display.drawBitmap(15, 0, epd_bitmap_blushing, 98, 64, WHITE);
        else if (variant == 1) display.drawBitmap(15, 0, epd_bitmap_blushing_v2, 98, 64, WHITE);
        else if (variant == 2) display.drawBitmap(20, 0, epd_bitmap_blushing_v3, 88, 64, WHITE);
        else if (variant == 3) display.drawBitmap(20, 0, epd_bitmap_blushing_v4, 88, 64, WHITE);
        else                   display.drawBitmap(20, 0, epd_bitmap_blushing_v5, 88, 64, WHITE);
        break;
      case NEUTRAL:
        variant = pickVariant(displayEmotion, 6);
        if (variant == 0)      display.drawBitmap(29, 0, epd_bitmap_neutral, 69, 64, WHITE);
        else if (variant == 1) display.drawBitmap(25, 0, epd_bitmap_neutral_v2, 78, 64, WHITE);
        else if (variant == 2) display.drawBitmap(25, 0, epd_bitmap_neutral_v3, 78, 64, WHITE);
        else if (variant == 3) display.drawBitmap(20, 0, epd_bitmap_neutral_v4, 88, 64, WHITE);
        else if (variant == 4) display.drawBitmap(25, 0, epd_bitmap_neutral_v5, 78, 64, WHITE);
        else                   display.drawBitmap(20, 0, epd_bitmap_neutral_v6, 88, 64, WHITE);
        break;
      default:
        display.drawBitmap(29, 0, epd_bitmap_neutral, 69, 64, WHITE);
        break;
    }
  }

  // Track displayed text changes for debug
  static char lastDisplayedMsg[256] = "";
  if (strcmp(aiMsg, lastDisplayedMsg) != 0) {
    strncpy(lastDisplayedMsg, aiMsg, sizeof(lastDisplayedMsg) - 1);
    lastDisplayedMsg[sizeof(lastDisplayedMsg) - 1] = '\0';
    LOGI("DISP","aiMsg='%s' mode=%d emo=%d", aiMsg, (int)currentMode, (int)currentEmotion);
  }

  // Defensive: strip bracket tags from display text to prevent [HAPPY] etc. leaking to OLED
  {
    char* bp;
    while ((bp = strchr(aiMsg, '[')) != nullptr) {
      char* be = strchr(bp, ']');
      if (be) {
        memmove(bp, be + 1, strlen(be + 1) + 1);
        if (bp > aiMsg && *(bp-1) == ' ' && *bp == ' ')
          memmove(bp, bp + 1, strlen(bp + 1) + 1);
      } else break;
    }
    if ((int)scrollOffset > (int)strlen(aiMsg)) scrollOffset = 0;
  }

  // Log emotion bitmap display changes
  static Emotion lastFaceEmotion = NEUTRAL;
  static uint8_t lastFaceVariant = 0;
  if (!showBlink && (displayEmotion != lastFaceEmotion || variant != lastFaceVariant)) {
    lastFaceEmotion = displayEmotion;
    lastFaceVariant = variant;
    int idx = (int)displayEmotion;
    LOGI("FACE","draw %s v%d aff=%d", 
      idx >= 0 && idx <= (int)BLUSHING ? emotionNames[idx] : "?", variant, sys.affinity);
  }

  // 3. Dialogue Text overlay
  display.setTextSize(1);
  int textY = 50;
  int boxH = 14;
  int maxChars = 21;
  display.setCursor(0, textY);
  int msgLen = strlen(aiMsg);
  
  // Draw background box for text area
  display.fillRect(0, textY, 128, boxH, BLACK);
  
  // Safety Check: If silentThoughtUntil is active, hide text. 
  // Otherwise, or if the timer is weirdly long, show it.
  if (silentThoughtUntil == 0 || millis() >= silentThoughtUntil || (silentThoughtUntil - millis() > 10000)) {
  if (msgLen <= maxChars) {
    display.print(aiMsg);
  } else {
    // Memory-safe scrolling using char arrays to prevent fragmentation
    char buffer[23];
    int bufSize = maxChars + 2;
    strncpy(buffer, aiMsg + scrollOffset, maxChars);
    buffer[maxChars] = '\0';
    int len = strlen(buffer);
    if (len < maxChars) {
      strcat(buffer, " ");
      strncat(buffer, aiMsg, bufSize - len - 2);
    }
    display.print(buffer);
  }
  }
  yield();
  display.display();
}
