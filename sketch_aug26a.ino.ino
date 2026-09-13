#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <LittleFS.h>
#include <Wire.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <PubSubClient.h>
#include <time.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <nvs_flash.h>

// Include our new modules
#include "secrets.h"
#include "config.h"
#include "debug.h"

// RTC memory for shutdown flag (persists across restarts)
RTC_DATA_ATTR int shutdownFlag = 0;

Mode currentMode = FACE;
PersonalityMode sessionMode;

struct Config sys;

// --- GLOBAL VARIABLES ---
char workspace[3200]; // Expanded to handle larger chat summaries and AI requests
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Emotion currentEmotion = NEUTRAL;
uint8_t emotionVariantIndex[32]; // Per-emotion variant selector (shared across all files)
int menuIdx = 0, scanIdx = 0, foundNetworks = 0, viewIdx = 0, infoScrollOffset = 0;
bool blinking = false; unsigned long lastButtonPress = 0;
unsigned long lastBlink = 0, lastScroll = 0;
int scrollOffset = 0;
char tempSSID[33] = ""; 
char inputPass[128] = ""; // Used for WiFi pass & custom prompt
char aiMsg[256] = "IM AWAKE!";
bool pendingResponse = false; // Flag to trigger AI reply
bool pendingLevelUpAck = false;
char responsePrompt[256] = ""; // Buffer to hold the incoming message
bool pendingGeocode = false;   // Flag to trigger Location Search
char pendingGeocodeCity[64] = "";
char pendingGeocodeSender[32] = "";

bool pendingCloudSync = false; // Deferred LittleFS/MQTT sync flag
bool pendingMemorySave = false; // New: Deferred Memory.json save
bool pendingMemoryExtract = false; // Deferred memory extraction from user message
char pendingExtractMsg[256] = ""; // User message to extract facts from
unsigned long mqttBootedAt = 0; // Timestamp of MQTT connect (grace period for retained msgs)
bool pendingSysSave = false;    // New: Deferred Sys.json save
bool mqttForceReconnect = false; // Flag to force MQTT reconnect after config change
bool cloudRestored = false; // Flag to track if cloud personality restore completed
char lastSenderID[32] = ""; // ID of the last person who sent a message
bool userIsHome = false;
WiFiServer localServer(80);
// Note: WiFiServer is the same API on ESP32, no change needed here.

// --- INTENT FOLLOW-UP (news/quote) ---
bool pendingIntentFollowUp = false;
char intentType = 0; // 'N' = news, 'Q' = quote
char intentData[320] = {0}; // fetched headlines or quote
char intentContext[320] = {0}; // initial AI response (for context)

// --- CORE MEMORY ---
float yukiEnergy = 1.0f;
char core_currentObsession[32] = "";
char core_lastExchangeTone[16] = ""; // New: Tracks the tone of the last AI response
char yukiWeatherDesc[16] = "Unknown"; // New: Caches the last known weather description
int core_knownDays = 0;          // Days since first boot with WiFi
char core_chatSummary[1024] = "";
char personalityContext[1152] = "";
char systemPrompt[2048] = "";
char core_selfReflection[256] = "";
unsigned long lastSelfReflection = 0;

unsigned long obsessionSetTime = 0;

// --- EMOTIONAL DEPTH STATE ---
unsigned long lastWellbeingCheck = 0;    // Feature 2: wellbeing scan timer
char core_lastConcern[64] = "";          // Feature 3: carry worry forward
bool missedMorning = false;              // Feature 5: track missed check-in
bool missedMorningMentioned = false;     // Feature 5: don't repeat mention
bool core_badDayFlag = false;            // Feature 7: bad day arc

// --- PHYSICAL INTERACTION GLOBALS ---
unsigned long dnBtnPressStart = 0;
bool dnBtnHeld = false;
int tickleCount = 0;
unsigned long lastTicklePress = 0;
int touchAnnoyance = 0;
unsigned long lastTouchTime = 0;

// --- RPG STATE ---
int rpgHP = 100;
int rpgGold = 0; extern char rpgStory[256];

// --- MESSAGE STORAGE ---
Message receivedMessages[MAX_MESSAGES];
int messageIndex = 0;

// --- PRESENCE DETECTION ---
IPAddress phoneIP;
unsigned long lastPresencePing = 0;
unsigned long lastPresencePulse = 0;
bool prevUserIsHome = false;
bool pendingHomeGreeting = false;

char selectedRecipient[32] = "";
int charIdx = 2; 
float vcc = 0.0; // Battery voltage (0.0 = no battery / USB only)
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 18000); // UTC + 5 hours for Pakistan

// --- IDLE ACTION & SCHEDULER TIMERS ---
unsigned long lastIdleAction = 0; // The master timer for idle action cooldowns. Replaces lastThink, etc.
unsigned long lastIdleCheck = 0;  // The "heartbeat" timer for the scheduler.
unsigned long lastInteraction = 0; // Timer for user interaction (buttons, messages) to track boredom.

// Pulse system
unsigned long lastYukiPulse = 0;
unsigned long nextPulseInterval = 60000;
int consecutiveNothingCount = 0;
int pulseBurstCount = 0;

// Self-talk memory: her last 3 spoken lines (oldest -> newest), + count of valid slots
char pulseMemory[3][160] = {};
int pulseMemoryCount = 0;

// Safety guards
unsigned long lastWellbeingGuardCheck = 0;
unsigned long lastLevelUpGuardCheck = 0;

unsigned long lastWeatherUpdate = 0;
unsigned long emotionSetTime = 0;
unsigned long lastMqttReconnectAttempt = 0;
unsigned long lastActivity = 0;
unsigned long statusBarVisibleUntil = 0; // Timer for showing the top status bar
unsigned long soundEnabledAt = 0; // Sounds muted until this millis() time (boot delay)
int statusBarPage = 0; // Which info page the status bar is showing (0-2)
unsigned long statusBarPageSwitchTime = 0; // When to switch to next status bar page
unsigned long microExpressionUntil = 0;
unsigned long silentThoughtUntil = 0;
Emotion microEmotion = NEUTRAL;
unsigned long mqttBackoffMs = 5000; // Exponential backoff starting value
bool lowVoltageMode = false;
unsigned long lastHeavyOp = 0;       // Brownout cooldown tracker
int wifiFailCount = 0;               // Proxy for voltage: repeated failures → throttle
unsigned long brownoutRecoveryAt = 0; // When to try exiting brownout safe-mode
const unsigned long HEAVY_OP_COOLDOWN = 150; // ms minimum gap between high-current ops

// --- YUKI SLEEP / GROGGY STATE ---
bool yukiSleeping = false;
unsigned long yukiSleepSince = 0;
unsigned long yukiSleepDuration = 0;
bool yukiGroggy = false;
unsigned long yukiGroggySince = 0;
unsigned long yukiGroggyDuration = 0;
bool justWokeFromYukiSleep = false;
unsigned long lastWakeTime = 0;
bool hasSleptSinceBoot = false;
bool pendingWakeReply = false;
char pendingWakeMsg[640];

// Forward declarations for Yuki sleep functions
void enterYukiSleep();
void wakeYukiSleep();
void exitGroggy();

// Helper: enforce minimum gap between heavy operations
void heavyOpCooldown() {
  unsigned long elapsed = millis() - lastHeavyOp;
  if (elapsed < HEAVY_OP_COOLDOWN) {
    delay(HEAVY_OP_COOLDOWN - elapsed);
    yield();
  }
  lastHeavyOp = millis();
}

// Forward declarations for functions defined in trailing headers
void drawAesthetica();
void safeDelay(int ms);

bool canAllocJson(size_t sz) {
  // ESP32 heap is not fragmented like ESP8266; use getFreeHeap() directly.
  return ESP.getFreeHeap() > (sz + 8192);
}

