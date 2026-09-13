// ui_handlers.h
#pragma once

#include <Preferences.h>
#include <esp_task_wdt.h>
#include <esp_sleep.h>

extern unsigned long lastInteraction;
extern unsigned long lastIdleCheck;
extern int infoScrollOffset;
extern char core_chatSummary[1024];
extern int shutdownFlag;

extern char workspace[3200];
extern bool rpgStoryUpdated;

extern PersonalityMode sessionMode;

// Physical Interaction Externs
extern unsigned long dnBtnPressStart;
extern bool dnBtnHeld;
extern int tickleCount;
extern unsigned long lastTicklePress;
extern int touchAnnoyance;
extern unsigned long lastTouchTime;
extern char core_lastConcern[64];
extern bool core_badDayFlag;
extern bool yukiSleeping;

inline void safeDelay(int ms) {
  // ESP32 does not need manual watchdog feeding.
  for(int i = 0; i < ms / 10; i++) {
    delay(10);
    yield();
  }
  delay(ms % 10);
}

// OLED display is 128px wide, default font is 6px/char = 21 chars max per line.
// Format a menu item "label = value" ensuring total width fits without wrapping.
inline void formatMenuItem(char* out, size_t outSize, const char* label, const char* value) {
  // "  > " = 4 chars, " = " = 3 chars => label + value must fit in 21 - 7 = 14 chars
  int maxVal = 14 - (int)strlen(label);
  if (maxVal < 1) maxVal = 1;
  snprintf(out, outSize, " = %.*s", maxVal, value);
}

// Forward declaration from main .ino
void gainXP(int amount);

// --- TERMINAL GLOBALS ---
int termScrollTop = 0;         // For scrolling through long output
// Removed TTT globals to save RAM

// --- RPG GLOBALS ---
extern char rpgStory[256];
extern char rpgChoices[3][40];
extern int rpgChoiceIdx;
int rpgStoryScrollOffset = 0;
unsigned long rpgStoryLastScroll = 0;

// --- QUICK RESPONSE GLOBALS (Stored in Flash to save RAM) ---
static const char qr0[] PROGMEM = "How are you?";
static const char qr1[] PROGMEM = "Hello!";
static const char qr2[] PROGMEM = "Goodbye.";
static const char qr3[] PROGMEM = "Yes";
static const char qr4[] PROGMEM = "No";
static const char qr5[] PROGMEM = "What?";
static const char qr6[] PROGMEM = "Why?";
static const char qr7[] PROGMEM = "Sorry!";
static const char qr8[] PROGMEM = "ok";
static const char qr9[] PROGMEM = "Thank you.";
static const char qr10[] PROGMEM = "I'm back.";
static const char qr11[] PROGMEM = "I'm bored.";
static const char qr12[] PROGMEM = "Tell me a joke.";
static const char qr13[] PROGMEM = "I love you!";
static const char qr14[] PROGMEM = "Good night!";
static const char qr15[] PROGMEM = "Status report.";
static const char qr16[] PROGMEM = "What are you thinking?";
static const char qr17[] PROGMEM = "You're cute.";
static const char qr18[] PROGMEM = "Sing a song.";
static const char qr19[] PROGMEM = "Do you like me?";
static const char qr20[] PROGMEM = "Are you sleepy?";
static const char qr21[] PROGMEM = "Tell me a secret.";
static const char qr22[] PROGMEM = "Let's play!";
static const char qr23[] PROGMEM = "What's the weather?";
static const char qr24[] PROGMEM = "Tell me a story.";
static const char qr25[] PROGMEM = "Give me a quest!";

static const char* const quickResponses[] PROGMEM = {
  qr0, qr1, qr2, qr3, qr4, qr5, qr6, qr7, qr8, qr9, qr10, qr11, qr12, qr13, qr14, qr15, qr16, qr17, qr18, qr19, qr20, qr21, qr22, qr23, qr24, qr25
};
const int numQuickResponses = 26;

// Small helpers for personality-aware phrase selection
inline const char* pick_phrase(const char* pool[], int n) {
  if (n <= 0) return "";
  return pool[random(0, n)];
}

inline void format_with_name(const char* tmpl, char* out, size_t outSize, const char* who) {
  if (who && tmpl && strstr(tmpl, "%s") != NULL) {
    snprintf(out, outSize, tmpl, who);
  } else if (tmpl) {
    strncpy(out, tmpl, outSize - 1);
    out[outSize - 1] = '\0';
  } else {
    if (outSize > 0) out[0] = '\0';
  }
}

inline void terminalCatFile(const char* filename) {
  workspace[0] = '\0';
  termScrollTop = 0;

  char path[70];
  if (filename[0] != '/') snprintf(path, sizeof(path), "/%s", filename); 
  else strncpy(path, filename, sizeof(path));
  
  if (LittleFS.exists(path)) {
    File f = LittleFS.open(path, "r");
    int len = f.readBytes(workspace, 1023);
    workspace[len] = '\0';
    f.close();
  } else {
    strncpy(workspace, "File not found.", 1023);
  }
}

inline void executeTerminalCommand(const char* rawInput) {
  workspace[0] = '\0';
  termScrollTop = 0;

  char cmd[32]; char arg[64]; arg[0] = '\0';
  // Parse "cmd arg" (Fixed to allow spaces in argument, e.g., "calc 5 + 5")
  sscanf(rawInput, "%31s %63[^\n]", cmd, arg);

  if (strcmp(cmd, "ipconfig") == 0) {
    long rssi = WiFi.RSSI();
    snprintf(workspace, 1024, "IP: %s\nMask: %s\nGW: %s\nSignal: %lddBm", 
      WiFi.localIP().toString().c_str(), WiFi.subnetMask().toString().c_str(),
      WiFi.gatewayIP().toString().c_str(), rssi);
  }
  else if (strcmp(cmd, "ping") == 0) {
    IPAddress remote(8,8,8,8); // Default to Google DNS
    int port = 53; // Default to DNS port
    // If an argument (e.g., "google.com") was typed, try to resolve it
    if (strlen(arg) > 0) {
      if (!WiFi.hostByName(arg, remote)) {
        strcat(workspace, "DNS Lookup Failed.\nUsing 8.8.8.8...\n");
      } else {
        char buf[64]; snprintf(buf, 64, "Pinging %s\n(%s)...\n", arg, remote.toString().c_str());
        strcat(workspace, buf);
        port = 80; // Use HTTP port for websites
      }
    } else {
      strcat(workspace, "Pinging 8.8.8.8...\n");
    }

    WiFiClient pinger;
    unsigned long start = millis();
    if (pinger.connect(remote, port)) {
      char buf[64]; snprintf(buf, 64, "Reply: %lums\nStatus: Online", millis() - start);
      strcat(workspace, buf);
      pinger.stop();
    } else {
      strcat(workspace, "Request timed out.\nStatus: Offline");
    }
  }
  else if (strcmp(cmd, "ls") == 0) {
    File root = LittleFS.open("/");
    strcat(workspace, "FILES:\n");
    File entry = root.openNextFile();
    while (entry) {
      if (strlen(workspace) > 950) { strcat(workspace, "...\n"); break; }
      char line[64];
      snprintf(line, sizeof(line), "%-13.13s %dB\n", entry.name(), (int)entry.size());
      strcat(workspace, line);
      entry = root.openNextFile();
    }
    root.close();
    // ESP32 LittleFS uses File iteration instead of Dir.
  }
  else if (strcmp(cmd, "free") == 0) {
    snprintf(workspace, 1024, "Heap: %u B\nMinHeap: %u B\nPSRAM: %u B",
      (unsigned)ESP.getFreeHeap(),
      (unsigned)ESP.getMinFreeHeap(),
      (unsigned)ESP.getFreePsram());
  }
  // ESP32 does not have getMaxFreeBlockSize() or getHeapFragmentation().
  // getMinFreeHeap() shows the lowest heap ever recorded (watermark).
  else if (strcmp(cmd, "reboot") == 0) {
    strcat(workspace, "System rebooting...");
    // We delay slightly in the display loop to show this
    sound_shutdown(); // Play shutdown sound
    safeDelay(1000);
    LittleFS.end(); delay(100); // Flush filesystem before power cut
    ESP.restart();
  }
  else if (strcmp(cmd, "hlp") == 0 || strcmp(cmd, "help") == 0) {
    strcat(workspace, "CMDS:\ntime, settime, calc\nwrite, ipconfig, ping\nls, cat, rm, free, reboot");
  }
  else if (strcmp(cmd, "time") == 0) {
    time_t epochTime = timeClient.getEpochTime();
    struct tm *ti = localtime(&epochTime);
    char dateBuf[64];
    strftime(dateBuf, sizeof(dateBuf), "%a %b %d %Y", ti);
    int tz = sys.timeZoneOffset / 3600;
    snprintf(workspace, 1024, "Time: %s\nDate: %s\nZone: UTC%+d", 
             timeClient.getFormattedTime().c_str(), dateBuf, tz);
  }
  else if (strcmp(cmd, "calc") == 0) {
    int a, b; char op;
    if (sscanf(arg, "%d %c %d", &a, &op, &b) == 3) {
      if (op == '+') snprintf(workspace, 1024, "%d + %d = %d", a, b, a + b);
      else if (op == '-') snprintf(workspace, 1024, "%d - %d = %d", a, b, a - b);
      else if (op == '*') snprintf(workspace, 1024, "%d * %d = %d", a, b, a * b);
      else if (op == '/') {
        if (b != 0) snprintf(workspace, 1024, "%d / %d = %d", a, b, a / b);
        else strcpy(workspace, "Divide by Zero!");
      }
      else strcpy(workspace, "Unknown operator.\nUse +, -, *, /");
    } else {
      strcpy(workspace, "Usage: calc 5 + 5");
    }
  }
  else if (strcmp(cmd, "write") == 0) {
     char fname[32]; char content[48];
     if (sscanf(arg, "%31s %47[^\n]", fname, content) == 2) {
       char path[24]; snprintf(path, sizeof(path), "/%s", fname);
       File f = LittleFS.open(path, "w");
       if (f) { f.print(content); f.close(); snprintf(workspace, 1024, "Saved %s.", fname); }
       else strcpy(workspace, "Write Error.");
     } else strcpy(workspace, "Usage:\nwrite file.txt text");
  }
  else if (strcmp(cmd, "cat") == 0) {
     if (strlen(arg) == 0) { viewIdx = 0; currentMode = TERMINAL_FILE_LIST; sound_terminal(); return; }
     terminalCatFile(arg);
  }
  else if (strcmp(cmd, "rm") == 0) {
     char path[70];
     if (arg[0] != '/') snprintf(path, sizeof(path), "/%s", arg); else strncpy(path, arg, sizeof(path));
     if (LittleFS.remove(path)) strcat(workspace, "Deleted file."); 
     else strcat(workspace, "Delete failed.");
  }
  else {
    snprintf(workspace, 1024, "Unknown command:\n'%s'\nType 'hlp' for list.", cmd);
  }

  currentMode = TERMINAL_OUTPUT;
  sound_terminal();
}