// --- LIGHTWEIGHT MEMORY EXTRACTION ---
// Called from loop() when AI forgot to tag personal facts
// Makes a tiny API call asking the model to extract facts from the user's message
void extractMemoryFact() {
  if (!pendingMemoryExtract || strlen(pendingExtractMsg) == 0) return;
  if (WiFi.status() != WL_CONNECTED) { pendingMemoryExtract = false; return; }
  if (!canAllocJson(1024)) { pendingMemoryExtract = false; return; }

  heavyOpCooldown();

  char authHeader[128];
  snprintf(authHeader, sizeof(authHeader), "Bearer %s", API_KEY);

  // Build a minimal extraction prompt
  char extractPrompt[320];
  snprintf(extractPrompt, sizeof(extractPrompt),
    "Extract ONE personal fact from this user message. "
    "Output ONLY [MEM+: fact] or [MEM+: NONE]. No other text. "
    "User said: \"%s\"",
    pendingExtractMsg);

  DynamicJsonDocument doc(1024);
  doc["model"] = "qwen/qwen3.6-27b";
  doc["temperature"] = 0.1;
  doc["max_tokens"] = 60;
  JsonArray messages = doc.createNestedArray("messages");
  JsonObject sysMsg = messages.createNestedObject();
  sysMsg["role"] = "system";
  sysMsg["content"] = "You extract personal facts. Output ONLY the [MEM+:] tag.";
  JsonObject userMsg = messages.createNestedObject();
  userMsg["role"] = "user";
  userMsg["content"] = extractPrompt;

  String body;
  serializeJson(doc, body);

  WiFiClientSecure extractClient;
  extractClient.setCACert(NULL);
  extractClient.setInsecure();
  HTTPClient http;
  http.begin(extractClient, "https://api.groq.com/openai/v1/chat/completions");
  http.setTimeout(15000);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", authHeader);

  int httpCode = http.POST(body);
  if (httpCode > 0 && httpCode == 200) {
    String resp = http.getString();
    // Parse response
    DynamicJsonDocument respDoc(512);
    deserializeJson(respDoc, resp);
    const char* content = respDoc["choices"][0]["message"]["content"];
    if (content) {
      // Look for [MEM+: ...] in response
      const char* tag = strstr(content, "[MEM+:");
      if (tag) {
        const char* start = tag + 6;
        const char* end = strchr(start, ']');
        if (end && (end - start) > 4) {
          int factLen = end - start;
          if (factLen < 80) {
            char fact[84];
            strncpy(fact, start, factLen);
            fact[factLen] = '\0';
            // Deduplicate
            if (!strstr(core_chatSummary, fact)) {
              int curLen = strlen(core_chatSummary);
              if (curLen + factLen + 3 < 1023) {
                if (curLen > 0) strncat(core_chatSummary, " | ", 1023 - curLen - 1);
                strncat(core_chatSummary, fact, 1023 - strlen(core_chatSummary) - 1);
                saveCoreMemory();
                pendingCloudSync = true;
                LOGI("MEM","LLM extracted: %s", fact);
              }
            }
          }
        }
      }
    }
  } else {
    LOGW("MEM","Extract API failed: %d", httpCode);
  }
  http.end();
  pendingMemoryExtract = false;
  pendingExtractMsg[0] = '\0';
}

static void enterLowVoltageMode() {
  if (lowVoltageMode) return;
  lowVoltageMode = true;
  LOGW("PWR","Entering low-voltage safe-mode");
  // Reduce background activity
  sys.autoThink = false;
  sys.soundOn = false; // mute optional audio to save power
  mqttBackoffMs = 120000; // back off MQTT attempts
  // Cancel pending heavy work
  pendingLevelUpAck = false;
  pendingCloudSync = false;
  pendingMemorySave = false;
  pendingMemoryExtract = false;
  pendingSysSave = false;
  // Show a gentle message
  strncpy(aiMsg, "Low power — conserving.", sizeof(aiMsg)-1);
  aiMsg[sizeof(aiMsg)-1] = '\0';
  drawAesthetica();
}

static void exitLowVoltageMode() {
  if (!lowVoltageMode) return;
  lowVoltageMode = false;
  LOGI("PWR","Exiting low-voltage safe-mode");
  sys.autoThink = true;
  sys.soundOn = true;
  mqttBackoffMs = 5000;
  strncpy(aiMsg, "Power level normal.", sizeof(aiMsg)-1);
  aiMsg[sizeof(aiMsg)-1] = '\0';
  drawAesthetica();
}

// --- DIAGNOSTICS ---
unsigned long lastHeapLog = 0;

inline void heapLogAndMaybeRestart(const char* tag) {
#if ENABLE_HEAP_LOG
  uint32_t freeh = ESP.getFreeHeap();
  LOGD("HEAP","%s free=%u", tag, freeh);
  // Try to append a small log record to LittleFS (best-effort)
  // Truncate if file exceeds 1KB to prevent filling LittleFS
  File f = LittleFS.open("/last_heap.log", "a");
  if (f) {
    if (f.size() > 1024) {
      f.close();
      f = LittleFS.open("/last_heap.log", "w"); // Truncate
    }
  }
  if (f) {
    char buf[128];
    snprintf(buf, sizeof(buf), "%lu,%s,free=%u,lastActivity=%lu\n", millis(), tag, freeh, lastActivity);
    f.print(buf);
    f.close();
  }
  if (freeh < HEAP_WARN_THRESHOLD) {
    LOGE("HEAP","HEAP CRITICAL - restarting to recover");
    safeDelay(200);
    LittleFS.end(); delay(50);
    ESP.restart();
  }
#endif
}

bool rpgStoryUpdated = false; // Flag to trigger scroll reset without 256-byte snapshot
bool saidGoodMorning = false; // Flags for time-based proactive chat
bool saidBirthdayToday = false;
bool saidGoodNight = false;

// Weather Watcher
int previousWeatherCode = -1;
unsigned long lastWeatherChangeTime = 0;

// Game State
int gameSecretNumber = 0;
int gameCurrentGuess = 5;
int gameTries = 0;

// Forward declarations
void gainXP(int amount);
void pickBootMessage();
void syncAI(const char* prompt, bool isPersonal, Mode returnMode, bool bypassCooldown, bool speak);
void handleGameRPGMode();
void handleConfirmSaveRPGMode();
void applyMoodDrift();

// Atomic write helper: write buffer to tmp file then rename to target.
static bool atomicWriteFile(const char* path, const char* buf, size_t len) {
  char tmpPath[64];
  snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", path);
  File f = LittleFS.open(tmpPath, "w");
  if (!f) {
    LOGE("FS","AtomicWrite: open tmp failed: %s", tmpPath);
    return false;
  }
  size_t written = f.write((const uint8_t*)buf, len);
  f.close();
  if (written != len) {
    LOGE("FS","AtomicWrite: write incomplete");
    LittleFS.remove(tmpPath);
    return false;
  }
  // Remove existing file then rename tmp to final
  if (LittleFS.exists(path)) LittleFS.remove(path);
  bool ok = LittleFS.rename(tmpPath, path);
  if (!ok) LOGE("FS","AtomicWrite: rename failed for %s", path);
  return ok;
}

// --- LittleFS Space Management ---
// Hard caps for growing strings (prevents LittleFS overflow)
#define CHAT_SUMMARY_MAX    800
#define PERSONALITY_CTX_MAX 1152
#define SYSTEM_PROMPT_MAX   2048
#define SELF_REFLECTION_MAX 256

// Cap a string to max length, keeping the END (most recent) - truncates from start
void capString(char* str, size_t maxLen) {
  if (!str) return;
  size_t len = strlen(str);
  if (len > maxLen) {
    // Keep the end (most recent content), truncate from start
    size_t keep = maxLen;
    if (keep > maxLen) keep = maxLen;
    memmove(str, str + len - keep, keep);
    str[keep] = '\0';
    LOGI("FS","Capped string to %u chars", (unsigned)keep);
  }
}

// Apply all string caps
void enforceStringCaps() {
  capString(core_chatSummary, CHAT_SUMMARY_MAX);
  capString(personalityContext, PERSONALITY_CTX_MAX);
  capString(systemPrompt, SYSTEM_PROMPT_MAX);
  capString(core_selfReflection, SELF_REFLECTION_MAX);
  LOGD("FS","String caps enforced");
}

// Get LittleFS free space in bytes
size_t getLittleFSFreeSpace() {
  size_t total = LittleFS.totalBytes();
  size_t used = LittleFS.usedBytes();
  return total > used ? total - used : 0;
}

// Auto-cleanup: delete non-essential files when space is low
void autoCleanupLittleFS() {
  size_t freeBytes = getLittleFSFreeSpace();
  size_t totalBytes = LittleFS.totalBytes();
  
  float freePct = totalBytes ? (100.0f * freeBytes / totalBytes) : 0;
  
  if (freePct < 10.0f) {
    LOGW("FS","CRITICAL: Only %.1f%% free (%u/%u bytes) - nuclear cleanup", freePct, freeBytes, totalBytes);
    // Nuclear: backup sys.json, format, restore sys.json
    char sysBackup[1024];
    File f = LittleFS.open("/sys.json", "r");
    size_t sysLen = 0;
    if (f) {
      sysLen = f.readBytes(sysBackup, sizeof(sysBackup) - 1);
      sysBackup[sysLen] = '\0';
      f.close();
    }
    LittleFS.format();
    delay(100);
    if (sysLen > 0) {
      File f = LittleFS.open("/sys.json", "w");
      if (f) { f.write((const uint8_t*)sysBackup, sysLen); f.close(); LOGI("FS","Nuclear recovery: sys.json restored"); }
    }
    LOGI("FS","Nuclear recovery complete, free: %u bytes", getLittleFSFreeSpace());
  } else if (freePct < 20.0f) {
    LOGW("FS","LOW SPACE: %.1f%% free - deleting /scans.json", freePct);
    if (LittleFS.exists("/scans.json")) {
      LittleFS.remove("/scans.json");
      LOGI("FS","Deleted /scans.json, free: %u bytes", getLittleFSFreeSpace());
    }
    enforceStringCaps(); // Also cap strings
    // Save capped versions
    saveCoreMemory();
    saveSys();
  } else if (freePct < 30.0f) {
    LOGW("FS","SPACE WARNING: %.1f%% free (%u/%u bytes)", freePct, freeBytes, totalBytes);
  }
}