void handleFaceMode() {
  if (yukiSleeping) return; // Yuki Sleep handles its own display + input in main loop
  // If any button is pressed, show the status bar for 3 seconds.
  if (digitalRead(BTN_UP) == LOW || digitalRead(BTN_DN) == LOW || digitalRead(BTN_SEL) == LOW || digitalRead(BTN_BACK) == LOW) {
    lastActivity = millis(); // Also reset sleep timer on any button press
    statusBarVisibleUntil = millis() + 3000;
  } else if (!dnBtnHeld) {
    // If no buttons are pressed and we aren't mid-touch, clear tickle counts
    if (millis() - lastTicklePress > 1000) tickleCount = 0;
  }

  drawAesthetica(); // This will now automatically draw the SLEEPY face if emotion is set

  // Timer 1: Mood-aware ambient micro-sound
  static unsigned long lastMicroSound = 0;
  static unsigned long nextSoundTime = 0;
  if (nextSoundTime == 0) {
    lastMicroSound = millis();
    nextSoundTime = random(900000, 1200000); // 15-20 minutes
  }
  if (sys.soundOn && millis() - lastMicroSound > nextSoundTime) {
    sound_ambient_state();
    lastMicroSound = millis();
    nextSoundTime = random(900000, 1200000);
  }

  // Timer 2: Scroll Pause
  static bool scrollPaused = false;
  static unsigned long scrollPauseUntil = 0;
  static unsigned long nextPauseTime = millis() + random(8000, 20000);
  int scrollTriggerLen = 21;
  if (strlen(aiMsg) > scrollTriggerLen) {
    if (!scrollPaused && millis() > nextPauseTime) {
      scrollPaused = true;
      scrollPauseUntil = millis() + random(1500, 3000);
    }
    if (scrollPaused && millis() > scrollPauseUntil) {
      scrollPaused = false;
      nextPauseTime = millis() + random(8000, 20000);
    }
  }

  // Timer 3: Micro-expression Flash (now handled by Yuki pulse)
  // Removed — micro-expressions are decided by the pulse LLM call


  if(currentMode == FACE && strcmp(aiMsg, "[THINKING_FACE] I'm bored... play a game?") == 0 && digitalRead(BTN_SEL) == LOW && millis() - lastButtonPress > debounceDelay) {
    lastButtonPress = millis(); lastActivity = millis();
    lastInteraction = millis(); lastIdleCheck = millis();
    currentMode = GAME_SELECT; sound_confirm();
    menuIdx = 0; // Reset menu selection
  } 
  // Quick Response Combo: Modifier + Down
  else if (digitalRead(BTN_MODIFIER) == LOW && digitalRead(BTN_DN) == LOW && millis() - lastButtonPress > debounceDelay) {
    lastButtonPress = millis(); lastActivity = millis();
    dnBtnHeld = false; // CANCEL touch logic if modifier is used
    menuIdx = 0;
    currentMode = QUICK_RESPONSE;
    sound_confirm();
  }
  // Hidden shortcut: Hold DOWN + SELECT to play Rock Paper Scissors
  else if (digitalRead(BTN_DN) == LOW && digitalRead(BTN_SEL) == LOW && millis() - lastButtonPress > debounceDelay) {
    lastButtonPress = millis(); currentMode = GAME_RPS; sound_confirm();
    dnBtnHeld = false; // CANCEL touch logic
  } else
  if(digitalRead(BTN_SEL) == LOW && millis() - lastButtonPress > debounceDelay) {
    lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis();
    missedMorning = false; // Feature 5: User showed up, cancel missed morning
    // Feature 1: Context-aware return greeting instead of generic "Say hi!"
    {
      unsigned long gap = millis() - lastInteraction;
      static char returnPrompt[160];
      if (gap > 3600000UL) // more than 1 hour
        snprintf(returnPrompt, sizeof(returnPrompt),
          "The user just came back after %.1f hours away. React genuinely — not generically. Affinity:%d. Energy:%.2f. BadDay:%s.",
          gap / 3600000.0f, sys.affinity, yukiEnergy, core_badDayFlag ? "yes" : "no");
      else if (gap > 900000UL) // 15+ minutes
        snprintf(returnPrompt, sizeof(returnPrompt),
          "The user pressed a button after %lu minutes of quiet. Acknowledge it naturally. Affinity:%d.",
          gap / 60000UL, sys.affinity);
      else
        snprintf(returnPrompt, sizeof(returnPrompt),
          "The user is here. Say something brief and warm. Affinity:%d.", sys.affinity);
      syncAI(returnPrompt, true, FACE, false, true);
    }
  }
  if(digitalRead(BTN_UP) == LOW && millis() - lastButtonPress > debounceDelay) { lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis(); if (timeClient.isTimeSet()) sys.lastConversationTime = timeClient.getEpochTime(); syncAI("Tell me a joke.", true, FACE, false, true); }
  
  // --- NEW PHYSICAL TOUCH LOGIC (BTN_DN REPLACEMENT) ---
  // STABILITY FIX: Explicitly only allow touches in FACE mode with no modifier held
  bool dnState = (currentMode == FACE && digitalRead(BTN_DN) == LOW && digitalRead(BTN_MODIFIER) == HIGH);
  if (dnState && !dnBtnHeld) { 
    dnBtnPressStart = millis();
    dnBtnHeld = true;
    
    // INTERACTION PRIORITY: Cancel micro-expressions/thoughts immediately
    microExpressionUntil = 0; 
    silentThoughtUntil = 0;

    // Start of a tickle?
    if (millis() - lastTicklePress < 400) tickleCount++;
    else tickleCount = 1;
    lastTicklePress = millis();
  } 
  else if (!dnState && dnBtnHeld) { // Release detected
    unsigned long duration = millis() - dnBtnPressStart;
    dnBtnHeld = false;
    lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis();
    lastTouchTime = millis();
    if (timeClient.isTimeSet()) sys.lastConversationTime = timeClient.getEpochTime();

    char touchPrompt[150] = "";
    bool shouldSpeak = false;
    touchAnnoyance++; // Every touch builds a little pressure

    if (tickleCount >= 3 && duration < 300) { // TICKLE
      sound_tickle_tone();
      currentEmotion = LAUGHING;
      if (touchAnnoyance > 6) {
        strncpy(touchPrompt, "Stop tickling me!", sizeof(touchPrompt)-1);
        currentEmotion = ANGRY;
        shouldSpeak = true;
      } else if (random(0, 10) < 4) {
        strncpy(touchPrompt, "Hehe! Tickles!", sizeof(touchPrompt)-1);
        shouldSpeak = true;
      }
    } else if (duration < 400) { // BOOP
      sound_blip();
      currentEmotion = SURPRISED;
      if (random(0, 10) < 3) {
        strncpy(touchPrompt, "Boop!", sizeof(touchPrompt)-1);
        shouldSpeak = true;
      }
    } else if (duration < 1500) { // HEAD PAT
      sound_purr();
      currentEmotion = (sys.affinity > 30) ? LOVE : BLUSHING;
      if (touchAnnoyance > 8) {
        strncpy(touchPrompt, "Enough pats.", sizeof(touchPrompt)-1);
        currentEmotion = SASSY; shouldSpeak = true;
      } else if (currentEmotion == SAD || currentEmotion == SLEEPY) {
        strncpy(touchPrompt, "You being here helps.", sizeof(touchPrompt)-1);
        shouldSpeak = true;
      } else if (core_badDayFlag) {
        strncpy(touchPrompt, "I needed that.", sizeof(touchPrompt)-1);
        core_badDayFlag = false; // Pat helps recovery
        shouldSpeak = true;
      } else if (sys.affinity > 50) {
        strncpy(touchPrompt, "I was hoping you'd do that.", sizeof(touchPrompt)-1);
        if (random(0, 10) < 7) shouldSpeak = true;
      } else {
        strncpy(touchPrompt, "That feels nice.", sizeof(touchPrompt)-1);
        if (random(0, 10) < 6) shouldSpeak = true;
      }
    } else { // LONG HUG / EMBRACE
      sound_purr();
      if (sys.affinity < 0) {
        strncpy(touchPrompt, "Not comfortable.", sizeof(touchPrompt)-1);
        currentEmotion = FLUSTERED; shouldSpeak = true;
      } else if (core_badDayFlag) {
        strncpy(touchPrompt, "...thank you. I needed this.", sizeof(touchPrompt)-1);
        strcat(touchPrompt, " [AFFINITY:+3]");
        core_badDayFlag = false; // Embrace heals a bad day
        touchAnnoyance = 0; shouldSpeak = true;
      } else {
        strncpy(touchPrompt, "Safe with you.", sizeof(touchPrompt)-1);
        strcat(touchPrompt, " [AFFINITY:+2]");
        touchAnnoyance = 0; shouldSpeak = true;
      }
    }

    // Local action: Update face and timer immediately regardless of AI call
    emotionSetTime = millis();
    drawAesthetica(); 

    if (shouldSpeak && strlen(touchPrompt) > 0) {
      syncAI(touchPrompt, false, FACE, true, true);
    } else if (strlen(touchPrompt) > 0) {
      // If she doesn't speak, she still "remembers" the event happened for next time.
      logLocalChat("Event", touchPrompt);
    }
  }

  if(digitalRead(BTN_BACK) == LOW && millis() - lastButtonPress > debounceDelay) { lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis(); currentMode = MENU; sound_confirm(); }
  
  // Handle text scrolling (trigger length depends on text size)
  static uint32_t scrollInterval = 300;
  if (strlen(aiMsg) > scrollTriggerLen && !scrollPaused && millis() - lastScroll > scrollInterval) {
    scrollOffset = (scrollOffset + 1) % strlen(aiMsg);
    lastScroll = millis();

    uint32_t nextBase = 300;
    static bool scrollFast = false;
    switch (currentEmotion) {
      case SLEEPY:    nextBase = 700; break;
      case ANGRY:     nextBase = 120; break;
      case LAUGHING:
      case HAPPY:     nextBase = 180; break;
      case NEUTRAL:   nextBase = 300; break;
      case CONFUSED: 
        scrollFast = !scrollFast;
        nextBase = scrollFast ? 150 : 600; 
        break;
      case LOVE:
      case SHY:       nextBase = 400; break;
      default:        nextBase = 300; break;
    }

    if (sys.scrollSpeed == 0) scrollInterval = nextBase * 1.7; // Slow
    else if (sys.scrollSpeed == 2) scrollInterval = nextBase * 0.6; // Fast
    else scrollInterval = nextBase; // Normal
  }
}

inline void handleQuickResponseMode() {
  display.clearDisplay(); display.setCursor(0, 0); display.println("-- QUICK CHAT --");
  display.drawFastHLine(0, 10, 128, WHITE);
  
  int startI = (menuIdx > 3) ? menuIdx - 3 : 0;
  char buffer[32]; // Temporary buffer to read from Flash

  for(int i=0; i<5 && (startI+i) < numQuickResponses; i++){
    int idx = startI + i;
    // Read the string pointer from Flash memory
    strcpy_P(buffer, (char*)pgm_read_ptr(&(quickResponses[idx])));
    
    display.setCursor(10, 15+(i*9));
    display.print(menuIdx == idx ? "> " : "  ");
    display.println(buffer);
  }
  display.display();
  
  if(digitalRead(BTN_UP) == LOW && millis() - lastButtonPress > debounceDelay) { lastButtonPress = millis(); lastActivity = millis(); menuIdx = (menuIdx + numQuickResponses - 1) % numQuickResponses; sound_blip(); }
  if(digitalRead(BTN_DN) == LOW && millis() - lastButtonPress > debounceDelay) { lastButtonPress = millis(); lastActivity = millis(); menuIdx = (menuIdx + 1) % numQuickResponses; sound_blip(); }
  
  if(digitalRead(BTN_SEL) == LOW && millis() - lastButtonPress > debounceDelay) {
    lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis();
    strncpy_P(buffer, (char*)pgm_read_ptr(&(quickResponses[menuIdx])), 31);
    syncAI(buffer, true, FACE, false, true);
    currentMode = FACE;
    sound_confirm();
  }
  if(digitalRead(BTN_BACK) == LOW && millis() - lastButtonPress > debounceDelay) { 
    lastButtonPress = millis(); lastActivity = millis(); 
    currentMode = FACE; 
    // Flush touch states when returning to face
    dnBtnHeld = false;
    tickleCount = 0;
    sound_confirm();
  }
}

void handleMenuMode() {
  display.clearDisplay(); display.setCursor(0, 0); display.println("-- MENU --");
  // Move menu strings to PROGMEM to save RAM
  static const char m0[] PROGMEM = "WIFI CFG"; static const char m1[] PROGMEM = "MQTT CONFIG";
  static const char m2[] PROGMEM = "SYNC AI"; static const char m3[] PROGMEM = "SOUND CFG";
  static const char m4[] PROGMEM = "AUTO THINK"; static const char m5[] PROGMEM = "CUSTOM SYNC";
  static const char m6[] PROGMEM = "VIEW SCANS"; static const char m7[] PROGMEM = "VIEW MSGS";
  static const char m8[] PROGMEM = "SEND MSG"; static const char m9[] PROGMEM = "TERMINAL";
  static const char m10[] PROGMEM = "SYSTEM INFO"; static const char m11[] PROGMEM = "VIEW EMOTIONS";
  static const char m12[] PROGMEM = "GAMES"; static const char m13[] PROGMEM = "MEMORY REBOOT";
  static const char m14[] PROGMEM = "SCROLL SPEED"; static const char m15[] PROGMEM = "SHUTDOWN";
  static const char m16[] PROGMEM = "EXIT";
  static const char* const menuItems[] PROGMEM = {m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16};

  int menuLen = 17;
  int startI = (menuIdx > 3) ? menuIdx - 3 : 0;
  char buffer[20];
  
  for(int i=0; i<5 && (startI+i) < menuLen; i++){
    int idx = startI + i;
    strcpy_P(buffer, (char*)pgm_read_ptr(&(menuItems[idx])));
    display.setCursor(10, 15+(i*10));
    display.print(menuIdx == idx ? "> " : "  ");
    display.print(buffer);
    if(idx == 2) { display.print(": >"); }
    if(idx == 4) { display.print(": "); display.print(sys.autoThink ? "ON" : "OFF"); }
    if(idx == 14) { 
      display.print(": "); 
      if(sys.scrollSpeed == 0) display.print("SLOW");
      else if(sys.scrollSpeed == 1) display.print("NORM");
      else display.print("FAST");
    }
    display.println();
  }
  display.display();
  
  // Navigation
  if(digitalRead(BTN_UP) == LOW && millis() - lastButtonPress > debounceDelay) { lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis(); menuIdx=(menuIdx + menuLen - 1)%menuLen; sound_blip(); }
  if(digitalRead(BTN_DN) == LOW && millis() - lastButtonPress > debounceDelay) { lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis(); menuIdx=(menuIdx+1)%menuLen; sound_blip(); }
  if(digitalRead(BTN_SEL) == LOW && millis() - lastButtonPress > debounceDelay) {
    lastButtonPress = millis();
    lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis();
    if(menuIdx == 0) { menuIdx = 0; currentMode = WIFI_CONFIG; }
    else if(menuIdx == 1) { menuIdx = 0; currentMode = MQTT_CONFIG; }
    else if(menuIdx == 2) {
      static const char* const moodPrompts[] PROGMEM = {
        "How are you feeling right now?",
        "Care to share your mood?",
        "Tell me how your day's been.",
        "What's your vibe today?",
        "%s, how do you feel today?"
      };
      char b[64]; strcpy_P(b, (char*)pgm_read_ptr(&(moodPrompts[random(0,4)])));
      syncAI(b, true, FACE, false, true);
    }
    else if(menuIdx == 3) { menuIdx = 0; currentMode = SOUND_MENU; }
    else if(menuIdx == 4) { sys.autoThink = !sys.autoThink; }
    else if(menuIdx == 5) { inputPass[0] = '\0'; charIdx = 2; currentMode = CUSTOM_PROMPT; }
    else if(menuIdx == 6) { viewIdx = 0; currentMode = VIEW_SCANS; }
    else if(menuIdx == 7) { viewIdx = 0; currentMode = VIEW_MSGS; }
    else if(menuIdx == 8) { strncpy(selectedRecipient, PHONE_CONTACT_ID, 31); inputPass[0] = '\0'; charIdx = 2; currentMode = CUSTOM_MESSAGE; }
    else if(menuIdx == 9) { menuIdx = 0; currentMode = TERMINAL; }
    else if(menuIdx == 10) { currentMode = SYSTEM_INFO; }
    else if(menuIdx == 11) { menuIdx = 0; currentMode = VIEW_EMOTIONS; }
    else if(menuIdx == 12) { menuIdx = 0; currentMode = GAME_SELECT; }
    else if(menuIdx == 13) { menuIdx = 0; currentMode = MEMORY_REBOOT; }
    else if(menuIdx == 14) { sys.scrollSpeed = (sys.scrollSpeed + 1) % 3; }
    else if(menuIdx == 15) { menuIdx = 0; currentMode = SHUTDOWN_CONFIRM; }
    else { currentMode = FACE; statusBarVisibleUntil = millis() + 3000; }
    saveSys(); sound_confirm();
    dnBtnHeld = false; // Safety flush
  }
  if(digitalRead(BTN_BACK) == LOW && millis() - lastButtonPress > debounceDelay) { 
    lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis(); 
    currentMode = FACE; statusBarVisibleUntil = millis() + 3000; 
    dnBtnHeld = false; tickleCount = 0; // Safety flush
    sound_confirm(); 
  }
}

void handleSoundMenuMode() {
  static bool editingVolume = false;
  display.clearDisplay(); display.setCursor(0, 0); display.println("-- SOUND CFG --");
  static const char l0[] PROGMEM = "MASTER"; static const char l1[] PROGMEM = "VOLUME";
  static const char l2[] PROGMEM = "AMBIENT"; static const char l3[] PROGMEM = "BOOT TONE";
  static const char l4[] PROGMEM = "SCENTS"; static const char l5[] PROGMEM = "TTS TEST";
  static const char l6[] PROGMEM = "BEEPS"; static const char l7[] PROGMEM = "SCREEN"; static const char l8[] PROGMEM = "BACK";
  static const char* const soundLabels[] PROGMEM = {l0, l1, l2, l3, l4, l5, l6, l7, l8};

  int soundMenuLen = 9;
  int startI = (menuIdx > 3) ? menuIdx - 3 : 0;
  char buffer[20];

  for (int i = 0; i < 5 && (startI + i) < soundMenuLen; i++) {
    int idx = startI + i;
    strcpy_P(buffer, (char*)pgm_read_ptr(&(soundLabels[idx])));
    display.setCursor(10, 15 + (i * 10));
    display.print(menuIdx == idx ? "> " : "  ");
    display.print(buffer);
    if (idx == 0) { display.print(": "); display.print(sys.soundOn ? "ON" : "OFF"); }
    else if (idx == 1) {
      display.print(": ");
      if (editingVolume && menuIdx == idx) display.print("*");
      display.print(sys.soundVolume);
      display.print("%");
    }
    else if (idx == 2) { display.print(": "); display.print(sys.ambientSounds ? "ON" : "OFF"); }
    else if (idx == 3) { display.print(": "); display.print(sys.bootSound ? "ON" : "OFF"); }
    else if (idx == 4) { display.print(": "); display.print(sys.soundScents ? "ON" : "OFF"); }
    else if (idx == 6) { display.print(": "); display.print(sys.beepsOn ? "ON" : "OFF"); }
    else if (idx == 7) { display.print(": "); display.print(sys.screenSaver ? "ON" : "OFF"); }
    display.println();
  }
  display.display();

  if (digitalRead(BTN_UP) == LOW && millis() - lastButtonPress > debounceDelay) {
    lastButtonPress = millis(); lastActivity = millis();
    if (editingVolume) {
      sys.soundVolume = min(100, sys.soundVolume + 1); saveSys(); sound_blip();
    } else {
      menuIdx = (menuIdx + soundMenuLen - 1) % soundMenuLen; sound_blip();
    }
  }
  if (digitalRead(BTN_DN) == LOW && millis() - lastButtonPress > debounceDelay) {
    lastButtonPress = millis(); lastActivity = millis();
    if (editingVolume) {
      sys.soundVolume = max(0, sys.soundVolume - 1); saveSys(); sound_blip();
    } else {
      menuIdx = (menuIdx + 1) % soundMenuLen; sound_blip();
    }
  }
  if (digitalRead(BTN_SEL) == LOW && millis() - lastButtonPress > debounceDelay) {
    lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis();
    if (editingVolume) {
      editingVolume = false; sound_confirm();
    } else if (menuIdx == 0) { sys.soundOn = !sys.soundOn; saveSys(); sound_confirm(); }
    else if (menuIdx == 1) { editingVolume = true; sound_blip(); }
    else if (menuIdx == 2) { sys.ambientSounds = !sys.ambientSounds; saveSys(); sound_confirm(); }
    else if (menuIdx == 3) { sys.bootSound = !sys.bootSound; saveSys(); sound_confirm(); }
    else if (menuIdx == 4) { sys.soundScents = !sys.soundScents; saveSys(); sound_confirm(); }
    else if (menuIdx == 5) { menuIdx = 0; currentMode = TTS_TEST_MENU; sound_confirm(); }
    else if (menuIdx == 6) { sys.beepsOn = !sys.beepsOn; saveSys(); sound_confirm(); }
    else if (menuIdx == 7) { sys.screenSaver = !sys.screenSaver; saveSys(); sound_confirm(); }
    else { menuIdx = 2; currentMode = MENU; }
  }
  if (digitalRead(BTN_BACK) == LOW && millis() - lastButtonPress > debounceDelay) {
    lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis();
    if (editingVolume) editingVolume = false;
    else { menuIdx = 2; currentMode = MENU; sound_confirm(); }
  }
}

void handleTtsTestMenuMode() {
  static bool ttsEdit = false;   // edit-state per knob row (like SOUND_MENU volume)
  display.clearDisplay(); display.setCursor(0, 0); display.println("-- VOICE CFG --");
  static const char t0[] PROGMEM = "VOICE"; static const char t1[] PROGMEM = "NAME";
  static const char t2[] PROGMEM = "RATE"; static const char t3[] PROGMEM = "PITCH";
  static const char t4[] PROGMEM = "PULSE"; static const char t5[] PROGMEM = "POL";
  static const char t6[] PROGMEM = "DEADBAND"; static const char t7[] PROGMEM = "SPREAD";
  static const char t8[] PROGMEM = "VOLGAIN"; static const char t9[] PROGMEM = "MONITOR";
  static const char t10[] PROGMEM = "REPLAY"; static const char t11[] PROGMEM = "BACK";
  static const char* const ttsLabels[] PROGMEM = {t0, t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11};

  // Refresh the selectable voice list on (re)entering the menu — one small GET
  // per visit, served by the worker task in the background.
  {
    static unsigned long ttsVoiceRefreshAt = 0;
    if (sys.voiceOn && (ttsVoiceRefreshAt == 0 || millis() - ttsVoiceRefreshAt > 60000)) {
      ttsVoiceRefreshAt = millis();
      ttsRequestVoiceRefresh();
    }
  }
  // Snap a persisted voice the relay no longer offers to the first available
  {
    int vc = ttsGetVoiceCount();
    if (vc > 0 && sys.voiceName[0]) {
      bool inList = false;
      for (int i = 0; i < vc; i++) {
        if (!strcmp(ttsGetVoiceName(i), sys.voiceName)) { inList = true; break; }
      }
      if (!inList) {
        char oldName[32];
        strncpy(oldName, sys.voiceName, sizeof(oldName) - 1);
        oldName[sizeof(oldName) - 1] = '\0';
        ttsSetVoice(ttsGetVoiceName(0)); saveSys();
        LOGW("TTS", "voice %s unavailable, snapped to %s", oldName, sys.voiceName);
      }
    }
  }

  const int ttsMenuLen = 12;
  int startI = (menuIdx > 3) ? menuIdx - 3 : 0;
  char buffer[16];
  char nbuf[24];

  for (int i = 0; i < 5 && (startI + i) < ttsMenuLen; i++) {
    int idx = startI + i;
    strcpy_P(buffer, (char*)pgm_read_ptr(&(ttsLabels[idx])));
    display.setCursor(10, 15 + (i * 10));
    display.print(menuIdx == idx ? "> " : "  ");
    display.print(buffer);
    if (idx == 0) { display.print(": "); display.print(sys.voiceOn ? "ON" : "OFF"); }
    else if (idx == 1) {
      display.print(": "); if (ttsEdit && menuIdx == idx) display.print("*");
      const char* vn = sys.voiceName;        // short form: strip "xx-XX-" prefix
      const char* p1 = strchr(vn, '-');
      const char* p2 = p1 ? strchr(p1 + 1, '-') : NULL;
      if (p2) vn = p2 + 1;
      strncpy(nbuf, vn, sizeof(nbuf) - 1); nbuf[sizeof(nbuf) - 1] = '\0';
      display.print(nbuf);
    }
    else if (idx == 2) { display.print(": "); if (ttsEdit && menuIdx == idx) display.print("*"); display.print(ttsGetRate() >= 0 ? "+" : ""); display.print(ttsGetRate()); display.print("%"); }
    else if (idx == 3) { display.print(": "); if (ttsEdit && menuIdx == idx) display.print("*"); display.print(ttsGetPitch() >= 0 ? "+" : ""); display.print(ttsGetPitch()); display.print("Hz"); }
    else if (idx == 4) { display.print(": "); if (ttsEdit && menuIdx == idx) display.print("*"); display.print(ttsGetPulse()); display.print("us"); }
    else if (idx == 5) { display.print(": "); if (ttsEdit && menuIdx == idx) display.print("*"); display.print(ttsGetPolarity() ? "INV" : "NORM"); }
    else if (idx == 6) { display.print(": "); if (ttsEdit && menuIdx == idx) display.print("*"); display.print(ttsGetDeadband()); }
    else if (idx == 7) { display.print(": "); if (ttsEdit && menuIdx == idx) display.print("*"); display.print(ttsGetSpread() == 0 ? "LIN" : (ttsGetSpread() == 1 ? "1.5X" : (ttsGetSpread() == 2 ? "2X" : "CLIP"))); }
    else if (idx == 8) { display.print(": "); if (ttsEdit && menuIdx == idx) display.print("*"); display.print(ttsGetVolGain()); display.print("%"); }
    else if (idx == 9) { display.print(": "); if (ttsEdit && menuIdx == idx) display.print("*"); display.print(ttsGetMonitor() ? "ON" : "OFF"); }
    display.println();
  }
  display.display();

  if (digitalRead(BTN_UP) == LOW && millis() - lastButtonPress > debounceDelay) {
    lastButtonPress = millis(); lastActivity = millis();
    if (ttsEdit) {
      if (menuIdx == 1) {   // NAME: cycle the selectable list
        int vc = ttsGetVoiceCount();
        if (vc > 0) {
          int ci = 0;
          for (int i = 0; i < vc; i++) if (!strcmp(ttsGetVoiceName(i), sys.voiceName)) { ci = i; break; }
          ttsSetVoice(ttsGetVoiceName((ci + 1) % vc)); sound_blip();
        } else { sound_error(); }
      } else if (menuIdx == 2) {
        ttsSetRate(constrain(ttsGetRate() + 5, -50, 50)); sound_blip();
      } else if (menuIdx == 3) {
        ttsSetPitch(constrain(ttsGetPitch() + 5, -50, 50)); sound_blip();
      } else if (menuIdx == 4) {
        uint32_t v[] = {55, 45, 35, 25}; uint32_t c = ttsGetPulse();
        int i = 0; while (v[i] != c) i++; ttsSetPulse(v[(i + 1) % 4]); sys.voicePulse = ttsGetPulse(); sound_blip();
      } else if (menuIdx == 5) {
        ttsSetPolarity(ttsGetPolarity() ? 0 : 1); sys.voicePolarity = ttsGetPolarity(); sound_blip();
      } else if (menuIdx == 6) {
        int v[] = {0, 4, 8, 16, 32}; int c = ttsGetDeadband();
        int i = 0; while (v[i] != c) i++; ttsSetDeadband(v[(i + 1) % 5]); sys.voiceDeadband = ttsGetDeadband(); sound_blip();
      } else if (menuIdx == 7) {
        ttsSetSpread((ttsGetSpread() + 1) % 4); sys.voiceSpread = ttsGetSpread(); sound_blip();
      } else if (menuIdx == 8) {
        int v[] = {50, 75, 100, 125, 150, 200}; int c = ttsGetVolGain();
        int i = 0; while (v[i] != c) i++; ttsSetVolGain(v[(i + 1) % 6]); sys.voiceGain = ttsGetVolGain(); sound_blip();
      } else if (menuIdx == 9) {
        ttsSetMonitor(!ttsGetMonitor()); sound_blip();
      }
      saveSys();   // persist knob edits like VOLUME does
    } else {
      menuIdx = (menuIdx + ttsMenuLen - 1) % ttsMenuLen; sound_blip();
    }
  }
  if (digitalRead(BTN_DN) == LOW && millis() - lastButtonPress > debounceDelay) {
    lastButtonPress = millis(); lastActivity = millis();
    if (ttsEdit) {
      if (menuIdx == 1) {   // NAME: cycle backward
        int vc = ttsGetVoiceCount();
        if (vc > 0) {
          int ci = 0;
          for (int i = 0; i < vc; i++) if (!strcmp(ttsGetVoiceName(i), sys.voiceName)) { ci = i; break; }
          ttsSetVoice(ttsGetVoiceName((ci + vc - 1) % vc)); sound_blip();
        } else { sound_error(); }
      } else if (menuIdx == 2) {
        ttsSetRate(constrain(ttsGetRate() - 5, -50, 50)); sound_blip();
      } else if (menuIdx == 3) {
        ttsSetPitch(constrain(ttsGetPitch() - 5, -50, 50)); sound_blip();
      } else if (menuIdx == 4) {
        uint32_t v[] = {55, 45, 35, 25}; uint32_t c = ttsGetPulse();
        int i = 0; while (v[i] != c) i++; ttsSetPulse(v[(i + 3) % 4]); sys.voicePulse = ttsGetPulse(); sound_blip();
      } else if (menuIdx == 5) {
        ttsSetPolarity(ttsGetPolarity() ? 0 : 1); sys.voicePolarity = ttsGetPolarity(); sound_blip();
      } else if (menuIdx == 6) {
        int v[] = {0, 4, 8, 16, 32}; int c = ttsGetDeadband();
        int i = 0; while (v[i] != c) i++; ttsSetDeadband(v[(i + 4) % 5]); sys.voiceDeadband = ttsGetDeadband(); sound_blip();
      } else if (menuIdx == 7) {
        ttsSetSpread((ttsGetSpread() + 3) % 4); sys.voiceSpread = ttsGetSpread(); sound_blip();
      } else if (menuIdx == 8) {
        int v[] = {50, 75, 100, 125, 150, 200}; int c = ttsGetVolGain();
        int i = 0; while (v[i] != c) i++; ttsSetVolGain(v[(i + 5) % 6]); sys.voiceGain = ttsGetVolGain(); sound_blip();
      } else if (menuIdx == 9) {
        ttsSetMonitor(!ttsGetMonitor()); sound_blip();
      }
      saveSys();   // persist knob edits like VOLUME does
    } else {
      menuIdx = (menuIdx + 1) % ttsMenuLen; sound_blip();
    }
  }
  if (digitalRead(BTN_SEL) == LOW && millis() - lastButtonPress > debounceDelay) {
    lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis();
    if (ttsEdit) {
      ttsEdit = false; sound_confirm();
    } else if (menuIdx == 0) {
      sys.voiceOn = !sys.voiceOn; saveSys(); sound_confirm();
    } else if (menuIdx >= 1 && menuIdx <= 9) {
      ttsEdit = true; sound_blip();
    } else if (menuIdx == 10) {
      display.clearDisplay(); display.setCursor(0, 20); display.println("Speaking...");
      display.display(); sound_confirm();
      ttsReplaySpeak();                       // blocking, like normal speech
    } else if (menuIdx == 11) {
      ttsEdit = false; menuIdx = 5; currentMode = SOUND_MENU; sound_confirm();
    }
  }
  if (digitalRead(BTN_BACK) == LOW && millis() - lastButtonPress > debounceDelay) {
    lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis();
    if (ttsEdit) { ttsEdit = false; sound_confirm(); }
    else { menuIdx = 5; currentMode = SOUND_MENU; sound_confirm(); }
  }
}