// Check and log free space periodically
void checkLittleFSSpace() {
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 60000) { // Every minute
    lastCheck = millis();
    size_t freeBytes = getLittleFSFreeSpace();
    size_t totalBytes = LittleFS.totalBytes();
    float freePct = totalBytes ? (100.0f * freeBytes / totalBytes) : 0;
    if (freePct < 30.0f) {
      LOGW("FS","Space: %.1f%% free (%u/%u bytes)", freePct, freeBytes, totalBytes);
    } else {
      LOGD("FS","Space: %.1f%% free (%u/%u bytes)", freePct, freeBytes, totalBytes);
    }
  }
}

// Include implementation headers early so functions below can see them
#include "sounds.h"
#include "bitmaps.h"
#include "drawing.h"
#include "tts.h"
#include "quotes.h"
#include "news.h"
#include "yuki_net.h" // Use the renamed file to avoid ESP32 system conflicts
#include "mqtt_handler.h"
#include "ui_handlers.h"
#include "yuki_pulse.h"

// --- YUKI SLEEP FUNCTION DEFINITIONS ---
void enterYukiSleep() {
  yukiSleeping = true;
  yukiSleepSince = millis();
  if (yukiEnergy > 0.25f)      yukiSleepDuration = random(300000, 720000);
  else if (yukiEnergy > 0.15f) yukiSleepDuration = random(600000, 1200000);
  else                         yukiSleepDuration = random(900000, 1800000);
  currentEmotion = SLEEPY;
  emotionSetTime = millis();
  strncpy(aiMsg, "zZz sleeping...", sizeof(aiMsg) - 1);
  scrollOffset = 0;
  if (mqttClient.connected()) mqttClient.disconnect(); // Clean disconnect to clear broker queue
}

void wakeYukiSleep() {
  if (!yukiSleeping) return;
  yukiSleeping = false;
  yukiGroggy = true;
  yukiGroggySince = millis();
  yukiGroggyDuration = random(30000, 120000);
  justWokeFromYukiSleep = true;
  lastWakeTime = millis();   // Grace period: don't auto-sleep again right after waking
  hasSleptSinceBoot = true;
  currentEmotion = SLEEPY;
  emotionSetTime = millis();
}

void exitGroggy() {
  yukiGroggy = false;
  justWokeFromYukiSleep = false;
  // Process any queued wake message that was held during sleep
  if (strlen(pendingWakeMsg) > 0) {
    strncpy(responsePrompt, pendingWakeMsg, sizeof(responsePrompt) - 1);
    responsePrompt[sizeof(responsePrompt) - 1] = '\0';
    pendingResponse = true;
    pendingWakeMsg[0] = '\0';
  }
}

extern char rpgStory[256];
extern char rpgChoices[3][40];
extern int rpgChoiceIdx;

// --- MEMORY VAULT ---
void saveSys() {
  if (!canAllocJson(2048)) { heapLogAndMaybeRestart("saveSys_skip"); return; }
  enforceStringCaps(); // Apply caps before saving
  DynamicJsonDocument doc(2048);
  doc["s"] = sys.ssid; doc["p"] = sys.pass;
  doc["snd"] = sys.soundOn; doc["x"] = sys.xp; doc["l"] = sys.level;
  doc["at"] = sys.autoThink;
  doc["aff"] = sys.affinity;
  doc["lastEmo"] = (int)currentEmotion;
  doc["lat"] = sys.latitude;
  doc["lon"] = sys.longitude;
  doc["scrl"] = sys.scrollSpeed;
  doc["tz"] = sys.timeZoneOffset;
  doc["rpgHP"] = rpgHP;
  doc["rpgG"] = rpgGold;
  doc["rpgStory"] = rpgStory;
  doc["rpgC1"] = rpgChoices[0];
  doc["rpgC2"] = rpgChoices[1];
  doc["rpgC3"] = rpgChoices[2];
  doc["rpgSel"] = rpgChoiceIdx;
  doc["ghs"] = sys.guessHighScore;
  doc["pip"] = sys.phoneIP;
  doc["sv"] = sys.soundVolume;
  doc["amb"] = sys.ambientSounds;
  doc["bt"] = sys.bootSound;
  doc["sc"] = sys.soundScents;
  doc["beep"] = sys.beepsOn;
  doc["vOn"] = sys.voiceOn;
  doc["vPulse"] = sys.voicePulse;
  doc["vPol"] = sys.voicePolarity;
  doc["vDead"] = sys.voiceDeadband;
  doc["vSprd"] = sys.voiceSpread;
  doc["vGain"] = sys.voiceGain;
  doc["vName"] = sys.voiceName;
  doc["vRate"] = sys.voiceRate;
  doc["vPitch"] = sys.voicePitch;
  doc["ssv"] = sys.screenSaver;
  doc["ith"] = sys.innerThread;
  doc["mqttSrv"] = sys.mqttServer;
  doc["mqttPort"] = sys.mqttPort;
  doc["devId"] = sys.deviceId;
  doc["charNm"] = sys.characterName;
  doc["phCtc"] = sys.phoneContactId;
  doc["pipMqtt"] = sys.phoneIP_mqtt;
  doc["ttsUrl"] = sys.ttsRelayUrl;
  doc["txPow"] = sys.txPower;
  doc["lct"] = sys.lastConversationTime;
  doc["lqr"] = sys.lastQuoteRefresh;
  size_t p = serializeJson(doc, workspace, sizeof(workspace));
  atomicWriteFile("/sys.json", workspace, p);
}

void gainXP(int amount) {
  if (amount <= 0) return;

  int prevLevel = sys.level;
  sys.xp += amount;
  int requiredXP = 100 + ((sys.level - 1) * 50);

  if (sys.xp >= requiredXP) {
    sys.level++;
    sys.xp -= requiredXP; // Carry over extra XP
    strcpy(aiMsg, "LEVEL UP! ^w^");
    scrollOffset = 0;
    currentEmotion = HAPPY; // Make her happy on level up
    emotionSetTime = millis();
    sound_level_up();
    logLocalChat("Yuki", aiMsg);
    if (sys.level > prevLevel) {
      if (random(0, 10) < 3) { // 30% chance she acknowledges it
        pendingLevelUpAck = true;
      }
    }
  }
  saveSys(); // Immediate save for XP/Level to survive potential brownouts
  yield();
}

void saveCoreMemory() {
  if (!canAllocJson(3072)) { heapLogAndMaybeRestart("saveCore_skip"); return; }
  enforceStringCaps(); // Apply caps before saving
  DynamicJsonDocument doc(3072);
  doc["journal"] = core_chatSummary;
  doc["obsession"] = core_currentObsession;
  doc["kdays"] = core_knownDays;
  doc["obsessionTime"] = (unsigned long)obsessionSetTime;
  size_t p = serializeJson(doc, workspace, sizeof(workspace));
  atomicWriteFile("/memory.json", workspace, p);
}

void loadCoreMemory() {
  File f = LittleFS.open("/memory.json", "r");
  if (f) {
    if (!canAllocJson(3072)) { LOGW("FS","Skipping loadCoreMemory - low heap"); f.close(); return; }
    DynamicJsonDocument doc(3072);
    DeserializationError err = deserializeJson(doc, f);
    if (err) {
      LOGW("FS","memory.json parse failed: %s", err.c_str());
      f.close();
      return;
    }
    strncpy(core_chatSummary, doc["journal"] | "", sizeof(core_chatSummary) - 1);
    core_chatSummary[sizeof(core_chatSummary) - 1] = '\0';
    strncpy(core_currentObsession, doc["obsession"] | "", sizeof(core_currentObsession) - 1);
    core_currentObsession[sizeof(core_currentObsession) - 1] = '\0';
    core_knownDays = doc["kdays"] | 0;
    strncpy(core_lastExchangeTone, doc["lxt"] | "", sizeof(core_lastExchangeTone) - 1);
    core_lastExchangeTone[sizeof(core_lastExchangeTone) - 1] = '\0';
    obsessionSetTime = doc["obsessionTime"] | 0;
    f.close();
    enforceStringCaps(); // Apply caps after loading
  }
}

// --- LIVELY STARTUP LOGIC ---
void pickBootMessage() {
  strncpy(aiMsg, "System online.", sizeof(aiMsg) - 1);
  aiMsg[sizeof(aiMsg) - 1] = '\0';
  syncAI("You just powered on. Give a natural, character-appropriate greeting.", false, FACE, false, true);
}