void handleWifiConfigMode() {
  static const char w0[] PROGMEM = "SCAN"; static const char w1[] PROGMEM = "STATUS";
  static const char w2[] PROGMEM = "RECONNECT"; static const char w3[] PROGMEM = "DISCONNECT";
  static const char w4[] PROGMEM = "TX POWER"; static const char w5[] PROGMEM = "FORGET";
  static const char w6[] PROGMEM = "BACK";
  static const char* const wifiItems[] PROGMEM = {w0, w1, w2, w3, w4, w5, w6};
  int wifiMenuLen = 7;
  static bool txPowerEditing = false;
  static int txPowerLevel = 4; // Default index: 0=Low 8dBm, 1=Med-Low 11, 2=Med 14, 3=Med-High 17, 4=High 19.5, 5=Max 21
  static const int txPowerValues[] = {8, 11, 14, 17, 20, 22}; // dBm
  static const char* const txPowerLabels[] = {"Low 8dBm", "Med-Low 11dBm", "Med 14dBm", "Med-High 17dBm", "High 19.5dBm", "Max 21dBm"};

  display.clearDisplay(); display.setCursor(0, 0); display.println("-- WIFI CFG --");
  for (int i = 0; i < wifiMenuLen; i++) {
    char b[16]; strcpy_P(b, (char*)pgm_read_ptr(&(wifiItems[i])));
    display.setCursor(10, 15 + (i * 10));
    display.print(menuIdx == i ? "> " : "  ");
    if (i == 1 && menuIdx != i) {
      display.print(b);
      if (WiFi.status() == WL_CONNECTED) display.print(" OK");
      else display.print(" --");
    } else if (i == 4 && menuIdx == i && txPowerEditing) {
      display.print(txPowerLabels[txPowerLevel]);
    } else {
      display.print(b);
    }
    display.println();
  }
  display.display();

  if (digitalRead(BTN_UP) == LOW && millis() - lastButtonPress > debounceDelay) {
    lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis();
    if (txPowerEditing) {
      txPowerLevel = (txPowerLevel + 1) % 6;
      WiFi.setTxPower((wifi_power_t)txPowerValues[txPowerLevel]);
      sound_blip();
    } else {
      menuIdx = (menuIdx + wifiMenuLen - 1) % wifiMenuLen; sound_blip();
    }
  }
  if (digitalRead(BTN_DN) == LOW && millis() - lastButtonPress > debounceDelay) {
    lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis();
    if (txPowerEditing) {
      txPowerLevel = (txPowerLevel + 5) % 6;
      WiFi.setTxPower((wifi_power_t)txPowerValues[txPowerLevel]);
      sound_blip();
    } else {
      menuIdx = (menuIdx + 1) % wifiMenuLen; sound_blip();
    }
  }
  if (digitalRead(BTN_SEL) == LOW && millis() - lastButtonPress > debounceDelay) {
    lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis();
    if (menuIdx == 4) {
      // TX POWER: toggle editing mode
      if (!txPowerEditing) {
        txPowerEditing = true;
        sound_confirm();
      } else {
        // Save and exit editing
        txPowerEditing = false;
        sys.txPower = txPowerValues[txPowerLevel];
        saveSys();
        LOGI("WIFI","TX Power set to %d dBm", sys.txPower);
        sound_confirm();
      }
      return;
    }
    txPowerEditing = false; // Any other select cancels editing
    if (menuIdx == 0) {
      display.clearDisplay(); display.setCursor(0, 0); display.println("Scanning..."); display.display();
      foundNetworks = WiFi.scanNetworks();
      LOGI("WIFI","Scan complete: %d networks found", foundNetworks);
      for (int si = 0; si < min(foundNetworks, 5); si++) {
        LOGI("WIFI","  [%d] %s (%d dBm)", si, WiFi.SSID(si).c_str(), WiFi.RSSI(si));
      }
      heavyOpCooldown();
      scanIdx = 0;
      currentMode = SCAN_LIST;
      if (foundNetworks > 5) { currentEmotion = SURPRISED; emotionSetTime = millis(); sound_surprised(); }
      if (canAllocJson(1024)) {
        StaticJsonDocument<1024> doc;
        JsonArray arr = doc.to<JsonArray>();
        for (int i = 0; i < foundNetworks; i++) { JsonObject obj = arr.add<JsonObject>(); obj["ssid"] = WiFi.SSID(i); obj["rssi"] = WiFi.RSSI(i); yield(); }
        size_t p = serializeJson(doc, workspace, sizeof(workspace));
        atomicWriteFile("/scans.json", workspace, p);
      } else { LOGW("UI","Skipping save scans - low heap"); }
    } else if (menuIdx == 1) {
      display.clearDisplay(); display.setCursor(0, 0); display.println("-- WIFI STATUS --");
      display.setCursor(0, 10);
      if (WiFi.status() == WL_CONNECTED) {
        display.print("SSID: "); display.println(WiFi.SSID());
        display.print("RSSI: "); display.print(WiFi.RSSI()); display.println(" dBm");
        display.print("IP: "); display.println(WiFi.localIP().toString().c_str());
        display.print("GW: "); display.println(WiFi.gatewayIP().toString().c_str());
        display.print("MAC: "); display.println(WiFi.macAddress().c_str());
      } else {
        display.println("Not connected");
        display.print("Saved: "); display.println(sys.ssid);
      }
      display.setCursor(0, 56); display.println("Any btn to exit");
      display.display();
      unsigned long statusStart = millis();
      while (millis() - statusStart < 8000) {
        if (digitalRead(BTN_UP) == LOW || digitalRead(BTN_DN) == LOW ||
            digitalRead(BTN_SEL) == LOW || digitalRead(BTN_BACK) == LOW) { delay(200); break; }
        delay(50);
      }
    } else if (menuIdx == 2) {
      display.clearDisplay(); display.setCursor(0, 10); display.println("Reconnecting..."); display.display();
      WiFi.disconnect(); delay(500);
      tryConnect();
    } else if (menuIdx == 3) {
      display.clearDisplay(); display.setCursor(0, 10); display.println("Disconnecting..."); display.display();
      WiFi.disconnect(true); delay(300);
      WiFi.mode(WIFI_OFF);
    } else if (menuIdx == 5) {
      display.clearDisplay(); display.setCursor(0, 10); display.println("Forget WiFi?"); display.setCursor(0, 25); display.println("SEL=Yes BACK=No"); display.display();
      unsigned long waitStart = millis();
      bool confirmed = false;
      while (millis() - waitStart < 3000) {
        if (digitalRead(BTN_SEL) == LOW) { confirmed = true; break; }
        if (digitalRead(BTN_BACK) == LOW) { break; }
        delay(50);
      }
      if (confirmed) {
        display.clearDisplay(); display.setCursor(0, 10); display.println("Clearing WiFi..."); display.display();
        sys.ssid[0] = '\0'; sys.pass[0] = '\0'; saveSys();
        nvs_flash_erase(); nvs_flash_init();
        delay(500); ESP.restart();
      }
    } else {
      menuIdx = 0; currentMode = MENU; sound_confirm();
    }
    sound_confirm();
  }
  if (digitalRead(BTN_BACK) == LOW && millis() - lastButtonPress > debounceDelay) {
    lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis();
    menuIdx = 0; currentMode = MENU; sound_confirm();
  }
}

void handleMqttConfigMode() {
  static int mqttMenuIdx = 0;
  static bool editingField = false;
  static int editFieldIdx = -1;
  static char editBuffer[64];
  static int editBufferLen = 0;
  static bool showTopics = false;

  // Build field list
  static const char f0[] PROGMEM = "Device ID"; static const char f1[] PROGMEM = "Character Name";
  static const char f2[] PROGMEM = "MQTT Broker"; static const char f3[] PROGMEM = "MQTT Port";
  static const char f4[] PROGMEM = "Phone Contact ID"; static const char f5[] PROGMEM = "Phone IP";
  static const char f6[] PROGMEM = "TTS Relay URL"; static const char f7[] PROGMEM = "SHOW TOPICS";
  static const char f8[] PROGMEM = "BACK";
  static const char* const mqttItems[] PROGMEM = {f0, f1, f2, f3, f4, f5, f6, f7, f8};
  int mqttMenuLen = 9;

  if (!editingField && !showTopics) {
    // Main list view
    display.clearDisplay(); display.setCursor(0, 0); display.println("-- MQTT CFG --");
    int startI = (mqttMenuIdx > 3) ? mqttMenuIdx - 3 : 0;
    char buffer[20];
    for(int i=0; i<5 && (startI+i) < mqttMenuLen; i++){
      int idx = startI + i;
      char label[20]; strcpy_P(label, (char*)pgm_read_ptr(&(mqttItems[idx])));
      display.setCursor(10, 15+(i*10));
      display.print(mqttMenuIdx == idx ? "> " : "  ");
      display.print(label);
      if (idx <= 6) {
        // Show current value for editable fields
        static char portBuf[8];
        const char* val = "";
        if (idx == 0) val = (strlen(sys.deviceId) ? sys.deviceId : DEVICE_ID);
        else if (idx == 1) val = (strlen(sys.characterName) ? sys.characterName : CHARACTER_NAME);
        else if (idx == 2) val = (strlen(sys.mqttServer) ? sys.mqttServer : mqtt_server);
        else if (idx == 3) {
          int port = sys.mqttPort ? sys.mqttPort : mqtt_port;
          itoa(port, portBuf, 10);
          val = portBuf;
        }
        else if (idx == 4) val = (strlen(sys.phoneContactId) ? sys.phoneContactId : PHONE_CONTACT_ID);
        else if (idx == 5) val = (strlen(sys.phoneIP_mqtt) ? sys.phoneIP_mqtt : sys.phoneIP);
        else if (idx == 6) val = (strlen(sys.ttsRelayUrl) ? sys.ttsRelayUrl : TTS_RELAY_URL);
        char valBuf[16];
        formatMenuItem(valBuf, sizeof(valBuf), label, val);
        display.print(valBuf);
      }
      display.println();
    }
    display.display();

    if(digitalRead(BTN_UP) == LOW && millis() - lastButtonPress > debounceDelay) {
      lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis();
      mqttMenuIdx = (mqttMenuIdx + mqttMenuLen - 1) % mqttMenuLen; sound_blip();
    }
    if(digitalRead(BTN_DN) == LOW && millis() - lastButtonPress > debounceDelay) {
      lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis();
      mqttMenuIdx = (mqttMenuIdx + 1) % mqttMenuLen; sound_blip();
    }
    if(digitalRead(BTN_SEL) == LOW && millis() - lastButtonPress > debounceDelay) {
      lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis();
      if (mqttMenuIdx <= 6) {
        // Start editing this field
        editingField = true;
        editFieldIdx = mqttMenuIdx;
        editBuffer[0] = '\0';
        editBufferLen = 0;
        if (mqttMenuIdx == 0) { strncpy(editBuffer, strlen(sys.deviceId) ? sys.deviceId : DEVICE_ID, sizeof(editBuffer)-1); }
        else if (mqttMenuIdx == 1) { strncpy(editBuffer, strlen(sys.characterName) ? sys.characterName : CHARACTER_NAME, sizeof(editBuffer)-1); }
        else if (mqttMenuIdx == 2) { strncpy(editBuffer, strlen(sys.mqttServer) ? sys.mqttServer : mqtt_server, sizeof(editBuffer)-1); }
        else if (mqttMenuIdx == 3) { 
          int port = sys.mqttPort ? sys.mqttPort : mqtt_port; 
          itoa(port, editBuffer, 10); 
        }
        else if (mqttMenuIdx == 4) { strncpy(editBuffer, strlen(sys.phoneContactId) ? sys.phoneContactId : PHONE_CONTACT_ID, sizeof(editBuffer)-1); }
        else if (mqttMenuIdx == 5) { strncpy(editBuffer, strlen(sys.phoneIP_mqtt) ? sys.phoneIP_mqtt : sys.phoneIP, sizeof(editBuffer)-1); }
        else if (mqttMenuIdx == 6) { strncpy(editBuffer, strlen(sys.ttsRelayUrl) ? sys.ttsRelayUrl : TTS_RELAY_URL, sizeof(editBuffer)-1); }
        editBufferLen = strlen(editBuffer);
        return; // Prevent fallthrough
      } else if (mqttMenuIdx == 7) {
        showTopics = true;
        return; // Prevent fallthrough
      } else {
        // BACK
        mqttMenuIdx = 0;
        currentMode = MENU;
        sound_confirm();
        return; // Early return to prevent fallthrough
      }
    }
    // Small delay to prevent immediate re-trigger when entering edit mode
    if (editingField && millis() - lastButtonPress < 500) {
      // Do nothing, wait for button release
    }
    if(digitalRead(BTN_BACK) == LOW && millis() - lastButtonPress > debounceDelay) {
      lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis();
      currentMode = MENU; sound_confirm();
      return; // Early return to prevent fallthrough
    }
  } else if (editingField) {
    // Debounce guard: ignore residual button press when entering edit mode
    if (millis() - lastButtonPress < 300) return;

    // Keyboard input for the field
    display.clearDisplay(); display.setCursor(0, 0);
    const char* fieldNames[] = {"Device ID", "Char Name", "MQTT Broker", "MQTT Port", "Phone ID", "Phone IP", "TTS URL"};
    display.print(fieldNames[editFieldIdx]); display.print(":"); display.println();
    display.setCursor(0, 15); display.print(editBuffer); display.print("_"); display.println();
    display.setCursor(0, 50); display.println("SEL=save BACK=cancel"); display.display();

    if(digitalRead(BTN_SEL) == LOW && millis() - lastButtonPress > debounceDelay) {
      lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis();
      // Save the field
      if (editFieldIdx == 0) { strncpy(sys.deviceId, editBuffer, sizeof(sys.deviceId)-1); sys.deviceId[sizeof(sys.deviceId)-1] = '\0'; }
      else if (editFieldIdx == 1) { strncpy(sys.characterName, editBuffer, sizeof(sys.characterName)-1); sys.characterName[sizeof(sys.characterName)-1] = '\0'; }
      else if (editFieldIdx == 2) { strncpy(sys.mqttServer, editBuffer, sizeof(sys.mqttServer)-1); sys.mqttServer[sizeof(sys.mqttServer)-1] = '\0'; }
      else if (editFieldIdx == 4) { strncpy(sys.phoneContactId, editBuffer, sizeof(sys.phoneContactId)-1); sys.phoneContactId[sizeof(sys.phoneContactId)-1] = '\0'; }
      else if (editFieldIdx == 5) { strncpy(sys.phoneIP_mqtt, editBuffer, sizeof(sys.phoneIP_mqtt)-1); sys.phoneIP_mqtt[sizeof(sys.phoneIP_mqtt)-1] = '\0'; }
      else if (editFieldIdx == 6) { strncpy(sys.ttsRelayUrl, editBuffer, sizeof(sys.ttsRelayUrl)-1); sys.ttsRelayUrl[sizeof(sys.ttsRelayUrl)-1] = '\0'; }
      else if (editFieldIdx == 3) { sys.mqttPort = atoi(editBuffer); }
      saveSys();
      // Trigger MQTT reconnect if broker/port/ID changed
      if (editFieldIdx <= 3) { mqttForceReconnect = true; }
      editingField = false;
      editFieldIdx = -1;
      sound_confirm();
    }
    if(digitalRead(BTN_BACK) == LOW && millis() - lastButtonPress > debounceDelay) {
      lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis();
      editingField = false; editFieldIdx = -1; sound_error();
      return; // Early return to prevent fallthrough
    }
    // Keyboard input (reuse KEYBOARD logic)
    if(digitalRead(BTN_UP) == LOW && millis() - lastButtonPress > debounceDelay) {
      lastButtonPress = millis(); lastActivity = millis();
      if (editBufferLen < (int)sizeof(editBuffer) - 1) {
        editBuffer[editBufferLen] = chars[charIdx % strlen_P(chars)];
        editBufferLen++; editBuffer[editBufferLen] = '\0'; sound_blip();
      }
    }
    if(digitalRead(BTN_DN) == LOW && millis() - lastButtonPress > debounceDelay) {
      lastButtonPress = millis(); lastActivity = millis();
      if (editBufferLen < (int)sizeof(editBuffer) - 1) {
        editBuffer[editBufferLen] = chars[(charIdx + 1) % strlen_P(chars)];
        editBufferLen++; editBuffer[editBufferLen] = '\0'; sound_blip();
      }
    }
    if(digitalRead(BTN_MODIFIER) == LOW) {
      if (charIdx < (int)strlen_P(chars) - 1) charIdx++; else charIdx = 0;
    } else if (charIdx > 0) charIdx = 0;
  } else if (showTopics) {
    // Show derived topics (read-only)
    display.clearDisplay(); display.setCursor(0, 0); display.println("-- TOPICS --");
    char topicBuf[22]; // 21 chars + null (128px / 6px font = 21 chars)
    const char* devId = (strlen(sys.deviceId) ? sys.deviceId : DEVICE_ID);
    const char* phoneId = (strlen(sys.phoneContactId) ? sys.phoneContactId : PHONE_CONTACT_ID);
    display.setCursor(0, 10); snprintf(topicBuf, sizeof(topicBuf), "/devices/%s/status", devId); display.println(topicBuf);
    display.setCursor(0, 20); snprintf(topicBuf, sizeof(topicBuf), "/memory/%s/+", devId); display.println(topicBuf);
    display.setCursor(0, 30); snprintf(topicBuf, sizeof(topicBuf), "/dm/%s/from/%s", devId, phoneId); display.println(topicBuf);
    display.setCursor(0, 40); snprintf(topicBuf, sizeof(topicBuf), "/dm/%s/from/+", devId); display.println(topicBuf);
    display.setCursor(0, 50); snprintf(topicBuf, sizeof(topicBuf), "/dm/<recip>/from/%s", devId); display.println(topicBuf);
    display.display();

    if(digitalRead(BTN_BACK) == LOW && millis() - lastButtonPress > debounceDelay) {
      lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis();
      showTopics = false; sound_blip();
    }
  }
}

void handleShutdownConfirmMode() {
  static int shutdownChoice = 0; // 0=YES, 1=NO
  display.clearDisplay(); display.setCursor(0, 0); display.println("-- SHUTDOWN --");
  display.setCursor(0, 20); display.print("Really shutdown?"); display.println();
  display.setCursor(10, 35); display.print(shutdownChoice == 0 ? "> YES" : "  YES"); display.println();
  display.setCursor(10, 45); display.print(shutdownChoice == 1 ? "> NO" : "  NO"); display.println();
  display.display();

  if(digitalRead(BTN_UP) == LOW && millis() - lastButtonPress > debounceDelay) {
    lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis();
    shutdownChoice = 0; sound_blip();
  }
  if(digitalRead(BTN_DN) == LOW && millis() - lastButtonPress > debounceDelay) {
    lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis();
    shutdownChoice = 1; sound_blip();
  }
  if(digitalRead(BTN_SEL) == LOW && millis() - lastButtonPress > debounceDelay) {
    lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis();
    if (shutdownChoice == 0) {
      // YES - do shutdown
      // 1. INSTANT display feedback FIRST
      display.clearDisplay(); display.setCursor(0, 20); display.println("Shutting down..."); display.display(); display.flush(); delay(200);
      
      // Quick saves (no blocking)
      if (pendingSysSave) saveSys();
      if (pendingCloudSync) saveCoreMemory();
      
      // Quick cleanup
      if (mqttClient.connected()) mqttClient.disconnect();
      LittleFS.end(); 
      WiFi.disconnect(true); 
      WiFi.mode(WIFI_OFF);
      
      // Show final message
      display.clearDisplay(); display.setCursor(0, 20); display.println("Safe to power off"); display.display(); display.flush();
      
      // Set shutdown flag in RTC memory (fast, non-blocking)
      shutdownFlag = 1;
      
      // 3 restart fallbacks
      for (int i = 0; i < 20; i++) { esp_task_wdt_reset(); delay(50); }
      esp_restart();                    // Primary
      esp_sleep_enable_timer_wakeup(1000000); esp_deep_sleep_start(); // Fallback 1
      while(1) { delay(100); esp_task_wdt_reset(); } // Fallback 2
    } else {
      // NO - back to menu
      shutdownChoice = 0;
      currentMode = MENU;
      sound_confirm();
    }
  }
  if(digitalRead(BTN_BACK) == LOW && millis() - lastButtonPress > debounceDelay) {
    lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis();
    shutdownChoice = 0;
    currentMode = MENU; sound_confirm();
  }
}

void handleScanListMode() {
  // CRITICAL FIX: Prevent division by zero if no networks are found.
  if (foundNetworks == 0) {
    display.clearDisplay();
    display.setCursor(10, 20); display.println("No networks");
    display.setCursor(10, 30); display.println("found.");
    display.display();
    if(digitalRead(BTN_BACK) == LOW && millis() - lastButtonPress > debounceDelay) { lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis(); currentMode = MENU; }
    return;
  }

  display.clearDisplay(); display.println("Select WiFi:");
  for(int i=0; i<3 && i < foundNetworks; i++) {
    int idx = (scanIdx + i) % foundNetworks;
    display.setCursor(10, 15+(i*12));
    display.print(i==0 ? "> " : "  "); display.println(WiFi.SSID(idx));
  }
  display.display();
  
  if(digitalRead(BTN_UP) == LOW && millis() - lastButtonPress > debounceDelay) { lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis(); scanIdx = (scanIdx - 1 + foundNetworks) % foundNetworks; sound_blip(); }
  if(digitalRead(BTN_DN) == LOW && millis() - lastButtonPress > debounceDelay) { lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis(); scanIdx = (scanIdx + 1) % foundNetworks; sound_blip(); }
  if(digitalRead(BTN_SEL) == LOW && millis() - lastButtonPress > debounceDelay) { 
    lastButtonPress = millis();
    lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis();
    char selectedSSID[33];
    strncpy(selectedSSID, WiFi.SSID(scanIdx).c_str(), sizeof(selectedSSID) - 1);
    selectedSSID[sizeof(selectedSSID)-1] = '\0';
    strncpy(tempSSID, selectedSSID, sizeof(tempSSID) - 1);
    tempSSID[sizeof(tempSSID)-1] = '\0';
    inputPass[0] = '\0'; charIdx = 2; currentMode = KEYBOARD; 
  }
  if(digitalRead(BTN_BACK) == LOW && millis() - lastButtonPress > debounceDelay) { lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis(); currentMode = MENU; }
}

inline void copySafeRpgText(char* dst, size_t dstSize, const char* src) {
  if (!dst || dstSize == 0) return;
  if (!src) src = "";
  strncpy(dst, src, dstSize - 1);
  dst[dstSize - 1] = '\0';
}

inline void normalizeStoryText(const char* src, char* dst, size_t dstSize) {
  copySafeRpgText(dst, dstSize, src);
  for (size_t i = 0; dst[i] != '\0'; i++) {
    if (dst[i] == '\n' || dst[i] == '\r' || dst[i] == '\t') dst[i] = ' ';
  }
}

inline bool isUsableRpgChoice(const char* src) {
  if (!src) return false;
  while (*src == ' ' || *src == '\n' || *src == '\r' || *src == '\t') src++;
  if (*src == '\0') return false;
  if (strcmp(src, "...") == 0) return false;
  return true;
}

inline bool hasActiveQuestState() {
  if (strlen(rpgStory) == 0) return false;
  if (strcmp(rpgStory, "Loading story...") == 0) return false;
  if (strcmp(rpgStory, "Starting quest...") == 0) return false;
  if (strcmp(rpgStory, "Thinking...") == 0) return false;
  for (int i = 0; i < 3; i++) {
    if (isUsableRpgChoice(rpgChoices[i])) return true;
  }
  return false;
}

inline void buildScrollingStoryView(char* dst, size_t dstSize, int windowChars) {
  char cleanStory[256];
  normalizeStoryText(rpgStory, cleanStory, sizeof(cleanStory));

  if (rpgStoryUpdated) {
    rpgStoryScrollOffset = 0;
    rpgStoryLastScroll = millis();
    rpgStoryUpdated = false; // Reset flag after handling
  }

  if (!dst || dstSize == 0) return;
  int maxSafeChars = (int)dstSize - 1;
  if (windowChars > maxSafeChars) windowChars = maxSafeChars;
  if (windowChars < 0) windowChars = 0;

  int storyLen = strlen(cleanStory);
  if (storyLen <= windowChars) {
    copySafeRpgText(dst, dstSize, cleanStory);
    return;
  }

  // Add a visible end gap so the user can tell the story finished.
  const int endGapChars = 16;
  int cycleLen = storyLen + endGapChars;
  if (millis() - rpgStoryLastScroll > 180) {
    rpgStoryLastScroll = millis();
    rpgStoryScrollOffset++;
    if (rpgStoryScrollOffset >= cycleLen) rpgStoryScrollOffset = 0;
  }

  for (int i = 0; i < windowChars; i++) {
    int idx = rpgStoryScrollOffset + i;
    dst[i] = (idx >= 0 && idx < storyLen) ? cleanStory[idx] : ' ';
  }
  dst[windowChars] = '\0';
}

inline void clampChoiceForDisplay(const char* src, char* dst, size_t dstSize, int maxChars) {
  if (!src || src[0] == '\0') {
    copySafeRpgText(dst, dstSize, "...");
    return;
  }
  copySafeRpgText(dst, dstSize, src);
  for (size_t i = 0; dst[i] != '\0'; i++) {
    if (dst[i] == '\n' || dst[i] == '\r') dst[i] = ' ';
  }
  int maxSafeChars = (int)dstSize - 1;
  if (maxChars > maxSafeChars) maxChars = maxSafeChars;
  if (maxChars < 0) maxChars = 0;
  int len = strlen(dst);
  if (len > maxChars) {
    if (maxChars >= 4) {
      dst[maxChars - 3] = '.';
      dst[maxChars - 2] = '.';
      dst[maxChars - 1] = '.';
      dst[maxChars] = '\0';
    } else {
      dst[maxChars] = '\0';
    }
  }
}

inline void drawRpgQuestScreen(bool isWaiting) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextWrap(false);

  // Top mini HUD
  display.setCursor(0, 0);
  display.print("HP:");
  display.print(rpgHP);
  display.print("  G:");
  display.print(rpgGold);
  display.setCursor(86, 0);
  display.print(isWaiting ? "SYNC..." : "QUEST");
  display.drawFastHLine(0, 10, 128, WHITE);

  // Middle story text (3-line scrolling window)
  char storyView[72];
  buildScrollingStoryView(storyView, sizeof(storyView), 63);
  display.setCursor(0, 13);
  display.setTextWrap(true);
  display.print(storyView);
  display.setTextWrap(false);
  display.drawFastHLine(0, 38, 128, WHITE);

  // Bottom choices
  for (int i = 0; i < 3; i++) {
    char choiceView[22];
    clampChoiceForDisplay(rpgChoices[i], choiceView, sizeof(choiceView), 19);
    display.setCursor(0, 41 + (i * 8));
    display.print(rpgChoiceIdx == i ? "> " : "  ");
    display.print(choiceView);
  }

  display.display();
}

inline void handleGameRPGMode() {
  yield();
  drawRpgQuestScreen(false);

  if (digitalRead(BTN_UP) == LOW && millis() - lastButtonPress > debounceDelay) {
    lastButtonPress = millis();
    lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis();
    rpgChoiceIdx = (rpgChoiceIdx - 1 + 3) % 3;
    sound_blip();
  }
  if (digitalRead(BTN_DN) == LOW && millis() - lastButtonPress > debounceDelay) {
    lastButtonPress = millis();
    lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis();
    rpgChoiceIdx = (rpgChoiceIdx + 1) % 3;
    sound_blip();
  }
  if (digitalRead(BTN_SEL) == LOW && millis() - lastButtonPress > debounceDelay) {
    lastButtonPress = millis();
    lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis();
    sound_confirm();

    int chosenIdx = rpgChoiceIdx;
    if (!isUsableRpgChoice(rpgChoices[chosenIdx])) {
      for (int i = 0; i < 3; i++) {
        if (isUsableRpgChoice(rpgChoices[i])) {
          chosenIdx = i;
          break;
        }
      }
    }

    char selectedChoice[40];
    copySafeRpgText(selectedChoice, sizeof(selectedChoice), rpgChoices[chosenIdx]);
    if (!isUsableRpgChoice(selectedChoice)) {
      copySafeRpgText(rpgStory, sizeof(rpgStory), "No valid choices yet. Please wait...");
      sound_error();
      return;
    }

    // Backup current state for recovery if network fails
    char prevStory[256];
    char prevChoices[3][40];
    copySafeRpgText(prevStory, sizeof(prevStory), rpgStory);
    for (int i = 0; i < 3; i++) copySafeRpgText(prevChoices[i], sizeof(prevChoices[i]), rpgChoices[i]);

    // Move large buffers to heap to prevent stack overflow on ESP8266
    char* sceneSnapshot = new char[160];
    char* prompt = new char[400];

    if (sceneSnapshot && prompt) {
      normalizeStoryText(rpgStory, sceneSnapshot, 160);

      strncpy(rpgStory, "Thinking...", 255);
      for (int i = 0; i < 3; i++) copySafeRpgText(rpgChoices[i], sizeof(rpgChoices[i]), "...");
      drawRpgQuestScreen(true); 

      snprintf(prompt, 400,
        "RPG state now: HP=%d Gold=%d Scene=\"%s\". I choose: \"%s\". Continue quest."
        " Format: [RPG] story | choice1 | choice2 | choice3",
        rpgHP, rpgGold, sceneSnapshot, selectedChoice);
      
      delete[] sceneSnapshot; // Free heap early before network call
      syncAI(prompt, false, GAME_RPG, true); // isPersonal=false saves massive RAM in network.h
      delete[] prompt;

      if (!hasActiveQuestState()) {
        copySafeRpgText(rpgStory, sizeof(rpgStory), prevStory);
        for (int i = 0; i < 3; i++) copySafeRpgText(prevChoices[i], sizeof(prevChoices[i]), prevChoices[i]);
      }

      saveSys(); // Persist local save
      syncMemoryToCloud(); // Save turn to cloud
    } else {
      if (sceneSnapshot) delete[] sceneSnapshot;
      if (prompt) delete[] prompt;
    }

    // Prevent accidental double-fire if button is still held after network delay.
    unsigned long waitStart = millis();
    while (digitalRead(BTN_SEL) == LOW && millis() - waitStart < 400) {
      yield();
      delay(5);
    }
  }
  if (digitalRead(BTN_BACK) == LOW && millis() - lastButtonPress > debounceDelay) {
    lastButtonPress = millis();
    lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis();
    saveSys(); // Keep quest progress when leaving the game
    syncMemoryToCloud(); // Cloud-save quest before exit
    currentMode = FACE;
    sound_confirm();
  }
}