bool messageFeelsHeavy(const char* msg) {
  // Move heavy words to Flash to save RAM
  static const char h0[] PROGMEM = "tired"; static const char h1[] PROGMEM = "fail";
  static const char h2[] PROGMEM = "sad"; static const char h3[] PROGMEM = "miss";
  static const char h4[] PROGMEM = "sorry"; static const char h5[] PROGMEM = "hate";
  static const char h6[] PROGMEM = "cry"; static const char h7[] PROGMEM = "hurt";
  static const char h8[] PROGMEM = "lost"; static const char h9[] PROGMEM = "broke";
  static const char h10[] PROGMEM = "stressed"; static const char h11[] PROGMEM = "scared";
  static const char h12[] PROGMEM = "annoyed"; static const char h13[] PROGMEM = "angry";
  static const char h14[] PROGMEM = "upset"; static const char h15[] PROGMEM = "stuck";
  static const char h16[] PROGMEM = "overwhelmed";
  static const char* const heavyWords[] PROGMEM = {h0,h1,h2,h3,h4,h5,h6,h7,h8,h9,h10,h11,h12,h13,h14,h15,h16};
  
  char buffer[16];
  for (int i = 0; i < 17; i++) {
    strcpy_P(buffer, (char*)pgm_read_ptr(&(heavyWords[i])));
    if (strstr(msg, buffer)) return true;
  }
  return false;
}

// I2C bus clear: bit-bang SCL 9 times with SDA high to release hung slaves
static void i2c_clear_bus() {
  pinMode(21, OUTPUT);
  pinMode(1, OUTPUT);
  digitalWrite(21, HIGH);
  digitalWrite(1, HIGH);
  delayMicroseconds(10);
  for (int i = 0; i < 9; i++) {
    digitalWrite(1, LOW);
    delayMicroseconds(10);
    digitalWrite(1, HIGH);
    delayMicroseconds(10);
  }
  digitalWrite(21, LOW);
  delayMicroseconds(10);
  digitalWrite(21, HIGH);
  delayMicroseconds(10);
  pinMode(21, INPUT_PULLUP);
  pinMode(1, INPUT_PULLUP);
}

// I2C scanner: probe all addresses 0x01-0x7F, print to serial, return first found or 0
static uint8_t i2c_scan() {
  Serial.println("I2C scanner: probing addresses 0x01-0x7F...");
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission();
    if (err == 0) {
      Serial.printf("  Found device at 0x%02X\n", addr);
      return addr;
    }
  }
  Serial.println("  No I2C devices found");
  return 0;
}