inline void handleConfirmSaveRPGMode() {
  yield();
  display.clearDisplay();
  display.setCursor(10, 10);
  display.println("Save RPG Progress?");

  display.setCursor(30, 40);
  display.print(menuIdx == 1 ? "> Yes" : "  Yes");
  display.setCursor(30, 50);
  display.print(menuIdx == 0 ? "> No" : "  No");
  display.display();

  // Navigation (UP/DN to toggle between Yes/No)
  if(digitalRead(BTN_UP) == LOW && millis() - lastButtonPress > debounceDelay) { lastButtonPress = millis(); menuIdx = 1; sound_blip(); } // Yes
  if(digitalRead(BTN_DN) == LOW && millis() - lastButtonPress > debounceDelay) { lastButtonPress = millis(); menuIdx = 0; sound_blip(); } // No

  // Back button cancels
  if(digitalRead(BTN_BACK) == LOW && millis() - lastButtonPress > debounceDelay) {
    lastButtonPress = millis();
    currentMode = GAME_RPG; // Go back to the game
    sound_confirm();
  }

  // Select button confirms action
  if(digitalRead(BTN_SEL) == LOW && millis() - lastButtonPress > debounceDelay) { lastButtonPress = millis();
    if (menuIdx == 1) { // "Yes" was selected
      syncMemoryToCloud(); // Save current RPG state to cloud
      display.clearDisplay(); display.setCursor(20, 30); display.print("Saved!"); display.display();
      safeDelay(1000); // Hold message on screen for 1 second
      currentMode = FACE;
    } else { // "No" was selected
      currentMode = FACE; sound_confirm();
    }
  }
}

inline void handleTimeZoneSelectMode() {
  display.clearDisplay();
  display.setCursor(0, 0); display.println("Select Timezone:");
  display.drawFastHLine(0, 10, 128, WHITE);
  
  int offsetHours = menuIdx - 12; // menuIdx 12 is UTC+0
  
  display.setCursor(20, 30);
  display.setTextSize(2);
  display.print("UTC");
  if (offsetHours >= 0) display.print("+");
  display.print(offsetHours);
  display.setTextSize(1);
  
  display.setCursor(0, 55); display.println("UP/DN: Adj  SEL: Save");
  display.display();

  if(digitalRead(BTN_UP) == LOW && millis() - lastButtonPress > debounceDelay) { lastButtonPress = millis(); lastActivity = millis(); if(menuIdx < 26) menuIdx++; sound_blip(); }
  if(digitalRead(BTN_DN) == LOW && millis() - lastButtonPress > debounceDelay) { lastButtonPress = millis(); lastActivity = millis(); if(menuIdx > 0) menuIdx--; sound_blip(); }
  
  if(digitalRead(BTN_SEL) == LOW && millis() - lastButtonPress > debounceDelay) {
    lastButtonPress = millis(); lastActivity = millis(); 
    sys.timeZoneOffset = (menuIdx - 12) * 3600;
    timeClient.setTimeOffset(sys.timeZoneOffset);
    saveSys(); sound_confirm();
    currentMode = TERMINAL;
  }
  if(digitalRead(BTN_BACK) == LOW && millis() - lastButtonPress > debounceDelay) { lastButtonPress = millis(); lastActivity = millis(); currentMode = TERMINAL; }
}

inline void handleViewEmotionsMode() {
  display.clearDisplay(); display.setCursor(0, 0); display.println("Select Emotion:");
  // Move emotion names to Flash
  static const char e0[] PROGMEM = "NEUTRAL"; static const char e1[] PROGMEM = "HAPPY";
  static const char e2[] PROGMEM = "SURPRISED"; static const char e3[] PROGMEM = "SAD";
  static const char e4[] PROGMEM = "ANGRY"; static const char e5[] PROGMEM = "THINKING";
  static const char e6[] PROGMEM = "SLEEPY"; static const char e7[] PROGMEM = "FLUSTERED";
  static const char e8[] PROGMEM = "LAUGHING"; static const char e9[] PROGMEM = "WINK";
  static const char e10[] PROGMEM = "CONFUSED"; static const char e11[] PROGMEM = "LOVE";
  static const char e12[] PROGMEM = "SASSY"; static const char e13[] PROGMEM = "SHOCKED";
  static const char e14[] PROGMEM = "SAD_EMB"; static const char e15[] PROGMEM = "SHY";
  static const char e16[] PROGMEM = "TEASING"; static const char e17[] PROGMEM = "BLUSHING";
  static const char e18[] PROGMEM = "BLINK";
  static const char* const emos[] PROGMEM = {e0,e1,e2,e3,e4,e5,e6,e7,e8,e9,e10,e11,e12,e13,e14,e15,e16,e17,e18};

  int emoLen = 19;
  int startI = (menuIdx > 3) ? menuIdx - 3 : 0;
  char buffer[12];
  for(int i=0; i<5 && (startI+i) < emoLen; i++) {
    int idx = startI + i;
    strcpy_P(buffer, (char*)pgm_read_ptr(&(emos[idx])));
    display.setCursor(10, 15+(i*10));
    display.print(menuIdx == idx ? "> " : "  ");
    display.println(buffer);
  }
  display.display();

  if(digitalRead(BTN_UP) == LOW && millis() - lastButtonPress > debounceDelay) { lastButtonPress = millis(); lastActivity = millis(); menuIdx = (menuIdx - 1 + emoLen) % emoLen; sound_blip(); }
  if(digitalRead(BTN_DN) == LOW && millis() - lastButtonPress > debounceDelay) { lastButtonPress = millis(); lastActivity = millis(); menuIdx = (menuIdx + 1) % emoLen; sound_blip(); }
  
  // On Select: Enter Display Mode. menuIdx determines which face.
  if(digitalRead(BTN_SEL) == LOW && millis() - lastButtonPress > debounceDelay) { 
    lastButtonPress = millis(); lastActivity = millis(); 
    currentMode = EMOTION_DISPLAY; 
    sound_confirm();
  }
  if(digitalRead(BTN_BACK) == LOW && millis() - lastButtonPress > debounceDelay) { lastButtonPress = millis(); lastActivity = millis(); currentMode = MENU; }
}

inline void handleEmotionDisplayMode() {
  emotionSetTime = millis();

  uint8_t variantCount = 1;
  Emotion e = (Emotion)menuIdx;
  switch(e) {
    case NEUTRAL: variantCount = 6; break;
    case HAPPY: variantCount = 3; break;
    case ANGRY: variantCount = 4; break;
    case SURPRISED: variantCount = 2; break;
    case SAD: variantCount = 2; break;
    case THINKING_FACE: variantCount = 2; break;
    case SLEEPY: variantCount = 2; break;
    case FLUSTERED: variantCount = 3; break;
    case LAUGHING: variantCount = 3; break;
    case WINK: variantCount = 3; break;
    case CONFUSED: variantCount = 2; break;
    case LOVE: variantCount = 6; break;
    case SHOCKED: variantCount = 4; break;
    case SASSY: variantCount = 2; break;
    case SHY: variantCount = 2; break;
    case TEASING: variantCount = 5; break;
    case BLUSHING: variantCount = 5; break;
    default: variantCount = 1; break;
  }

  if (menuIdx == 18) { blinking = true; currentEmotion = NEUTRAL; }
  else { blinking = false; currentEmotion = e; }

  // Reuse emotion strings from Flash
  static const char e0[] PROGMEM = "NEUTRAL"; static const char e1[] PROGMEM = "HAPPY";
  static const char e2[] PROGMEM = "SURPRISED"; static const char e3[] PROGMEM = "SAD";
  static const char e4[] PROGMEM = "ANGRY"; static const char e5[] PROGMEM = "THINKING";
  static const char e6[] PROGMEM = "SLEEPY"; static const char e7[] PROGMEM = "FLUSTERED";
  static const char e8[] PROGMEM = "LAUGHING"; static const char e9[] PROGMEM = "WINK";
  static const char e10[] PROGMEM = "CONFUSED"; static const char e11[] PROGMEM = "LOVE";
  static const char e12[] PROGMEM = "SASSY"; static const char e13[] PROGMEM = "SHOCKED";
  static const char e14[] PROGMEM = "SAD_EMB"; static const char e15[] PROGMEM = "SHY";
  static const char e16[] PROGMEM = "TEASING"; static const char e17[] PROGMEM = "BLUSHING";
  static const char e18[] PROGMEM = "BLINK";
  static const char* const names[] PROGMEM = {e0,e1,e2,e3,e4,e5,e6,e7,e8,e9,e10,e11,e12,e13,e14,e15,e16,e17,e18};
  
  char buffer[12]; strcpy_P(buffer, (char*)pgm_read_ptr(&(names[menuIdx])));
  snprintf(aiMsg, sizeof(aiMsg), "%s (Var %d/%d)", buffer, emotionVariantIndex[menuIdx] + 1, variantCount);

  drawAesthetica();

  if(digitalRead(BTN_UP) == LOW && millis() - lastButtonPress > debounceDelay) {
    lastButtonPress = millis();
    emotionVariantIndex[menuIdx] = (emotionVariantIndex[menuIdx] + variantCount - 1) % variantCount;
    sound_blip();
  }
  if(digitalRead(BTN_DN) == LOW && millis() - lastButtonPress > debounceDelay) {
    lastButtonPress = millis();
    emotionVariantIndex[menuIdx] = (emotionVariantIndex[menuIdx] + 1) % variantCount;
    sound_blip();
  }

  if(digitalRead(BTN_BACK) == LOW && millis() - lastButtonPress > debounceDelay) { 
    lastButtonPress = millis(); 
    blinking = false; currentEmotion = NEUTRAL;
    currentMode = VIEW_EMOTIONS; 
  }
}

inline void handleGameGuessNumberMode() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Guess the Number (1-20)");
  display.drawFastHLine(0, 10, 128, WHITE);

  display.setCursor(12, 22);
  display.print("Your Guess:");
  display.setTextSize(2);
  display.setCursor(12, 34);
  display.print(gameCurrentGuess);
  display.setTextSize(1);

  display.setCursor(0, 54);
  display.print("Tries:");
  display.print(gameTries);
  display.setCursor(70, 54);
  display.print("Best:");
  display.print(sys.guessHighScore == 999 ? 0 : sys.guessHighScore);

  display.display();

  if(digitalRead(BTN_UP) == LOW && millis() - lastButtonPress > debounceDelay) {
    lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis();
    gameCurrentGuess = min(20, gameCurrentGuess + 1); sound_blip();
  }
  if(digitalRead(BTN_DN) == LOW && millis() - lastButtonPress > debounceDelay) {
    lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis();
    gameCurrentGuess = max(1, gameCurrentGuess - 1); sound_blip();
  }
  if(digitalRead(BTN_SEL) == LOW && millis() - lastButtonPress > debounceDelay) {
    lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis();
    gameTries++;
    if (gameCurrentGuess == gameSecretNumber) {
      snprintf(aiMsg, sizeof(aiMsg), "You got it in %d tries!", gameTries);
      // Award XP for winning, with a bonus for fewer tries
      gainXP(25 + max(0, 10 - gameTries));
      sound_level_up();
      // Update high score if improved (fewer tries)
      if (gameTries < sys.guessHighScore) {
        sys.guessHighScore = gameTries;
        saveSys(); pendingCloudSync = true; pendingMemorySave = true;
        snprintf(aiMsg, sizeof(aiMsg), "New best: %d tries!", gameTries);
      }
      currentMode = FACE;
    } else if (gameCurrentGuess < gameSecretNumber) {
      snprintf(aiMsg, sizeof(aiMsg), "Higher!");
      sound_confirm();
    } else {
      snprintf(aiMsg, sizeof(aiMsg), "Lower!");
      sound_confirm();
    }
    // Show result on main screen
    currentMode = FACE;
    emotionSetTime = millis();
  }
  if(digitalRead(BTN_BACK) == LOW && millis() - lastButtonPress > debounceDelay) { lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis(); currentMode = FACE; sound_error(); }
}

inline void handleTerminalMode() {
  display.clearDisplay();
  display.setCursor(0, 0); display.println(">_ TERMINAL");
  display.drawFastHLine(0, 9, 128, WHITE);

  static const char c0[] PROGMEM = "RUN CMD..."; static const char c1[] PROGMEM = "PING";
  static const char c2[] PROGMEM = "LS"; static const char c3[] PROGMEM = "FREE";
  static const char c4[] PROGMEM = "REBOOT"; static const char c5[] PROGMEM = "HELP";
  static const char* const cmds[] PROGMEM = {c0, c1, c2, c3, c4, c5};

  int cmdLen = 6;
  char buffer[12];
  for(int i=0; i<cmdLen; i++) {
    strcpy_P(buffer, (char*)pgm_read_ptr(&(cmds[i])));
    display.setCursor(6, 15 + (i*9)); // Tighter spacing
    display.print(menuIdx == i ? "> " : "  ");
    display.println(buffer);
  }
  display.display();

  if(digitalRead(BTN_UP) == LOW && millis() - lastButtonPress > debounceDelay) { lastButtonPress = millis(); lastActivity = millis(); menuIdx = (menuIdx - 1 + cmdLen) % cmdLen; sound_blip(); }
  if(digitalRead(BTN_DN) == LOW && millis() - lastButtonPress > debounceDelay) { lastButtonPress = millis(); lastActivity = millis(); menuIdx = (menuIdx + 1) % cmdLen; sound_blip(); }
  
  // Execute Command
  if(digitalRead(BTN_SEL) == LOW && millis() - lastButtonPress > debounceDelay) {
    lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis();
    
    // Route selection to execution
    if (menuIdx == 0) { // RUN CMD...
      inputPass[0] = '\0'; charIdx = 2; // Reset keyboard buffer
      currentMode = TERMINAL_INPUT;
      sound_terminal();
    }
    else if (menuIdx == 1) { executeTerminalCommand("ping"); }
    else if (menuIdx == 2) { executeTerminalCommand("ls"); }
    else if (menuIdx == 3) { executeTerminalCommand("free"); }
    else if (menuIdx == 4) { executeTerminalCommand("reboot"); }
    else if (menuIdx == 5) { executeTerminalCommand("hlp"); }
  }
  if(digitalRead(BTN_BACK) == LOW && millis() - lastButtonPress > debounceDelay) { lastButtonPress = millis(); lastActivity = millis(); currentMode = MENU; }
}