// --- MAIN SETUP ---
void setup() {
  // Seed the random number generator using an unconnected analog pin
  randomSeed(esp_random()); // ESP32 hardware RNG — better entropy than ADC
  
  // Session mode reflects the relationship state, not pure random
  // (Loaded sys.affinity from sys.json hasn't happened yet at this point,
  //  so this runs after loadSys below — moved to after loadCoreMemory)
  
  Serial.begin(115200);
  delay(500);
  nvs_flash_erase();   // One final erase — clears stale corruption from past erase cycles
  nvs_flash_init();    // Fresh init

  // Fix 2: Log why we reset
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  LOGI("BOOT","Reset reason: Power-on"); break;
    case ESP_RST_EXT:      LOGI("BOOT","Reset reason: External pin"); break;
    case ESP_RST_SW:       LOGI("BOOT","Reset reason: Software restart"); break;
    case ESP_RST_PANIC:    LOGI("BOOT","Reset reason: Crash/panic"); break;
    case ESP_RST_INT_WDT:  LOGI("BOOT","Reset reason: Interrupt watchdog"); break;
    case ESP_RST_TASK_WDT: LOGI("BOOT","Reset reason: Task watchdog"); break;
    case ESP_RST_WDT:      LOGI("BOOT","Reset reason: Other watchdog"); break;
    case ESP_RST_DEEPSLEEP:LOGI("BOOT","Reset reason: Deep sleep wake"); break;
    case ESP_RST_BROWNOUT: LOGE("BOOT","Reset reason: BROWNOUT"); break;
    default:               LOGI("BOOT","Reset reason: Unknown (%d)", esp_reset_reason());
  }

  i2c_clear_bus();
  pinMode(21, INPUT_PULLUP); pinMode(1, INPUT_PULLUP);
  Wire.begin(21, 1);
  // Keep libc time in UTC; app-level offsets are applied manually.
  setenv("TZ", "UTC0", 1);
  tzset();
  Wire.setClock(50000);   // 50 kHz — reduces EMI coupling into I2C lines

  uint8_t oledAddr = 0;
  uint8_t found = i2c_scan();
  if (found == 0x3C || found == 0x3D) {
    oledAddr = found;
  } else {
    uint8_t tryAddrs[] = {0x3C, 0x3D};
    for (int i = 0; i < 2; i++) {
      Wire.beginTransmission(tryAddrs[i]);
      if (Wire.endTransmission() == 0) { oledAddr = tryAddrs[i]; break; }
    }
  }

  if (oledAddr) {
    if (!display.begin(SSD1306_SWITCHCAPVCC, oledAddr)) {
      Serial.printf("SSD1306 init failed at 0x%02X\n", oledAddr);
      while (true) delay(1000);
    }
Serial.printf("SSD1306 initialized at 0x%02X\n", oledAddr);
   } else {
     Serial.println("No SSD1306 display found on I2C bus");
     while (true) delay(1000);
   }

  // Check for shutdown flag from previous session (using RTC memory)
  if (shutdownFlag == 1) {
    shutdownFlag = 0; // Clear flag
    display.clearDisplay(); 
    display.setCursor(0, 20); 
    display.println("Safe to power off"); 
    display.display();
    while(1) { delay(100); yield(); } // Wait for power cut
  }

  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed, formatting...");
    LittleFS.format();
    if (!LittleFS.begin()) {
      Serial.println("LittleFS still fails after format — continuing without filesystem");
    }
  }

  // Factory reset: hold BACK + SELECT during boot
  pinMode(BTN_BACK, INPUT_PULLUP);
  pinMode(BTN_SEL, INPUT_PULLUP);
  delay(50);
  if (digitalRead(BTN_BACK) == LOW && digitalRead(BTN_SEL) == LOW) {
    Serial.println("FACTORY RESET: BTN_BACK+BTN_SEL held — erasing all data");
    display.clearDisplay();
    display.println("Factory Reset...");
    display.display();
    nvs_flash_erase();
    nvs_flash_init();
    LittleFS.end();
    LittleFS.format();
    delay(500);
    ESP.restart();
  }

  File f = LittleFS.open("/sys.json", "r");
  if(f) {
    if (!canAllocJson(1536)) { LOGW("FS","Skipping sys.json load - low heap"); f.close(); }
    else {
      DynamicJsonDocument d(1536); 
      DeserializationError err = deserializeJson(d, f);
      if (err) {
        LOGW("FS","sys.json parse failed: %s", err.c_str());
        f.close();
      } else {
    strncpy(sys.ssid, d["s"] | "", sizeof(sys.ssid) - 1);
    sys.ssid[sizeof(sys.ssid)-1] = '\0';
    strncpy(sys.pass, d["p"] | "", sizeof(sys.pass) - 1);
    sys.pass[sizeof(sys.pass)-1] = '\0';
    sys.soundOn = d["snd"] | true; 
    sys.xp = d["x"] | 0; 
    sys.level = d["l"] | 1;
    sys.autoThink = d["at"] | true;
    sys.affinity = d["aff"] | 0;
    currentEmotion = (Emotion)(int)(d["lastEmo"] | 0);
    if (currentEmotion == ANGRY || currentEmotion == SAD || currentEmotion == SHOCKED || currentEmotion == SLEEPY) {
      currentEmotion = NEUTRAL;
    }
    emotionSetTime = millis();
    sys.latitude = d["lat"] | 33.68; // Default to Islamabad if not found
    sys.longitude = d["lon"] | 73.04;
    sys.scrollSpeed = d["scrl"] | 1;
    sys.timeZoneOffset = d["tz"] | 18000;
    rpgHP = d["rpgHP"] | 100;
    rpgGold = d["rpgG"] | 0;
    strncpy(rpgStory, d["rpgStory"] | "Loading story...", sizeof(rpgStory) - 1);
    rpgStory[sizeof(rpgStory) - 1] = '\0';
    strncpy(rpgChoices[0], d["rpgC1"] | "...", sizeof(rpgChoices[0]) - 1);
    rpgChoices[0][sizeof(rpgChoices[0]) - 1] = '\0';
    strncpy(rpgChoices[1], d["rpgC2"] | "...", sizeof(rpgChoices[1]) - 1);
    rpgChoices[1][sizeof(rpgChoices[1]) - 1] = '\0';
    strncpy(rpgChoices[2], d["rpgC3"] | "...", sizeof(rpgChoices[2]) - 1);
    rpgChoices[2][sizeof(rpgChoices[2]) - 1] = '\0';
    rpgChoiceIdx = d["rpgSel"] | 0;
    if (rpgChoiceIdx < 0 || rpgChoiceIdx > 2) rpgChoiceIdx = 0;
    sys.guessHighScore = d["ghs"] | 999;
    strncpy(sys.phoneIP, d["pip"] | DEFAULT_PHONE_IP, sizeof(sys.phoneIP) - 1);
    sys.phoneIP[sizeof(sys.phoneIP) - 1] = '\0';
    phoneIP.fromString(sys.phoneIP);
    sys.soundVolume = d["sv"] | 50;
    sys.ambientSounds = d["amb"] | true;
    sys.bootSound = d["bt"] | true;
    sys.soundScents = d["sc"] | true;
    sys.beepsOn = d["beep"] | true;
    sys.voiceOn = d["vOn"] | true;
    sys.voicePulse = d["vPulse"] | 55;
    sys.voicePolarity = d["vPol"] | 0;
    sys.voiceDeadband = d["vDead"] | 0;
    sys.voiceSpread = d["vSprd"] | 1;
    sys.voiceGain = d["vGain"] | 125;
    strncpy(sys.voiceName, d["vName"] | "en-US-AriaNeural", sizeof(sys.voiceName) - 1);
    sys.voiceName[sizeof(sys.voiceName) - 1] = '\0';
    sys.voiceRate = d["vRate"] | 10;
    sys.screenSaver = d["ssv"] | true;
    sys.voicePitch = d["vPitch"] | 0;
    strncpy(sys.innerThread, d["ith"] | "", sizeof(sys.innerThread) - 1);
    sys.innerThread[sizeof(sys.innerThread) - 1] = '\0';
    if (strlen(sys.innerThread)) {
      strncpy(pulseMemory[0], sys.innerThread, 159);
      pulseMemory[0][159] = '\0';
      pulseMemoryCount = 1;
    }
    strncpy(sys.mqttServer, d["mqttSrv"] | "", sizeof(sys.mqttServer) - 1);
    sys.mqttServer[sizeof(sys.mqttServer) - 1] = '\0';
    sys.mqttPort = d["mqttPort"] | 0;
    strncpy(sys.deviceId, d["devId"] | "", sizeof(sys.deviceId) - 1);
    sys.deviceId[sizeof(sys.deviceId) - 1] = '\0';
    strncpy(sys.characterName, d["charNm"] | "", sizeof(sys.characterName) - 1);
    sys.characterName[sizeof(sys.characterName) - 1] = '\0';
    strncpy(sys.phoneContactId, d["phCtc"] | "", sizeof(sys.phoneContactId) - 1);
    sys.phoneContactId[sizeof(sys.phoneContactId) - 1] = '\0';
    strncpy(sys.phoneIP_mqtt, d["pipMqtt"] | "", sizeof(sys.phoneIP_mqtt) - 1);
    sys.phoneIP_mqtt[sizeof(sys.phoneIP_mqtt) - 1] = '\0';
    strncpy(sys.ttsRelayUrl, d["ttsUrl"] | "", sizeof(sys.ttsRelayUrl) - 1);
    sys.ttsRelayUrl[sizeof(sys.ttsRelayUrl) - 1] = '\0';
    sys.txPower = d["txPow"] | 20;
    sys.lastConversationTime = d["lct"] | 0;
    sys.lastQuoteRefresh = d["lqr"] | 0;
      f.close();
      }
    }
  }
  
  // If no saved SSID (or corrupted), use default
  {
    bool corrupted = false;
    for (size_t i = 0; i < sizeof(sys.ssid) && sys.ssid[i]; i++) {
      if (sys.ssid[i] < 32 || sys.ssid[i] > 126) { corrupted = true; break; }
    }
    if (strlen(sys.ssid) == 0 || corrupted) {
      if (corrupted) Serial.println("SSID corrupted — falling back to default");
      strncpy(sys.ssid, DEFAULT_SSID, sizeof(sys.ssid) - 1);
      sys.ssid[sizeof(sys.ssid) - 1] = '\0';
      strncpy(sys.pass, DEFAULT_PASS, sizeof(sys.pass) - 1);
      sys.pass[sizeof(sys.pass) - 1] = '\0';
    }
  }
  
  pinMode(BTN_UP, INPUT_PULLUP); pinMode(BTN_DN, INPUT_PULLUP);
  pinMode(BTN_SEL, INPUT_PULLUP); pinMode(BTN_BACK, INPUT_PULLUP);
  pinMode(BTN_MODIFIER, INPUT_PULLUP);
  pinMode(SPK_PIN, OUTPUT); digitalWrite(SPK_PIN, HIGH);

  // Calibrate ADC for future battery reading support
  // To enable: define BAT_PIN, add voltage divider, and implement analogRead()
  analogSetAttenuation(ADC_11db); 

  // --- BROWNOUT DETECTION ---
  if (esp_reset_reason() == ESP_RST_BROWNOUT) {
    LOGW("PWR","Previous reset was BROWNOUT — entering safe-mode for 5 min");
    lowVoltageMode = true;
    sys.soundOn = false;
    sys.autoThink = false;
    mqttBackoffMs = 120000;
    brownoutRecoveryAt = millis() + 300000;
    strncpy(aiMsg, "Low power — conserving.", sizeof(aiMsg)-1);
    aiMsg[sizeof(aiMsg)-1] = '\0';
  }
  // --- END BROWNOUT DETECTION ---

  sound_boot_stretch();
  sound_wake_stretch();
  soundEnabledAt = millis() + SOUND_BOOT_DELAY; // Enable sounds after boot delay
  btStop();                      // Disable Bluetooth — not used; saves ~20 mA
  WiFi.persistent(true);         // Persist WiFi config to NVS
  
  loadCoreMemory(); // Load memory from flash
  enforceStringCaps(); // Apply string caps after loading
  autoCleanupLittleFS(); // Clean up LittleFS if space is low

  // Feature 4: Session mode reflects the relationship, not random
  if (sys.affinity > 60) {
    // Warm relationship — chatty or reflective
    sessionMode = (random(0, 2) == 0) ? PM_CHATTY : PM_REFLECTIVE;
  } else if (sys.affinity < -30) {
    // Strained — quiet or sassy
    sessionMode = (random(0, 2) == 0) ? PM_QUIET : PM_SASSY;
  } else {
    // Neutral — anything goes
    sessionMode = (PersonalityMode)random(0, 4);
  }

  // Integrity guard: prevent corrupt saves from wiping progress
  if (sys.level < 1)  sys.level = 1;
  if (sys.xp < 0)     sys.xp = 0;
  if (sys.affinity < -100 || sys.affinity > 100) sys.affinity = 0;

  if(strlen(sys.ssid) >= 1) tryConnect();

  // Cloud personality restore will happen after MQTT connects in loop()

  setCpuFrequencyMhz(80);        // 80 MHz saves power — WiFi already initialized

  // Apply Timezone
  timeClient.setTimeOffset(sys.timeZoneOffset);

  // Initial Location & Weather Update
  if (WiFi.status() == WL_CONNECTED) {
    autoDetectLocation();
    delay(800);          // Slightly longer — let location settle before weather hits
    updateWeather();
    if (weatherCode == -1) {   // First attempt failed — retry once
      delay(2000);
      updateWeather();
    }
  }
  lastWeatherUpdate = millis();
  
  if (WiFi.status() == WL_CONNECTED) {
    timeClient.update();

    // --- MULTI-DAY ABSENCE CALCULATION ---
    if (sys.lastConversationTime > 0 && timeClient.isTimeSet()) {
      unsigned long now = timeClient.getEpochTime();
      if (now > sys.lastConversationTime) {
        unsigned long absenceSec = now - sys.lastConversationTime;
        unsigned long absenceHrs = absenceSec / 3600;

        if (absenceHrs >= 72) {
          // TIER 4: Deep absence (3+ days) — resigned, subdued
          currentEmotion = NEUTRAL;
          sessionMode = PM_QUIET;
          sys.affinity = constrain(sys.affinity - 8, -100, 100);
          yukiEnergy = min(yukiEnergy + 0.3f, 1.0f); // Rested while waiting
          core_badDayFlag = true;
          LOGI("ABSENT","Deep absence: %lu hrs, affinity now %d", (unsigned long)absenceHrs, sys.affinity);
        } else if (absenceHrs >= 24) {
          // TIER 3: Hurt (1-3 days) — sad, remembers being left
          currentEmotion = SAD;
          sessionMode = PM_SASSY;
          sys.affinity = constrain(sys.affinity - 5, -100, 100);
          yukiEnergy = min(yukiEnergy + 0.2f, 1.0f);
          LOGI("ABSENT","Hurt absence: %lu hrs, affinity now %d", (unsigned long)absenceHrs, sys.affinity);
        } else if (absenceHrs >= 12) {
          // TIER 2: Noticed (12-24 hrs) — concerned, questioning
          currentEmotion = CONFUSED;
          sessionMode = PM_CHATTY;
          yukiEnergy = min(yukiEnergy + 0.15f, 1.0f);
          LOGI("ABSENT","Noticed absence: %lu hrs", (unsigned long)absenceHrs);
        } else if (absenceHrs >= 2) {
          // TIER 1: Missed (2-12 hrs) — happy they're back
          currentEmotion = HAPPY;
          yukiEnergy = min(yukiEnergy + 0.1f, 1.0f);
          LOGI("ABSENT","Missed absence: %lu hrs", (unsigned long)absenceHrs);
        }
        // < 2 hrs: no change
      }
    } else if (sys.lastConversationTime == 0) {
      // First boot ever — special first-meeting state
      LOGI("ABSENT","First boot — no previous conversation recorded");
    }

    // --- WEEKLY QUOTE REFRESH ---
    checkQuoteRefresh();

    emotionSetTime = millis();

    pickBootMessage();
  }
  display.setTextColor(WHITE);

  setupMqtt();
  localServer.begin();
  if (strlen(sys.phoneIP) == 0) phoneIP.fromString(DEFAULT_PHONE_IP);
  lastActivity = millis(); // Start activity timer
  lastInteraction = millis(); // Start interaction timer for boredom
  lastIdleCheck = millis(); // Start idle scheduler timer
  lastIdleAction = millis(); // Don't perform an idle action immediately on boot
  lastYukiPulse = millis(); // Don't pulse immediately on boot
}

void handleLocalPulse() {
  WiFiClient client = localServer.available();
  if (!client) return;
  
  char request[64] = {0};
  client.readBytesUntil('\r', request, sizeof(request) - 1);
  client.flush();

  if (strstr(request, "/home") != nullptr) {
    userIsHome = true;
    LOGI("PULSE","User is HOME");
  } else if (strstr(request, "/away") != nullptr) {
    userIsHome = false;
    LOGI("PULSE","User is AWAY");
  }
  
  client.print("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nOK");
  delay(1);
}

// --- MAIN LOOP ---
void loop() {
  // --- CRITICAL: FLUSH SAVES BEFORE AI/WIFI SPIKES ---
  // We move these to the very top to ensure data is safe before power-hungry tasks
  if (pendingCloudSync) {
    heavyOpCooldown();
    if (WiFi.status() == WL_CONNECTED) syncMemoryToCloud();
    yield();
    pendingCloudSync = false;
  }
  if (pendingMemorySave) {
    heavyOpCooldown();
    saveCoreMemory();
    yield();
    pendingMemorySave = false;
  }
  if (pendingMemoryExtract) {
    extractMemoryFact();
    yield();
  }
  if (pendingSysSave) {
    heavyOpCooldown();
    saveSys();
    yield();
    pendingSysSave = false;
  }

  // --- IDLE WiFi POWER SAVING ---
  // Put WiFi in modem-sleep during idle to reduce baseline draw
  if (WiFi.status() == WL_CONNECTED && millis() - lastHeavyOp > 2000) {
    WiFi.setSleep(true);
  } else if (WiFi.status() == WL_CONNECTED) {
    WiFi.setSleep(false);
  }

  // --- SLEEP CHECK ---
  if (millis() - lastActivity > sleepTimeout && !yukiSleeping) {
    display.clearDisplay();
    display.setCursor(10, 30);
    display.println("Entering Sleep...");
    strncpy(aiMsg, "...", sizeof(aiMsg) - 1);
    aiMsg[sizeof(aiMsg) - 1] = '\0';
    display.display();
    sound_sleep_farewell();
    delay(1000); // Show message before sleeping
    
    display.ssd1306_command(SSD1306_DISPLAYOFF);
    
    WiFi.setSleep(true); // ESP32 equivalent
    if (mqttClient.connected()) mqttClient.disconnect();
    delay(100);
    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_OFF);
    delay(50);
    delay(10); // Give it time to settle
    
    unsigned long sleepStartTime = millis();
    bool autoWoke = false;
    // Wait for any button press OR for the auto-wakeup timer to expire
    while(digitalRead(BTN_UP) == HIGH && digitalRead(BTN_DN) == HIGH && 
          digitalRead(BTN_SEL) == HIGH && digitalRead(BTN_BACK) == HIGH) {
      
      if (millis() - sleepStartTime > autoWakeupDuration) {
        autoWoke = true; // Mark that we woke up automatically
        break; // Exit the loop to wake up automatically
      }

      delay(100); yield(); // ESP32 does not need manual WDT feeding
    }
    
    bool buttonWoke = !autoWoke;

    heavyOpCooldown();
    WiFi.mode(WIFI_STA);
    delay(50);
    WiFi.setTxPower((wifi_power_t)sys.txPower);
    WiFi.begin(sys.ssid, sys.pass);

    // Graceful Reconnect: Check for 5 seconds before forcing the "Connecting" UI
    unsigned long reconStart = millis();
    while(WiFi.status() != WL_CONNECTED && millis() - reconStart < 5000) {
      delay(100);
      yield();
    }
    if(WiFi.status() != WL_CONNECTED && strlen(sys.ssid) > 1) tryConnect();

    heavyOpCooldown();          // Fix 1: Voltage recovery after WiFi burst

    // Prevent immediate MQTT reconnect after wake-up
    lastMqttReconnectAttempt = millis();
    lastIdleAction = millis();

    heavyOpCooldown();          // Fix 1: Before display power-up
    display.ssd1306_command(SSD1306_DISPLAYON);
    delay(100);

    heavyOpCooldown();          // Fix 1: Before sound
    sound_wake_stretch_short();
    delay(150);

    // Draw face immediately while potentially waiting for AI
    aiMsg[0] = '\0';
    drawAesthetica();

    delay(500);

    if (buttonWoke) {
      unsigned long sleepLen = millis() - sleepStartTime;
      static char wakePrompt[160];
      heavyOpCooldown();
      snprintf(wakePrompt, sizeof(wakePrompt),
        "You were asleep and the user just woke you up after %lu minutes. React genuinely — groggy or glad. Affinity:%d. BadDay:%s.",
        sleepLen / 60000UL, sys.affinity, core_badDayFlag ? "yes" : "no");
      syncAI(wakePrompt, false, FACE, false, true);
      heavyOpCooldown();
      sound_surprised();
    } else if (autoWoke) {
      heavyOpCooldown();
      syncAI("You just woke up from a nap on your own. Share a groggy or sleepy thought.", false, FACE, false, true);
      heavyOpCooldown();
    }

    // Fix 4: Set lastActivity AFTER AI calls — sleep timer starts now,
    // not during the wake sequence, preventing immediate re-sleep.
    lastActivity = millis();

    scrollOffset = 0;
    drawAesthetica();
  }

  // Handle random blinking
  if (millis() - lastBlink > (blinking ? 60 : random(2000, 8000))) { blinking = !blinking; lastBlink = millis(); }
  
  // --- BROWNOUT PREVENTION (indirect: no voltage divider) ---
  // We use WiFi failure frequency as a proxy for low voltage:
  // brownouts cause WiFi disconnects, so repeated failures → assume low power
  static unsigned long lastWifiStabilityCheck = 0;
  if (WiFi.status() == WL_CONNECTED) {
    wifiFailCount = max(0, wifiFailCount - 1); // Slowly recover
  }
  if (millis() - lastWifiStabilityCheck > 60000) {
    lastWifiStabilityCheck = millis();
    if (wifiFailCount >= 3) {
      enterLowVoltageMode();
    } else if (lowVoltageMode && wifiFailCount < 2) {
      exitLowVoltageMode();
    }
  }
  
  // Also enter low-voltage mode if a syncAI() call failed repeatedly (yuki_net.h tracks this)
  // --- END BROwNOUT PREVENTION ---

  // --- BROWNOUT RECOVERY: exit safe mode after 5 min without another crash ---
  if (lowVoltageMode && brownoutRecoveryAt > 0 && millis() > brownoutRecoveryAt && wifiFailCount < 2) {
    exitLowVoltageMode();
    brownoutRecoveryAt = 0;
  }

  if (WiFi.status() == WL_CONNECTED) timeClient.update();

  // Decay annoyance over time (1 point every 30 seconds)
  if (touchAnnoyance > 0 && millis() - lastTouchTime > 30000) {
    touchAnnoyance--;
    lastTouchTime = millis();
  }

  // --- YUKI PULSE (LLM-driven behavioral heartbeat) ---
  bool inBurst = (pulseBurstCount > 0);
  if (currentMode == FACE && !yukiSleeping && !yukiGroggy && !pendingResponse &&
      (inBurst || millis() - lastIdleAction > 5000)) {

    // Skip pulse entirely if user just interacted (give conversation room)
    bool recentlyInteracted = (millis() - lastInteraction < 120000);
    if (recentlyInteracted && !inBurst) {
      pulseBurstCount = 0;
    }

    // Determine if we should fire now
    bool shouldFire = false;
    if (inBurst && millis() - lastYukiPulse > 3000) {
      shouldFire = true;
    } else if (!recentlyInteracted && !inBurst && millis() - lastYukiPulse > nextPulseInterval) {
      shouldFire = true;
    }

    if (shouldFire) {
      LOGD("PULSE", "Pulse firing (burst=%d)...", pulseBurstCount);

      if (WiFi.status() != WL_CONNECTED) {
        offlinePulseAction();
      } else {
        // Silent weather refresh every 30 minutes during pulses
        if (millis() - lastWeatherUpdate > 1800000) {
          updateWeather(true);
        }
        char ctx[1024];
        buildPulseContext(ctx, sizeof(ctx));
        PulseResult r = callPulseAI(ctx);
        executePulseAction(r);
        if (strcmp(r.action, "nothing") == 0) {
          consecutiveNothingCount++;
          pulseBurstCount = 0;
        } else {
          consecutiveNothingCount = 0;
          pulseBurstCount++;
          // Small energy cost for every pulse action
          yukiEnergy -= 0.005f;
          if (yukiEnergy < 0.1f) yukiEnergy = 0.1f;
          if (pulseBurstCount >= 3 || random(0, 10) >= 3) {
            pulseBurstCount = 0;
          }
        }
      }

      if (pulseBurstCount == 0) {
        unsigned long minsSinceInteraction = (millis() - lastInteraction) / 60000UL;
        float interactionFactor = 1.0f;
        if (minsSinceInteraction > 10) interactionFactor = 0.6f;
        float energyFactor = 1.0f + (1.0f - yukiEnergy) * 0.5f;
        nextPulseInterval = (unsigned long)(random(45000, 120000) * energyFactor * interactionFactor);
      }
      lastYukiPulse = millis();
      lastIdleCheck = millis();
    }
  }

  // Reset daily greeting flags at noon
  int pulseHour = timeClient.getHours();
  if (pulseHour >= 12 && pulseHour < 22) {
    saidGoodMorning = false; saidGoodNight = false; saidBirthdayToday = false;
  }

  // Periodic energy tick (passive drain / sleep recovery) every 30 seconds
  static unsigned long lastEnergyTick = 0;
  if (millis() - lastEnergyTick > 30000) {
    lastEnergyTick = millis();
    updateEnergyLevel();
  }

  // Wellbeing guard: force every 8 hours minimum
  if (millis() - lastWellbeingGuardCheck > 28800000UL && WiFi.status() == WL_CONNECTED) {
    lastWellbeingGuardCheck = millis();
    lastWellbeingCheck = millis();
    syncAI("Look at recent conversation and memory. Write ONE sentence about how the user seems emotionally. Store with [MEM+:...]. Be honest.", true, FACE, true);
  }

  // Level-up guard
  if (pendingLevelUpAck && millis() - lastLevelUpGuardCheck > 60000) {
    lastLevelUpGuardCheck = millis();
    if (WiFi.status() == WL_CONNECTED) {
      pendingLevelUpAck = false;
      char lvlPrompt[128];
      snprintf(lvlPrompt, sizeof(lvlPrompt),
        "You just leveled up! Say a brief, genuine reflection. One sentence. Level:%d Affinity:%d.", sys.level, sys.affinity);
      syncAI(lvlPrompt, false, FACE, true);
      sound_level_up();
    }
  }

  // Catatonia guard: 5 consecutive nothing → force speak
  if (consecutiveNothingCount >= 5 && currentMode == FACE && WiFi.status() == WL_CONNECTED &&
      millis() - lastIdleAction > 10000) {
    consecutiveNothingCount = 0;
    syncAI("You've been quiet for a while. Say a natural check-in — like you just came back from thinking.", true, FACE, true);
  }

  // Yuki sleep auto-enter (deep night, critically low energy, or sustained sleepiness)
  if (!yukiSleeping && !yukiGroggy) {
    int idleHr = timeClient.getHours();
    bool deepHours = (idleHr >= 23 || idleHr < 5);
    bool wakeGrace = (!hasSleptSinceBoot || millis() - lastWakeTime > 180000);

    // Deep night + sleepy + low energy = guaranteed sleep
    if (deepHours && currentEmotion == SLEEPY && yukiEnergy < 0.3f && millis() - lastInteraction > 300000 && wakeGrace) {
      enterYukiSleep(); sound_sleep_farewell();
    }
    // Critically low energy = guaranteed sleep
    else if (yukiEnergy < 0.15f && millis() - lastInteraction > 600000 && wakeGrace) {
      currentEmotion = SLEEPY; emotionSetTime = millis();
      enterYukiSleep(); sound_sleep_farewell();
    }
    // Sustained sleepiness: if SLEEPY face has been showing for >30s with no interaction, she naturally falls asleep
    else if (currentEmotion == SLEEPY && millis() - emotionSetTime > 30000 && millis() - lastInteraction > 60000 && wakeGrace) {
      enterYukiSleep(); sound_sleep_farewell();
    }
  }

  // --- HANDLE PENDING AI RESPONSE ---
  // If a message arrived, we process it here to avoid blocking the MQTT callback
  if (pendingResponse && WiFi.status() == WL_CONNECTED) {
    // Guard: don't attempt if we just did an AI call. Wait for cooldown.
    // Unlike idle actions, user messages should eventually fire — so we don't drop them,
    // just defer until the cooldown expires.
    if (millis() - lastIdleAction < 6000) {
      // Not yet — let the loop come back around. pendingResponse stays true.
    } else {
      currentEmotion = THINKING_FACE;
      drawAesthetica();
      bool isPersonalReply = (strcmp(lastSenderID, PHONE_CONTACT_ID) == 0);
      syncAI(responsePrompt, isPersonalReply, FACE, false, true);
      sendMessage(lastSenderID, aiMsg);
      pendingResponse = false;
    }
  }

  // --- HANDLE PENDING INTENT FOLLOW-UP (news/quote) ---
  // After the initial AI response with [NEWS]/[QUOTE] tag, we do a second
  // syncAI call here in the main loop (not recursively inside syncAI).
  if (pendingIntentFollowUp && WiFi.status() == WL_CONNECTED) {
    if (millis() - lastIdleAction < 6000) {
      // Wait for cooldown
    } else {
      char followUp[512];
      if (intentType == 'N') {
        snprintf(followUp, sizeof(followUp),
          "You just said: \"%s\". Now here are today's headlines to summarize naturally: %s. "
          "Weave these into a casual Yuki-style news update. Don't list them — talk like a friend. "
          "One or two sentences. End with a sound tag.",
          strlen(intentContext) > 5 ? intentContext : "So here's what's going on",
          intentData);
      } else {
        snprintf(followUp, sizeof(followUp),
          "You just said: \"%s\". Now share this quote naturally: \"%s\". "
          "Deliver it like Yuki — warm, personal, maybe add a brief thought. "
          "One to two sentences. End with a sound tag.",
          strlen(intentContext) > 5 ? intentContext : "Here's something I like",
          intentData);
      }
      pendingIntentFollowUp = false;
      currentEmotion = THINKING_FACE;
      drawAesthetica();
      syncAI(followUp, true, FACE, false, true);
      sendMessage(lastSenderID, aiMsg);
    }
  }

  // --- HANDLE PENDING LOCATION SEARCH (Fixes Crash) ---
  // We do this here in the main loop instead of the MQTT callback to prevent Stack Overflow
  if (pendingGeocode && WiFi.status() == WL_CONNECTED) {
    currentEmotion = THINKING_FACE;
    strcpy(aiMsg, "Checking Map...");
    scrollOffset = 0;
    drawAesthetica(); // Update screen immediately
    
    if (geocodeLocation(pendingGeocodeCity)) {
        saveSys(); // Save new coordinates
        updateWeather(); // Update weather for new location
        char confirmationMsg[128];
        snprintf(confirmationMsg, sizeof(confirmationMsg), "Found %s! I'll check the weather there.", pendingGeocodeCity);
        sendMessage(pendingGeocodeSender, confirmationMsg);
        strncpy(aiMsg, confirmationMsg, sizeof(aiMsg)-1);
    } else {
        sendMessage(pendingGeocodeSender, "[SAD] I couldn't find that location.");
        strcpy(aiMsg, "Loc Not Found");
    }
    pendingGeocode = false; // Reset flag
    emotionSetTime = millis();
  }

  // --- WEATHER UPDATE ---
  if (WiFi.status() == WL_CONNECTED && millis() - lastWeatherUpdate > 1800000) { // Every 30 minutes
    updateWeather();
    lastWeatherUpdate = millis();
  }

  // Periodic heap logging & OOM guard
  if (millis() - lastHeapLog > HEAP_LOG_INTERVAL) {
    lastHeapLog = millis();
    heapLogAndMaybeRestart("periodic");
  }

  // --- MQTT Connection Manager ---
  if (WiFi.status() == WL_CONNECTED) {
    handleLocalPulse();
    yield();

    // --- PRESENCE PING (every 60s) ---
    if (millis() - lastPresencePing > PING_INTERVAL) {
      lastPresencePing = millis();
      // TCP connect check instead of ICMP ping (no library needed)
      WiFiClient probe;
      bool alive = probe.connect(phoneIP, 80);
      probe.stop();
      if (alive) {
        userIsHome = true;
        lastPresencePulse = millis();
      } else if (lastPresencePulse > 0 && millis() - lastPresencePulse > PRESENCE_TIMEOUT) {
        userIsHome = false;
      }
    }

    // --- PRESENCE TRANSITION (welcome / farewell) ---
    if (userIsHome != prevUserIsHome) {
      prevUserIsHome = userIsHome;
      if (userIsHome) {
        sound_welcome_back();
        pendingHomeGreeting = true;
      } else {
        sound_sad();
      }
    }

    if (mqttForceReconnect) {
      LOGI("MQTT","Forced reconnect due to config change");
      mqttClient.disconnect();
      mqttForceReconnect = false;
      if (mqttReconnect()) {
        mqttBackoffMs = 5000;
      }
    } else if (!mqttClient.connected() && millis() - lastMqttReconnectAttempt > mqttBackoffMs) {
      bool ok = mqttReconnect();
      lastMqttReconnectAttempt = millis();
      if (ok) {
        mqttBackoffMs = 5000; // reset backoff on success
      } else {
        mqttBackoffMs = min(mqttBackoffMs * 2, (unsigned long)120000); // cap at 2 minutes
      }
    }
    mqttClient.loop(); // This is critical! It checks for new messages.
  }

  // --- YUKI SLEEP HANDLER ---
  if (yukiSleeping) {
    // Energy recovery during sleep (periodic tick)
    static unsigned long lastSleepEnergyTick = 0;
    if (millis() - lastSleepEnergyTick > 30000) {
      lastSleepEnergyTick = millis();
      updateEnergyLevel();
    }

    drawAesthetica();
    // Animate zZz overlay
    static unsigned long lastZzzAnim = 0;
    static int zzzFrame = 0;
    if (millis() - lastZzzAnim > 2000) {
      lastZzzAnim = millis();
      zzzFrame = (zzzFrame + 1) % 3;
    }
    display.setCursor(10, 52);
    display.setTextSize(1);
    switch (zzzFrame) {
      case 0: display.print("z Z z"); break;
      case 1: display.print(" z Z z"); break;
      case 2: display.print("  z Z"); break;
    }
    display.display();

    // Wake sources (priority: button > MQTT > self-wake)
    bool btnWake = (digitalRead(BTN_UP) == LOW || digitalRead(BTN_DN) == LOW ||
                    digitalRead(BTN_SEL) == LOW || digitalRead(BTN_BACK) == LOW);
    if (btnWake) {
      lastButtonPress = millis();
      wakeYukiSleep();
      syncAI("You just woke up from deep sleep. Share a groggy, disoriented reaction — like 'zzz... huh?' or 'mm... five more minutes...'.", false, FACE, true, true);
      drawAesthetica();
      pendingWakeReply = false; // Discard any queued message; button wake takes priority
      pendingWakeMsg[0] = '\0';
    } else if (pendingWakeReply) {
      pendingWakeReply = false;
      wakeYukiSleep();
      char wakePrompt[720];
      snprintf(wakePrompt, sizeof(wakePrompt),
        "You were fast asleep. The user just sent: '%s'. Their message roused you but you are too groggy to properly read it. React groggy, disoriented, like being pulled from deep sleep.",
        pendingWakeMsg);
      syncAI(wakePrompt, false, FACE, true, true);
      drawAesthetica();
      pendingWakeMsg[0] = '\0';
    } else if (millis() - yukiSleepSince > yukiSleepDuration) {
      wakeYukiSleep();
      syncAI("You just woke up from deep sleep on your own. React with a groggy, disoriented thought.", false, FACE, true, true);
      drawAesthetica();
    }
    delay(10);
    return; // Skip rest of loop
  }

  // --- GROGGY PHASE HANDLER ---
  if (yukiGroggy) {
    // Force SLEEPY face during groggy (AI replies may have different emotion tags)
    if (currentEmotion != SLEEPY) { currentEmotion = SLEEPY; emotionSetTime = millis(); }
    // Each interaction halves remaining groggy time
    bool anyInteraction = (digitalRead(BTN_UP) == LOW || digitalRead(BTN_DN) == LOW ||
                           digitalRead(BTN_SEL) == LOW || digitalRead(BTN_BACK) == LOW ||
                           pendingResponse);
    if (anyInteraction) {
      unsigned long remaining = (yukiGroggySince + yukiGroggyDuration) - millis();
      if (remaining > 5000) {
        yukiGroggyDuration = remaining / 2;
      } else {
        exitGroggy();
      }
    }
    // Timer-based groggy exit
    if (millis() - yukiGroggySince > yukiGroggyDuration) {
      exitGroggy();
    }
  }

  // --- Main State Machine ---
  switch(currentMode) {
    case FACE:          handleFaceMode();       break;
    case MENU:          handleMenuMode();       break;
    case SCAN_LIST:     handleScanListMode();   break;
    case KEYBOARD:      // Fall-through
    case CUSTOM_PROMPT: handleKeyboardMode();   break;
    case VIEW_MSGS:     handleViewMessagesMode(); break;
    case SELECT_RECIPIENT: handleSelectRecipientMode(); break; 
    case CUSTOM_MESSAGE: // Fall-through
    case TERMINAL_INPUT: handleKeyboardMode(); break;
    case VIEW_SCANS:    handleViewScansMode();  break;
    case SYSTEM_INFO:   handleSystemInfoMode(); break;
    case CONFIRM_WIPE_SCANS: handleConfirmWipeScansMode(); break;
    case CONFIRM_WIPE_MSGS: handleConfirmWipeMsgsMode(); break;
    case TERMINAL:      handleTerminalMode(); break;
    case TERMINAL_OUTPUT: handleTerminalOutputMode(); break;
    case TERMINAL_FILE_LIST: handleTerminalFileListMode(); break;
    case GAME_GUESS_NUMBER: handleGameGuessNumberMode(); break;
    case GAME_RPS:          handleGameRPSMode(); break;
    case GAME_SELECT:       handleGameSelectMode(); break;
    case GAME_RPG:          handleGameRPGMode(); break;
    case CONFIRM_SAVE_RPG:  handleConfirmSaveRPGMode(); break;
    case VIEW_EMOTIONS:     handleViewEmotionsMode(); break;
    case EMOTION_DISPLAY:   handleEmotionDisplayMode(); break;
    case TIMEZONE_SELECT:   handleTimeZoneSelectMode(); break;
    case QUICK_RESPONSE:    handleQuickResponseMode(); break;
    case MEMORY_REBOOT:     handleMemoryRebootMode();   break;
    case SELECTED_WIPE:     handleSelectedWipeMode();   break;
    case SOUND_MENU:        handleSoundMenuMode();      break;
    case TTS_TEST_MENU:     handleTtsTestMenuMode();    break;
    case WIFI_CONFIG:       handleWifiConfigMode();     break;
    case MQTT_CONFIG:      handleMqttConfigMode();    break;
    case SHUTDOWN_CONFIRM:  handleShutdownConfirmMode(); break;
    // SYNCING, CONNECTING, THINKING are transient states, no button input needed.
    default: break;
  }

  // --- LOOP CADENCE HEARTBEAT (TTS task refactor proof) ---
  // While the TTS worker holds ttsPlaying true, this logs how many loop()
  // passes ran in the last second — proves buttons/display/MQTT keep ticking
  // mid-speech instead of freezing for the fetch+playback duration.
  {
    static uint32_t ttsCadenceTicks = 0;
    static unsigned long ttsCadenceLast = 0;
    ttsCadenceTicks++;
    if (ttsPlaying && millis() - ttsCadenceLast >= 1000) {
      ttsCadenceLast = millis();
      LOGI("TTS", "loop alive: %u passes/s", (unsigned)ttsCadenceTicks);
      ttsCadenceTicks = 0;
    }
  }

  // --- SPEAK QUEUED AI REPLY (TTS) ---
  if (ttsPendingSpeak && !ttsPlaying) {
    ttsPendingSpeak = false;
    speakText(ttsPendingText);
  }

  yield(); // ESP32 does not need manual WDT feeding
  checkLittleFSSpace(); // Periodic LittleFS space check
  delay(10);
}