inline void handleTerminalFileListMode() {
  display.clearDisplay();
  display.setCursor(0, 0); display.println("cat: Select File");
  display.drawFastHLine(0, 10, 128, WHITE);
  
  File root = LittleFS.open("/");
  int total = 0;
  File fCount = root.openNextFile();
  while(fCount) { total++; fCount = root.openNextFile(); }
  root.close();
  
  if (total == 0) {
    display.setCursor(10, 30); display.println("No files.");
  } else {
    root = LittleFS.open("/");
    for (int i = 0; i < total; i++) {
      File entry = root.openNextFile();
      if (i >= viewIdx && i < viewIdx + 4) {
        display.setCursor(10, 15 + ((i - viewIdx) * 12));
        display.print(i == viewIdx ? "> " : "  ");
        display.println(entry.name());
      }
    }
    yield();
  }
  display.display();

  if(digitalRead(BTN_UP) == LOW && millis() - lastButtonPress > debounceDelay) { lastButtonPress = millis(); viewIdx = (viewIdx - 1 + total) % total; sound_blip(); }
  if(digitalRead(BTN_DN) == LOW && millis() - lastButtonPress > debounceDelay) { lastButtonPress = millis(); viewIdx = (viewIdx + 1) % total; sound_blip(); }
  
  if(digitalRead(BTN_SEL) == LOW && millis() - lastButtonPress > debounceDelay) {
    lastButtonPress = millis();
    root = LittleFS.open("/");
    char target[32] = "";
    for(int i = 0; i <= viewIdx; i++) { 
      File f = root.openNextFile(); 
      if(i == viewIdx && f.name() != NULL) strncpy(target, f.name(), 31); 
    }
    root.close();
    terminalCatFile(target);
    currentMode = TERMINAL_OUTPUT;
    sound_confirm();
  }
  
  if(digitalRead(BTN_BACK) == LOW && millis() - lastButtonPress > debounceDelay) { 
    lastButtonPress = millis(); currentMode = TERMINAL; sound_confirm(); 
  }
}

inline void handleTerminalOutputMode() {
  // Pure console look: Black background, small white text
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0 - termScrollTop);
  if (workspace[0] != '\0') display.print(workspace);
  else display.print("No Output.");
  
  display.display();

  // Scrolling
  if(digitalRead(BTN_DN) == LOW) { // Hold to scroll
    if (millis() - lastButtonPress > 50) { // Fast scroll
      termScrollTop += 2;
      if (termScrollTop % 16 == 0) sound_blip(); // Blip every few lines of scroll
      lastButtonPress = millis();
    }
  }
  if(digitalRead(BTN_UP) == LOW) { 
    if (millis() - lastButtonPress > 50) { 
      termScrollTop = max(0, termScrollTop - 2);
      if (termScrollTop % 16 == 0) sound_blip();
      lastButtonPress = millis();
    }
  }
  
  if(digitalRead(BTN_BACK) == LOW && millis() - lastButtonPress > debounceDelay) {
    lastButtonPress = millis(); 
    workspace[0] = '\0'; // Clear buffer
    currentMode = TERMINAL; 
  }
}

inline void handleKeyboardMode() {
  display.clearDisplay();
  display.setCursor(0,0);

  if (currentMode == KEYBOARD) {
    display.print("SSID: "); display.println(tempSSID);
    display.setCursor(0,15); display.print("PASS: ");
  } else if (currentMode == CUSTOM_PROMPT) {
    display.print("Enter prompt:");
    display.setCursor(0,15);
  } else if (currentMode == CUSTOM_MESSAGE) {
    display.print("To: "); display.println(selectedRecipient);
    display.setCursor(0,15);
  } else if (currentMode == TERMINAL_INPUT) {
    display.print("RUN CMD:");
    display.setCursor(0,15);
  }
  display.print(inputPass);
  display.print("_");

  const int numSpecialChars = 2; // OK and <-
  const int cycleLen = numSpecialChars + strlen_P(chars);

  display.drawRect(0, 40, 128, 24, WHITE);
  display.setTextSize(2); display.setCursor(55, 45);
  if (charIdx == 0) display.print("OK"); 
  else if (charIdx == 1) display.print("<-"); 
  else {
    char selectedChar = pgm_read_byte(&chars[charIdx - numSpecialChars]);
    if (selectedChar == ' ') { display.setCursor(48, 45); display.print("SPC"); }
    else display.print(selectedChar);
  }
  display.setTextSize(1);
  display.setCursor(0,45); display.println("DN: Cycle | SEL: Choose");
  display.display();

  bool triggerOK = false;
  bool triggerDelete = false;

  // Cycle Character or Backspace
  if(digitalRead(BTN_DN) == LOW && (millis() - lastButtonPress > debounceDelay/2)) { // Use shorter delay for faster cycling
    lastButtonPress = millis(); lastActivity = millis();
    if (digitalRead(BTN_MODIFIER) == LOW) {
      triggerDelete = true;
    } else {
      charIdx = (charIdx + 1) % cycleLen;
    }
  }
  // Cycle Back or Enter
  if(digitalRead(BTN_UP) == LOW && (millis() - lastButtonPress > debounceDelay/2)) {
    lastButtonPress = millis(); lastActivity = millis();
    if (digitalRead(BTN_MODIFIER) == LOW) {
      triggerOK = true;
    } else {
      charIdx = (charIdx - 1 + cycleLen) % cycleLen;
    }
  }
  // Choose Action
  if((digitalRead(BTN_SEL) == LOW && (millis() - lastButtonPress > debounceDelay)) || triggerOK || triggerDelete) {
    if (!triggerOK && !triggerDelete) {
      lastButtonPress = millis();
    }
    lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis();
    if (charIdx == 0 || triggerOK) { // OK Action
      if (currentMode == KEYBOARD) {
        strncpy(sys.ssid, tempSSID, sizeof(sys.ssid) - 1); sys.ssid[sizeof(sys.ssid) - 1] = '\0';
        strncpy(sys.pass, inputPass, sizeof(sys.pass) - 1); sys.pass[sizeof(sys.pass) - 1] = '\0';
        saveSys(); 
        tryConnect(); 
      } else if (currentMode == CUSTOM_PROMPT) { // Custom prompts from the device are always personal
        syncAI(inputPass, true);
      } else if (currentMode == CUSTOM_MESSAGE) {
        sendMessage(selectedRecipient, inputPass);
        display.clearDisplay(); 
        display.setCursor(0, 25); display.println("Message Sent!");
        display.setCursor(0, 35); display.print("To: "); display.print(selectedRecipient);
        display.display();
        sound_confirm();
        safeDelay(1000); // Hold message on screen for 1 second
        currentMode = MENU;
      } else if (currentMode == TERMINAL_INPUT) {
        executeTerminalCommand(inputPass); // Run the typed command
      }
    } else if (charIdx == 1 || triggerDelete) { // Delete
      int len = strlen(inputPass);
      if (len > 0) inputPass[len - 1] = '\0';
    } else { // Add Character
      int len = strlen(inputPass);
      if (len < sizeof(inputPass) - 1) {
        inputPass[len] = pgm_read_byte(&chars[charIdx - numSpecialChars]);
        inputPass[len + 1] = '\0';
      }
    }
  }
  
  // Back to previous mode
  if(digitalRead(BTN_BACK) == LOW && millis() - lastButtonPress > debounceDelay) { lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis();
    if (currentMode == CUSTOM_MESSAGE) {
      currentMode = SELECT_RECIPIENT;
    } else if (currentMode == TERMINAL_INPUT) {
      currentMode = TERMINAL;
    } else {
      currentMode = (currentMode == KEYBOARD) ? SCAN_LIST : MENU;
    }
  }
}

inline void handleGameSelectMode() {
  display.clearDisplay();
  display.setCursor(0, 0); display.println("Pick a Game:");
  
  static const char g0[] PROGMEM = "Guess Number"; static const char g1[] PROGMEM = "Rock Paper Scis";
  static const char g2[] PROGMEM = "Yuki's Quest";
  static const char* const games[] PROGMEM = {g0, g1, g2};

  int gameLen = 3;
  char buffer[20];
  for(int i=0; i<gameLen; i++) {
    strcpy_P(buffer, (char*)pgm_read_ptr(&(games[i])));
    display.setCursor(10, 20+(i*12));
    display.print(menuIdx == i ? "> " : "  ");
    display.println(buffer);
  }
  display.display();

  if(digitalRead(BTN_UP) == LOW && millis() - lastButtonPress > debounceDelay) { lastButtonPress = millis(); lastActivity = millis(); menuIdx = (menuIdx - 1 + gameLen) % gameLen; sound_blip(); }
  if(digitalRead(BTN_DN) == LOW && millis() - lastButtonPress > debounceDelay) { lastButtonPress = millis(); lastActivity = millis(); menuIdx = (menuIdx + 1) % gameLen; sound_blip(); }
  
  if(digitalRead(BTN_SEL) == LOW && millis() - lastButtonPress > debounceDelay) {
    lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis();
    
    yield(); 
    delay(50); // Small pause to let system settle after selection
    sound_confirm();

    if (menuIdx == 0) { // Guess Number (Index 0)
      gameSecretNumber = random(1, 21); gameCurrentGuess = 10; gameTries = 0;
      currentMode = GAME_GUESS_NUMBER;
    } else if (menuIdx == 1) { // RPS (Index 1)
      menuIdx = 0; // Reset choice for RPS logic
      currentMode = GAME_RPS;
    } else if (menuIdx == 2) { // RPG (Index 2)
      rpgChoiceIdx = 0;
      currentMode = GAME_RPG;
      if (hasActiveQuestState()) {
        drawRpgQuestScreen(false); // Resume saved quest state
      } else {
        copySafeRpgText(rpgStory, sizeof(rpgStory), "Starting quest...");
        for (int i = 0; i < 3; i++) copySafeRpgText(rpgChoices[i], sizeof(rpgChoices[i]), "...");
        drawRpgQuestScreen(true);
        syncAI("Start a new short fantasy RPG. You are a hero with 100 HP and 0 Gold. Reply only as: [RPG] story | choice1 | choice2 | choice3", true, GAME_RPG, true);
        if (hasActiveQuestState()) {
          saveSys();
          syncMemoryToCloud(); // Cloud-save new quest seed state
        }
      }
    }
  }

  if(digitalRead(BTN_BACK) == LOW && millis() - lastButtonPress > debounceDelay) { lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis(); currentMode = FACE; sound_confirm(); }
}

inline void handleGameRPSMode() {
  display.clearDisplay();
  display.setCursor(0, 0); display.println("Rock Paper Scissors");
  display.drawFastHLine(0, 10, 128, WHITE);

  static const char r0[] PROGMEM = "Rock"; static const char r1[] PROGMEM = "Paper";
  static const char r2[] PROGMEM = "Scissors";
  static const char* const choices[] PROGMEM = {r0, r1, r2};

  char buffer[10];
  strcpy_P(buffer, (char*)pgm_read_ptr(&(choices[menuIdx % 3])));

  display.setCursor(10, 25);
  display.print("Pick: "); 
  display.setTextSize(2);
  display.println(buffer);
  display.setTextSize(1);

  display.setCursor(0, 55); display.println("UP/DN: Pick  SEL: Go");
  display.display();

  if(digitalRead(BTN_UP) == LOW && millis() - lastButtonPress > debounceDelay) { lastButtonPress = millis(); lastActivity = millis(); menuIdx = (menuIdx + 1) % 3; sound_blip(); }
  if(digitalRead(BTN_DN) == LOW && millis() - lastButtonPress > debounceDelay) { lastButtonPress = millis(); lastActivity = millis(); menuIdx = (menuIdx + 2) % 3; sound_blip(); } // Cycle back
  
  if(digitalRead(BTN_SEL) == LOW && millis() - lastButtonPress > debounceDelay) {
    lastButtonPress = millis();
    int aiChoice = random(0, 3);
    int userChoice = menuIdx % 3;
    
    // 0=Rock, 1=Paper, 2=Scissors
    // Win logic: (User - AI + 3) % 3 == 1
    if (userChoice == aiChoice) {
        snprintf(aiMsg, sizeof(aiMsg), "Tie! Both picked %s.", choices[aiChoice]);
        currentEmotion = CONFUSED;
        emotionVariantIndex[(int)CONFUSED] = random(0, 2);
        emotionSetTime = millis();
        sound_confirm();
    } else if ((userChoice - aiChoice + 3) % 3 == 1) {
        snprintf(aiMsg, sizeof(aiMsg), "You won! %s beats %s.", choices[userChoice], choices[aiChoice]);
        gainXP(15);
        Emotion winEmotions[] = {HAPPY, LAUGHING, WINK, TEASING};
        currentEmotion = winEmotions[random(0, 4)];
        { uint8_t vc = 1; switch(currentEmotion) { case HAPPY: vc=3; break; case LAUGHING: vc=3; break; case WINK: vc=3; break; case TEASING: vc=5; break; default: vc=1; } emotionVariantIndex[(int)currentEmotion] = random(0, vc); }
        emotionSetTime = millis();
        sound_level_up();
    } else {
        snprintf(aiMsg, sizeof(aiMsg), "I won! %s beats %s.", choices[aiChoice], choices[userChoice]);
        Emotion lossEmotions[] = {SAD, SASSY, CONFUSED, ANGRY};
        currentEmotion = lossEmotions[random(0, 4)];
        { uint8_t vc = 1; switch(currentEmotion) { case SAD: vc=2; break; case SASSY: vc=2; break; case CONFUSED: vc=2; break; case ANGRY: vc=4; break; default: vc=1; } emotionVariantIndex[(int)currentEmotion] = random(0, vc); }
        emotionSetTime = millis();
        sound_sad();
    }
    currentMode = FACE;
    emotionSetTime = millis();
  }

  if(digitalRead(BTN_BACK) == LOW && millis() - lastButtonPress > debounceDelay) { 
    lastButtonPress = millis(); currentMode = FACE; sound_confirm(); 
  }
}

inline void handleViewScansMode() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Stored Scans:");
  File f = LittleFS.open("/scans.json", "r");
  int total = 0;
  if(f){
    if (!canAllocJson(1024)) {
      LOGW("UI","Skipping scans view - low heap");
      display.setCursor(10, 30);
      display.print("No scans stored");
      f.close();
      display.display();
      return;
    }
    DynamicJsonDocument* doc = new DynamicJsonDocument(1024);
    deserializeJson(*doc, f);
    JsonArray arr = doc->as<JsonArray>();
    total = arr.size();
    if (total > 0) {
      for(int i=0; i<3 && i<total; i++){
        int idx = (viewIdx + i) % total;
        display.setCursor(10, 15+(i*12));
        display.print(i==0 ? "> " : "  ");
        display.print(arr[idx]["ssid"].as<const char*>());
        display.print(" (");
        display.print(arr[idx]["rssi"].as<int>());
        display.println("dBm)");
      }
    }
    f.close();
    delete doc;
  } else {
    display.setCursor(10, 30);
    display.print("No scans stored");
  }
  display.display();
  
  if(total > 0) {
    if(digitalRead(BTN_UP) == LOW && millis() - lastButtonPress > debounceDelay) { lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis(); viewIdx = (viewIdx - 1 + total) % total; sound_blip(); }
    if(digitalRead(BTN_DN) == LOW && millis() - lastButtonPress > debounceDelay) { lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis(); viewIdx = (viewIdx + 1) % total; sound_blip(); }
    if(digitalRead(BTN_SEL) == LOW && millis() - lastButtonPress > debounceDelay) { 
      lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis();
      menuIdx = 0; // Default to "No"
      currentMode = CONFIRM_WIPE_SCANS;
    }
  }
  if(digitalRead(BTN_BACK) == LOW && millis() - lastButtonPress > debounceDelay) { lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis(); currentMode = MENU; }
  delay(100); // Reduce refresh rate to prevent filesystem thrashing
}

inline void handleSystemInfoMode() {
  display.clearDisplay();
  display.setCursor(0, 0 - infoScrollOffset);
  display.println("System Info:");
  display.print("MAC: ");
  display.println(WiFi.macAddress());
  display.print("IP:  ");
  display.println(WiFi.localIP().toString());
  display.print("Free Heap: ");
  display.print(ESP.getFreeHeap());
  display.println(" bytes");
  display.print("Uptime: ");
  display.print(millis() / 1000);
  display.println(" sec");
  display.print("Voltage: ");
  if (vcc > 2.0) {
    display.print(vcc, 2);
    display.println(" V");
  } else {
    display.println("USB");
  }
  display.print("Time: ");
  display.println(timeClient.getFormattedTime());
  display.print("WiFi: ");
  display.println(WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
  display.print("Home: ");
  display.println(userIsHome ? "YES" : "NO");
  display.print("Phone: ");
  display.println(phoneIP.toString().c_str());
  display.print("Level: ");
  display.print(sys.level);
  display.print(" XP: ");
  display.println(sys.xp);
  display.display();
  
  // Scrolling Logic
  if(digitalRead(BTN_DN) == LOW) {
    if (millis() - lastButtonPress > 50) {
      infoScrollOffset = min(infoScrollOffset + 2, 60); 
      lastButtonPress = millis();
    }
  }
  if(digitalRead(BTN_UP) == LOW) {
    if (millis() - lastButtonPress > 50) {
      infoScrollOffset = max(0, infoScrollOffset - 2);
      lastButtonPress = millis();
    }
  }

  if(digitalRead(BTN_BACK) == LOW && millis() - lastButtonPress > debounceDelay) { 
    lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis(); 
    infoScrollOffset = 0; currentMode = MENU; 
  }
}

inline void handleConfirmWipeScansMode() {
  display.clearDisplay();
  display.setCursor(10, 10);
  display.println("Wipe all stored");
  display.setCursor(10, 20);
  display.println("WiFi scans?");

  display.setCursor(30, 40);
  display.print(menuIdx == 1 ? "> Yes" : "  Yes");
  display.setCursor(30, 50);
  display.print(menuIdx == 0 ? "> No" : "  No");
  display.display();

  // Navigation (UP/DN to toggle between Yes/No)
  if(digitalRead(BTN_UP) == LOW && millis() - lastButtonPress > debounceDelay) { lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis(); menuIdx = 1; sound_blip(); } // Yes
  if(digitalRead(BTN_DN) == LOW && millis() - lastButtonPress > debounceDelay) { lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis(); menuIdx = 0; sound_blip(); } // No

  // Back button cancels
  if(digitalRead(BTN_BACK) == LOW && millis() - lastButtonPress > debounceDelay) {
    lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis();
    currentMode = VIEW_SCANS; // Go back to the scans list
    sound_confirm();
  }

  // Select button confirms action
  if(digitalRead(BTN_SEL) == LOW && millis() - lastButtonPress > debounceDelay) { lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis();
    if (menuIdx == 1) { // "Yes" was selected
      LittleFS.remove("/scans.json");
      display.clearDisplay(); display.setCursor(20, 30); display.print("Scans wiped!"); display.display();
      sound_error(); // Use a more "destructive" sound
      safeDelay(1000);
      currentMode = MENU;
    } else { // "No" was selected
      currentMode = VIEW_SCANS; sound_confirm();
    }
  }
}

inline void handleConfirmWipeMsgsMode() {
  display.clearDisplay();
  display.setCursor(10, 10);
  display.println("Wipe all chat");
  display.setCursor(10, 20);
  display.println("history?");

  display.setCursor(30, 40);
  display.print(menuIdx == 1 ? "> Yes" : "  Yes");
  display.setCursor(30, 50);
  display.print(menuIdx == 0 ? "> No" : "  No");
  display.display();

  // Navigation (UP/DN to toggle between Yes/No)
  if(digitalRead(BTN_UP) == LOW && millis() - lastButtonPress > debounceDelay) { lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis(); menuIdx = 1; sound_blip(); } // Yes
  if(digitalRead(BTN_DN) == LOW && millis() - lastButtonPress > debounceDelay) { lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis(); menuIdx = 0; sound_blip(); } // No

  // Back button cancels
  if(digitalRead(BTN_BACK) == LOW && millis() - lastButtonPress > debounceDelay) {
    lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis();
    currentMode = VIEW_MSGS; // Go back to the message list
    sound_confirm();
  }

  // Select button confirms action
  if(digitalRead(BTN_SEL) == LOW && millis() - lastButtonPress > debounceDelay) { lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis();
    if (menuIdx == 1) { // "Yes" was selected
      for (int i = 0; i < MAX_MESSAGES; i++) { receivedMessages[i].content[0] = '\0'; }
      messageIndex = 0; // Reset the buffer index
      display.clearDisplay(); display.setCursor(20, 30); display.print("History wiped!"); display.display();
      sound_error(); // Use a more "destructive" sound
      safeDelay(1000);
      currentMode = MENU;
    } else { // "No" was selected
      currentMode = VIEW_MSGS; sound_confirm();
    }
  }
}

inline void handleViewMessagesMode() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("MQTT Messages:");

  bool hasMessages = false;
  for(int i = 0; i < MAX_MESSAGES; ++i) {
    if (strlen(receivedMessages[i].content) > 0) {
      hasMessages = true;
      break;
    }
  }

  if (hasMessages) {
    for(int i=0; i<3 && i < MAX_MESSAGES; i++){
      int idx = (viewIdx + i) % MAX_MESSAGES;
      display.setCursor(10, 15+(i*12));
      display.print(i==0 ? "> " : "  ");
      // Truncate: 21 chars/line, "> " prefix = 2 chars, so 17 chars for message
      char msgBuf[18];
      strncpy(msgBuf, receivedMessages[idx].content, sizeof(msgBuf) - 1);
      msgBuf[sizeof(msgBuf) - 1] = '\0';
      if ((int)strlen(receivedMessages[idx].content) > 17) msgBuf[16] = '~';
      display.print(msgBuf);
    }
  } else {
    display.setCursor(10, 30);
    display.print("No messages yet.");
  }
  display.display();

  if(digitalRead(BTN_UP) == LOW && millis() - lastButtonPress > debounceDelay) { lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis(); viewIdx = (viewIdx - 1 + MAX_MESSAGES) % MAX_MESSAGES; sound_blip(); }
  if(digitalRead(BTN_DN) == LOW && millis() - lastButtonPress > debounceDelay) { lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis(); viewIdx = (viewIdx + 1) % MAX_MESSAGES; sound_blip(); }
  if(digitalRead(BTN_SEL) == LOW && millis() - lastButtonPress > debounceDelay) { // Ask to clear history
    lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis();
    menuIdx = 0; // Default to "No"
    currentMode = CONFIRM_WIPE_MSGS;
  }
  if(digitalRead(BTN_BACK) == LOW && millis() - lastButtonPress > debounceDelay) { lastButtonPress = millis(); lastActivity = millis(); lastInteraction = millis(); lastIdleCheck = millis(); currentMode = MENU; }
}

inline void handleSelectRecipientMode() {
  // Redirecting Select Recipient to default Phone for single-user mode
  strncpy(selectedRecipient, PHONE_CONTACT_ID, 31);
  inputPass[0] = '\0';
  charIdx = 2;
  currentMode = CUSTOM_MESSAGE;
}

inline void handleMemoryRebootMode() {
  display.clearDisplay(); display.setCursor(0, 0); display.println("-- MEMORY REBOOT --");
  display.drawFastHLine(0, 10, 128, WHITE);
  display.setCursor(10, 20);
  display.print(menuIdx == 0 ? "> " : "  "); display.println("CLEAN SLATE");
  display.setCursor(10, 30);
  display.print(menuIdx == 1 ? "> " : "  "); display.println("SELECTED WIPE");
  display.setCursor(10, 40);
  display.print(menuIdx == 2 ? "> " : "  "); display.println("BACK");
  display.display();
  if(digitalRead(BTN_UP) == LOW && millis() - lastButtonPress > debounceDelay) { lastButtonPress = millis(); menuIdx = (menuIdx + 2) % 3; sound_blip(); }
  if(digitalRead(BTN_DN) == LOW && millis() - lastButtonPress > debounceDelay) { lastButtonPress = millis(); menuIdx = (menuIdx + 1) % 3; sound_blip(); }
  if(digitalRead(BTN_SEL) == LOW && millis() - lastButtonPress > debounceDelay) {
    lastButtonPress = millis();
    if (menuIdx == 0) {
      display.clearDisplay(); display.setCursor(0, 20); display.println("Wiping Soul..."); display.display();
      sys.xp = 0; sys.level = 1; sys.affinity = 0;
      core_chatSummary[0] = '\0';
      rpgHP = 100; rpgGold = 0; strcpy(rpgStory, "");
      for(int i=0; i<3; i++) strcpy(rpgChoices[i], "...");
      extern const int MAX_MESSAGES; extern Message receivedMessages[];
      for(int i=0; i<MAX_MESSAGES; i++) receivedMessages[i].content[0] = '\0';
      clearCloudMemory();
      saveSys(); saveCoreMemory();
      sound_shutdown(); safeDelay(1000); LittleFS.end(); delay(100); ESP.restart();
    } else if (menuIdx == 1) { menuIdx = 0; currentMode = SELECTED_WIPE; sound_confirm(); }
    else { currentMode = MENU; sound_confirm(); }
  }
  if(digitalRead(BTN_BACK) == LOW && millis() - lastButtonPress > debounceDelay) { lastButtonPress = millis(); currentMode = MENU; }
}

inline void handleSelectedWipeMode() {
  display.clearDisplay(); display.setCursor(0, 0); display.println("-- SELECT WIPE --");
  display.drawFastHLine(0, 10, 128, WHITE);
  static const char* wipeItems[] = {"Reset Level/XP", "Forget Facts", "Clear Identity", "Clear Chat Log", "Reset RPG", "BACK"};
  int startI = (menuIdx > 3) ? menuIdx - 3 : 0;
  for(int i=0; i<5 && (startI+i) < 6; i++){
    display.setCursor(10, 15+(i*9));
    display.print(menuIdx == (startI+i) ? "> " : "  ");
    display.println(wipeItems[startI+i]);
  }
  display.display();
  if(digitalRead(BTN_UP) == LOW && millis() - lastButtonPress > debounceDelay) { lastButtonPress = millis(); menuIdx = (menuIdx + 5) % 6; sound_blip(); }
  if(digitalRead(BTN_DN) == LOW && millis() - lastButtonPress > debounceDelay) { lastButtonPress = millis(); menuIdx = (menuIdx + 1) % 6; sound_blip(); }
  if(digitalRead(BTN_SEL) == LOW && millis() - lastButtonPress > debounceDelay) {
    lastButtonPress = millis();
    bool wiped = false;
    if (menuIdx == 0) { sys.xp = 0; sys.level = 1; wiped = true; }
    else if (menuIdx == 1) {
      core_chatSummary[0] = '\0'; clearCloudKey("summary"); wiped = true;
    } else if (menuIdx == 2) {
      core_chatSummary[0] = '\0'; clearCloudKey("summary");
      extern const int MAX_MESSAGES; extern Message receivedMessages[];
      for(int i=0; i<MAX_MESSAGES; i++) receivedMessages[i].content[0] = '\0';
      sys.affinity = 0; wiped = true;
    } else if (menuIdx == 3) {
      extern const int MAX_MESSAGES; extern Message receivedMessages[];
      for(int i=0; i<MAX_MESSAGES; i++) receivedMessages[i].content[0] = '\0';
      core_chatSummary[0] = '\0'; clearCloudKey("summary"); wiped = true;
    } else if (menuIdx == 4) {
      rpgHP = 100; rpgGold = 0; strcpy(rpgStory, "");
      for(int i=0; i<3; i++) strcpy(rpgChoices[i], "...");
      clearCloudKey("rpgHP"); clearCloudKey("rpgGold"); clearCloudKey("rpgStory");
      clearCloudKey("rpgC1"); clearCloudKey("rpgC2"); clearCloudKey("rpgC3");
      wiped = true;
    } else { currentMode = MEMORY_REBOOT; sound_confirm(); return; }

    if (wiped) {
      saveSys(); saveCoreMemory();
      display.clearDisplay(); display.setCursor(20, 30); display.println("Wiped!"); display.display();
      sound_error(); safeDelay(800);
    }
  }
  if(digitalRead(BTN_BACK) == LOW && millis() - lastButtonPress > debounceDelay) { lastButtonPress = millis(); currentMode = MEMORY_REBOOT; }
}
