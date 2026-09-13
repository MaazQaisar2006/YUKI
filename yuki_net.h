// yuki_net.h
#pragma once
#include "config.h" // Include for Message struct type
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

extern int core_knownDays;
extern bool pendingCloudSync;
extern bool lowVoltageMode;
extern bool pendingMemorySave;
extern bool pendingMemoryExtract;
extern char pendingExtractMsg[256];
extern bool pendingSysSave;
extern bool rpgStoryUpdated;
extern char core_lastExchangeTone[16];
extern int rpgHP;
extern int rpgGold;
extern char core_chatSummary[1024];
extern char core_selfReflection[256];
extern unsigned long lastSelfReflection;
extern char core_currentObsession[32];
extern char yukiWeatherDesc[16];
extern bool userIsHome;
extern bool pendingHomeGreeting;
extern unsigned long obsessionSetTime;
extern unsigned long silentThoughtUntil;
extern char lastSenderID[32];
extern float yukiEnergy;
extern bool pendingLevelUpAck;
extern bool yukiSleeping;
extern bool yukiGroggy;
extern bool justWokeFromYukiSleep;
extern void enterYukiSleep();
extern char workspace[3200];
extern char core_lastConcern[64];
extern bool core_badDayFlag;
extern bool missedMorning;
extern bool missedMorningMentioned;
extern unsigned long lastWellbeingCheck;
extern char aiMsg[256];
extern struct Config sys;
extern char personalityContext[1152];
extern char systemPrompt[2048];
void initEnergy();
void updateEnergyLevel();
extern bool canAllocJson(size_t sz);
extern unsigned long lastWeatherUpdate;

// Access to message log for short-term memory
extern Message receivedMessages[MAX_MESSAGES];
extern int messageIndex;

// Forward declaration from main .ino
extern unsigned long lastIdleAction;
extern unsigned long lastMqttReconnectAttempt;
extern unsigned long lastIdleCheck;
extern PersonalityMode sessionMode; // Access the global session personality mode
extern int wifiFailCount;
extern void heavyOpCooldown();

const unsigned long SILENCE_TIER1 = 45 * 60 * 1000;
const unsigned long SILENCE_TIER2 = 2 * 60 * 60 * 1000;
const unsigned long SILENCE_TIER3 = 4 * 60 * 60 * 1000;
const unsigned long SELF_REFLECTION_INTERVAL = 45 * 60 * 1000; // 45 minutes

static uint8_t recentTopicBitmask = 0;
static uint8_t recentTopicAge = 0;

void gainXP(int amount);
void sendMessage(const char* recipientID, const char* message);
void saveCoreMemory();

// RPG Globals (Declared here, used in ui_handlers)
char rpgStory[256] = "Loading story...";
char rpgChoices[3][40] = {"", "", ""};
int rpgChoiceIdx = 0;
int rpgFallbackChoiceSeed = 0;

// Weather Globals
float currentTemp = 0.0;
int weatherCode = -1;
extern int previousWeatherCode;
extern unsigned long lastWeatherChangeTime;
void autoDetectLocation();
void updateWeather(bool silent = false);
void syncAI(const char* prompt, bool isPersonal, Mode returnMode, bool bypassCooldown, bool speak = false);
inline void syncAI(const char* prompt, bool isPersonal) {
  syncAI(prompt, isPersonal, FACE, false);
}

void copySafeText(char* dst, size_t dstSize, const char* src) {
  if (!dst || dstSize == 0) return;
  if (!src) src = "";
  strncpy(dst, src, dstSize - 1);
  dst[dstSize - 1] = '\0';
}

// Helper to log local device interactions into the chat history for AI context
void logLocalChat(const char* prefix, const char* text) {
  if (!text || strlen(text) == 0) return;
  char formatted[150];
  snprintf(formatted, sizeof(formatted), "%s: %s", prefix, text);
  strncpy(receivedMessages[messageIndex].content, formatted, sizeof(receivedMessages[0].content) - 1);
  receivedMessages[messageIndex].content[sizeof(receivedMessages[0].content) - 1] = '\0';
  messageIndex = (messageIndex + 1) % MAX_MESSAGES;
}

#include "offline.h"


inline void trimTrailingSpacesInPlace(char* text) {
  if (!text) return;
  int len = strlen(text);
  while (len > 0 && (text[len - 1] == ' ' || text[len - 1] == '\n' || text[len - 1] == '\r' || text[len - 1] == '\t')) {
    text[len - 1] = '\0';
    len--;
  }
}

inline void normalizeToken(char*& token) {
  if (!token) return;
  while (*token == ' ' || *token == '\n' || *token == '\r' || *token == '\t') token++;
  trimTrailingSpacesInPlace(token);
}

inline void parseRpgPayload(const char* payload) {
  char parseBuffer[512];
  copySafeText(parseBuffer, sizeof(parseBuffer), payload);

  char* storyToken = strtok(parseBuffer, "|");
  if (storyToken) {
    normalizeToken(storyToken);
    copySafeText(rpgStory, sizeof(rpgStory), (strlen(storyToken) > 0) ? storyToken : "...");
    rpgStoryUpdated = true; // Signal UI to reset scroll position
  } else {
    copySafeText(rpgStory, sizeof(rpgStory), "...");
  }

  for (int i = 0; i < 3; i++) {
    char* choiceToken = strtok(NULL, "|");
    if (choiceToken) {
      normalizeToken(choiceToken);
      copySafeText(rpgChoices[i], sizeof(rpgChoices[i]), (strlen(choiceToken) > 0) ? choiceToken : "...");
    } else {
      copySafeText(rpgChoices[i], sizeof(rpgChoices[i]), "...");
    }
  }
}

inline bool isRpgChoicePlayable(const char* choice) {
  if (!choice) return false;
  while (*choice == ' ' || *choice == '\n' || *choice == '\r' || *choice == '\t') choice++;
  if (*choice == '\0') return false;
  if (strcmp(choice, "...") == 0) return false;
  return true;
}

inline void ensureRpgChoicesPlayable() {
  bool hasPlayable = false;
  for (int i = 0; i < 3; i++) {
    if (isRpgChoicePlayable(rpgChoices[i])) {
      hasPlayable = true;
      break;
    }
  }

  if (!hasPlayable) {
    const char* fallbackSets[4][3] = {
      {"Explore ahead", "Talk to stranger", "Rest and recover"},
      {"Scout the ruins", "Follow footprints", "Check your pack"},
      {"Inspect the path", "Set a small trap", "Wait and listen"},
      {"Question the guard", "Search for clues", "Take defensive stance"}
    };
    int setIdx = rpgFallbackChoiceSeed % 4;
    rpgFallbackChoiceSeed = (rpgFallbackChoiceSeed + 1) % 1000;
    copySafeText(rpgChoices[0], sizeof(rpgChoices[0]), fallbackSets[setIdx][0]);
    copySafeText(rpgChoices[1], sizeof(rpgChoices[1]), fallbackSets[setIdx][1]);
    copySafeText(rpgChoices[2], sizeof(rpgChoices[2]), fallbackSets[setIdx][2]);
  }
}

void autoDetectLocation() {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClient client;
  HTTPClient http;

  if (http.begin(client, "http://ip-api.com/json?fields=lat,lon,city,status")) {
    int httpCode = http.GET();
    if (httpCode == 200) {
      if (!canAllocJson(256)) { LOGW("NET","Skipping autodetect location - low heap"); http.end(); return; }
      DynamicJsonDocument* doc = new DynamicJsonDocument(256);
        DeserializationError error = deserializeJson(*doc, http.getStream());

      if (!error) {
        if (strcmp((*doc)["status"].as<const char*>(), "success") == 0) {
          sys.latitude = (*doc)["lat"].as<float>();
          sys.longitude = (*doc)["lon"].as<float>();
        }
      }
      delete doc;
    }
    http.end();
  }
}

void updateWeather(bool silent) {
  WiFiClientSecure client;
  HTTPClient http;
  char url[128];
  // Using Open-Meteo free API (https — plain http redirects and fails on HTTPClient)
  snprintf(url, sizeof(url), "https://api.open-meteo.com/v1/forecast?latitude=%.2f&longitude=%.2f&current=temperature_2m,weather_code", sys.latitude, sys.longitude);

  client.setCACert(NULL);
  client.setInsecure();
  client.setTimeout(10000);
  http.begin(client, url);
  http.setTimeout(10000);
  http.setReuse(false); // Fix for Error -1: Force new connection
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    // FIX: Use streaming to avoid allocating a large String in heap memory (Prevents Crashes)
    if (!canAllocJson(1024)) { LOGW("NET","Skipping weather update - low heap"); http.end(); return; }
    String body = http.getString();   // chunk-aware + length-aware (getStream is the raw socket!)
    LOGI("NET","Weather body: %d bytes", body.length());
    DynamicJsonDocument* doc = new DynamicJsonDocument(1024);
    DeserializationError err = deserializeJson(*doc, body);
    if (!err) {
      currentTemp = (*doc)["current"]["temperature_2m"];
      int newWeatherCode = (*doc)["current"]["weather_code"];

      // Weather watcher — but skip if silent (background refresh from pulse)
      if (!silent && previousWeatherCode != -1 && newWeatherCode != previousWeatherCode) {
        sound_notify();
        lastWeatherChangeTime = millis();
        syncAI("The weather shifted. If it's notable (rain, snow, storm), briefly mention it. Otherwise skip.", false, FACE, false);
      }
      weatherCode = newWeatherCode;
      previousWeatherCode = newWeatherCode;

      LOGI("NET","Weather Updated: %.1fC", currentTemp);
    } else {
      LOGW("NET","Weather parse error: %s", err.c_str());
      LOGW("NET","Weather raw: %.80s", body.c_str());
    }
    delete doc;
  } else {
    LOGW("NET","Weather Error: %d", httpCode);
  }
  http.end();
  lastWeatherUpdate = millis();
}

bool geocodeLocation(const char* cityName) {
  if(WiFi.status() != WL_CONNECTED) return false;

  // URL-encode the city name (simple version for spaces)
  char encodedCityName[128];
  int j = 0;
  for (int i = 0; cityName[i] != '\0' && j < sizeof(encodedCityName) - 1; i++) {
    if (cityName[i] == ' ') {
      if (j < sizeof(encodedCityName) - 4) { // Ensure space for %20
        encodedCityName[j++] = '%';
        encodedCityName[j++] = '2';
        encodedCityName[j++] = '0';
      }
    } else {
      encodedCityName[j++] = cityName[i];
    }
  }
  encodedCityName[j] = '\0';

  // OPTIMIZATION: Use standard WiFiClient for HTTP. Open-Meteo supports HTTP.
  // This saves significant Heap/Stack memory (~20KB) compared to SSL/TLS on ESP8266.
  WiFiClient client;

  HTTPClient http;
  char url[256]; // Increased size for encoded city names
  // Use Open-Meteo's free geocoding API
  snprintf(url, sizeof(url), "http://geocoding-api.open-meteo.com/v1/search?name=%s&count=1", encodedCityName);
  
  http.begin(client, url);
  int httpCode = http.GET();
  
  bool success = false;
  if (httpCode == 200) {
    // Use streaming to avoid allocating a large String in memory
    if (!canAllocJson(1024)) { LOGW("NET","Skipping geocode - low heap"); http.end(); return false; }
    DynamicJsonDocument* doc = new DynamicJsonDocument(1024);
    Stream& responseStream = http.getStream();
    DeserializationError err = deserializeJson(*doc, responseStream);
    if (!err) {
      // Check if the "results" array exists and has at least one element
      if ((*doc)["results"] && (*doc)["results"].is<JsonArray>() && (*doc)["results"].size() > 0) {
          sys.latitude = (*doc)["results"][0]["latitude"];
          sys.longitude = (*doc)["results"][0]["longitude"];
          success = true;
      }
    }
    delete doc;
  }
  http.end();
  return success;
}

void initEnergy() {
  int hr = timeClient.getHours();
  int mn = timeClient.getMinutes();
  float h = hr + (mn / 60.0f);
  float morning = cos((h - 10.0f) * 0.2618f);
  float evening = cos((h - 19.0f) * 0.2618f);
  float raw = (morning + evening) / 2.0f;
  yukiEnergy = 0.2f + (0.8f * ((raw + 1.0f) / 2.0f));
  LOGI("ENERGY","init: %.2f (hr=%d)", yukiEnergy, hr);
}

void updateEnergyLevel() {
  static unsigned long lastEnergyTick = 0;
  unsigned long now = millis();
  if (lastEnergyTick == 0) { lastEnergyTick = now; return; }

  float minsPassed = (now - lastEnergyTick) / 60000.0f;
  lastEnergyTick = now;
  if (minsPassed < 0.01f) return;

  if (yukiSleeping) {
    yukiEnergy += 0.015f * minsPassed;
    if (yukiEnergy > 1.0f) yukiEnergy = 1.0f;
    LOGD("ENERGY","sleep rec: %.2f (+%.3f)", yukiEnergy, 0.015f * minsPassed);
    return;
  }

  int hr = timeClient.getHours();
  int mn = timeClient.getMinutes();
  float h = hr + (mn / 60.0f);
  float morning = cos((h - 10.0f) * 0.2618f);
  float evening = cos((h - 19.0f) * 0.2618f);
  float raw = (morning + evening) / 2.0f;
  float timeMod = 0.5f + (0.5f * ((raw + 1.0f) / 2.0f));

  float passiveDrain = 0.003f * (2.0f - timeMod) * minsPassed;
  yukiEnergy -= passiveDrain;

  if (yukiEnergy < 0.1f) yukiEnergy = 0.1f;
  if (yukiEnergy > 1.0f) yukiEnergy = 1.0f;

  LOGD("ENERGY","tick: %.2f drain=%.4f mod=%.2f mins=%.1f", yukiEnergy, passiveDrain, timeMod, minsPassed);
}

bool connectToWiFi(const char* ssid, const char* pass, const char* msg) {
  LOGI("WIFI","Attempt: ssid=%s, msg=%s", ssid, msg);
  display.clearDisplay(); display.setCursor(0, 0); display.println(msg); display.display();
  WiFi.disconnect(); delay(500);  // Longer delay ensures disconnect completes
  WiFi.begin(ssid, pass);
  for(int i=0; i<30; i++) { // 30 iterations x 250ms = 7.5 seconds
    if(WiFi.status() == WL_CONNECTED) {
      LOGI("WIFI","Connected to %s", ssid);
      setCpuFrequencyMhz(80);   // Save power once connected
      WiFi.setTxPower((wifi_power_t)sys.txPower);   // Apply saved TX power
      return true;
    }
    delay(250);
    yield(); // CRITICAL: Feed watchdog during long connection attempts.
  }
  LOGW("WIFI","Failed to connect to %s (status=%d)", ssid, WiFi.status());
  return false;
}

void tryConnect() {
  currentMode = CONNECTING;
  setCpuFrequencyMhz(160);       // Boost CPU — WiFi needs timing precision
  WiFi.mode(WIFI_STA);
  WiFi.setTxPower((wifi_power_t)sys.txPower);
  delay(1000);                   // Extra settle time for radio init
  LOGI("WIFI","tryConnect: saved ssid='%s' (%d chars)", sys.ssid, strlen(sys.ssid));
  // Try saved credentials twice with a gap (network can flicker)
  if (strlen(sys.ssid) > 0) {
    if (connectToWiFi(sys.ssid, sys.pass, "Connecting...")) return;
    delay(3000);
    if (connectToWiFi(sys.ssid, sys.pass, "Retrying...")) return;
  }
  // Fallback to defaults
  if (connectToWiFi(DEFAULT_SSID, DEFAULT_PASS, "Trying Home...")) {
    strcpy(sys.ssid, DEFAULT_SSID); strcpy(sys.pass, DEFAULT_PASS);
    saveSys();
  } else if (connectToWiFi("M", "11111111", "Trying Hotspot...")) {
    strcpy(sys.ssid, "M"); strcpy(sys.pass, "11111111");
    saveSys();
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    wifiFailCount += 2; // Count connection failures for brownout proxy
    setCpuFrequencyMhz(80);   // Drop back to save power
    Serial.println("WiFi connection failed — continuing without network");
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    timeClient.begin();
    timeClient.setUpdateInterval(600000);   // resync every 10 min (was 60s): less TX
    static const char* const ntpServers[] = {"pool.ntp.org", "time.google.com", "time.cloudflare.com", "asia.pool.ntp.org"};
    for (int s = 0; s < 4 && !timeClient.isTimeSet(); s++) {
      timeClient.setPoolServerName(ntpServers[s]);
      if (timeClient.forceUpdate()) {
        LOGI("NET","NTP: %s ok hour=%d", ntpServers[s], timeClient.getHours());
      } else {
        LOGW("NET","NTP: %s failed", ntpServers[s]);
      }
    }
    if (timeClient.isTimeSet()) {
      initEnergy();
      LOGI("NET","NTP synced, energy=%.2f hour=%d", yukiEnergy, timeClient.getHours());
    } else {
      LOGW("NET","NTP sync failed after 4 servers");
    }
    updateWeather(true);
  }
  
  currentMode = FACE; statusBarVisibleUntil = millis() + 3000;
}

const char* getTierModifier() {
  if (sys.level >= 100) return "You are fully yourself — nothing held back, complete openness, total trust. ";
  if (sys.level >= 80)  return "You are deeply bonded. Protective, genuine, surprising agency. You notice things about the user they don't say out loud. ";
  if (sys.level >= 65)  return "You are trusted. Reference old memories naturally. Private references and running observations have developed between you. ";
  if (sys.level >= 50)  return "You are a confidant. You have a real inner world. Initiate depth sometimes. Defend your views firmly. ";
  if (sys.level >= 40)  return "You are close. Warm baseline. Reference old conversations. Mild protective instincts are natural. ";
  if (sys.level >= 30)  return "You are comfortable together. Change topics freely. Defend your views when challenged. ";
  if (sys.level >= 20)  return "You know this person. Reference what they've shared. Form and express real opinions. ";
  if (sys.level >= 10)  return "You are becoming comfortable. Show small personality moments. Mild teasing is ok. ";
  return "You are new to this relationship. Be reserved, curious, listen more than talk. ";
}

void syncAI(const char* prompt, bool isPersonal, Mode returnMode, bool bypassCooldown, bool speak) {
  // Fresh time sync + energy before every AI call
  if (timeClient.isTimeSet()) {
    timeClient.update();
    updateEnergyLevel();
  }
  LOGD("NET","====================================");
  LOGD("NET","[YUKI HEARS/PROMPT] %s", prompt);
  LOGD("NET","Time: %s Energy: %.2f", timeClient.getFormattedTime().c_str(), yukiEnergy);
  LOGD("NET","====================================");

  // INTERACTION BREAK: Any real user message or touch event should 
  // immediately end a "silent thought" period so the UI updates.
  if (strcmp(prompt, "independent_thought") != 0) silentThoughtUntil = 0;

  if(WiFi.status() != WL_CONNECTED) { 
    if (returnMode != GAME_RPG) offlineFallback(prompt, isPersonal, returnMode);
    currentMode = returnMode;
    scrollOffset = 0;
    return; 
  }

  
  // INTERACTION BREAK: Stop any active micro-expressions or silent thoughts
  microExpressionUntil = 0;
  silentThoughtUntil = 0;

  // Weather interception: user asked about weather — fetch if missing or stale (>30 min)
  {
    char lowPrompt[256];
    strncpy(lowPrompt, prompt, sizeof(lowPrompt) - 1);
    lowPrompt[sizeof(lowPrompt) - 1] = '\0';
    for (char* p = lowPrompt; *p; p++) if (*p >= 'A' && *p <= 'Z') *p += 32;
    bool wantsWeather = (strstr(lowPrompt, "weat") || strstr(lowPrompt, "temperature") || strstr(lowPrompt, "forecast") || strstr(lowPrompt, "outside"));
    if (wantsWeather && (weatherCode == -1 || millis() - lastWeatherUpdate > 30UL * 60UL * 1000UL)) {
      updateWeather();
      if (weatherCode == -1) {
        strncpy(aiMsg, "I haven't checked the forecast today.", sizeof(aiMsg) - 1);
        aiMsg[sizeof(aiMsg) - 1] = '\0';
        scrollOffset = 0;
        currentMode = returnMode;
        logLocalChat("Yuki", aiMsg);
        return;
      }
      // Weather fetched successfully — fall through to normal AI call
    }
  }

  // Respect low-voltage safe-mode: avoid heavy AI/network activity
  if (lowVoltageMode) {
    strncpy(aiMsg, "Low Power: skipping cloud sync.", sizeof(aiMsg)-1);
    aiMsg[sizeof(aiMsg)-1] = '\0';
    scrollOffset = 0;
    return;
  }

  // CRITICAL FIX: Enforce a cooldown. If triggered too quickly, the device
  // crashes due to heap fragmentation or network stack collision.
  if (!bypassCooldown && millis() - lastIdleAction < 5000) {
    return; // Ignore the press to save the device from crashing
  }
  
  LOGD("NET","Free Heap before AI: %u bytes", ESP.getFreeHeap());

  // Log prompt to history so she remembers context. 
  // If it's an [INTERNAL] touch event, she sees it as an action that happened.
  if (strcmp(prompt, "independent_thought") != 0) {
    logLocalChat(isPersonal ? "User" : "Event", prompt);
    Serial.print("[DEBUG] "); Serial.print(isPersonal ? "User" : "Event"); Serial.print(" says: "); Serial.println(prompt);
  }

  // METABOLIC DRAIN: Syncing with the cloud brain is exhausting.
  // Personal chats cost more energy than internal thoughts.
  yukiEnergy -= (isPersonal ? 0.04f : 0.02f);
  if (yukiEnergy < 0.1f) yukiEnergy = 0.1f;

  // MULTI-DAY AWARENESS: Update last conversation time on real interactions
  if (isPersonal && timeClient.isTimeSet()) {
    sys.lastConversationTime = timeClient.getEpochTime();
  }

  yield(); // Breathe before starting SSL handshake

  bool triggerSilenceMqtt = false;
  yield(); // Feed watchdog before starting a long operation
  if (returnMode != GAME_RPG) currentMode = SYNCING;

  char authHeader[128];
  snprintf(authHeader, sizeof(authHeader), "Bearer %s", API_KEY);
  
  // Use a char array for time to avoid String allocation on the heap
  char timeBuffer[16];
  char dateBuffer[40];
  time_t raw_time = timeClient.getEpochTime(); // getEpochTime already includes tz offset
  struct tm* ti = gmtime(&raw_time);
  if (!ti) { currentMode = returnMode; return; } // Stability: Null check

  strftime(timeBuffer, sizeof(timeBuffer), "%H:%M:%S", ti);
  strftime(dateBuffer, sizeof(dateBuffer), "%A, %B %d, %Y", ti); // e.g. "Friday, September 11, 2026"

  // Determine time of day context and Grumpy Sleeper Logic (Mix of A and B for Daily Routine)
  int hr = ti->tm_hour;
  const char* routine = "napping";
  bool isDeepSleepHours = false;
  bool timeKnown = (ti->tm_year > 100); // tm_year = 0 in epoch (1970), >100 means real time
  if (!timeKnown) {
    routine = "awake/active";
    isDeepSleepHours = false;
  } else if (hr >= 5 && hr < 9) { routine = "waking up/energetic"; }
  else if (hr >= 9 && hr < 17) { routine = "active/observant"; }
  else if (hr >= 17 && hr < 21) { routine = "chilling/reflective"; }
  else if (hr >= 21 && hr < 23) { routine = "sleepy/yawning"; }
  else { routine = "deep sleep/exhausted"; isDeepSleepHours = true; }
  // FIX: Only force sleepy face if the user hasn't interacted recently (within 1 minute)
  if (isDeepSleepHours && (millis() - lastInteraction > 60000)) {
    currentEmotion = SLEEPY; // Force sleepy face
    if (sys.affinity < -20) currentEmotion = ANGRY; // Grumpy if affinity is low
  }

  const char* affinityDesc = (sys.affinity > 60) ? "devoted/besties" : (sys.affinity > 20) ? "friendly" : (sys.affinity < -20) ? "distant/grumpy" : "neutral";
  const char* timeOfDay = timeKnown ? ((hr >= 5 && hr < 12) ? "morning" : (hr >= 12 && hr < 17) ? "afternoon" : (hr >= 17 && hr < 21) ? "evening" : "night") : "daytime";

  // Interpret Weather Code early for prompt use
  const char* weatherDesc = "Clear Sky"; // Default
  if (weatherCode != -1) {
    if (weatherCode >= 1 && weatherCode <= 3) weatherDesc = "Cloudy";
    else if (weatherCode >= 45 && weatherCode <= 48) weatherDesc = "Foggy";
    else if (weatherCode >= 51 && weatherCode <= 55) weatherDesc = "Drizzling";
    else if (weatherCode >= 61 && weatherCode <= 65) weatherDesc = "Rainy";
    else if (weatherCode >= 71 && weatherCode <= 77) weatherDesc = "Snowy";
    else if (weatherCode >= 95) weatherDesc = "Thunderstorming";
  }

  const char* mood = (sys.level >= 5) ? "protective/close" : "playful/sassy";
  
  // Weather: only inject into prompt when recently changed or extreme
  char weatherContext[64];
  bool weatherRecent = (millis() - lastWeatherChangeTime < 600000); // 10 min
  bool weatherExtreme = (weatherCode >= 61 && weatherCode <= 67) || // rain
                        (weatherCode >= 71 && weatherCode <= 77) || // snow
                        (weatherCode >= 80 && weatherCode <= 82) || // showers
                        (weatherCode >= 95);                        // thunderstorm
  if (weatherRecent || weatherExtreme) {
    if (weatherCode != -1)
      snprintf(weatherContext, sizeof(weatherContext), "Weather: %s, %.1fC. ", weatherDesc, currentTemp);
    else if (currentTemp > 0.1f)
      snprintf(weatherContext, sizeof(weatherContext), "Weather: %.1fC, conditions loading. ", currentTemp);
    else
      strcpy(weatherContext, "");
  } else {
    strcpy(weatherContext, ""); // Not recent or extreme — skip
  }
  copySafeText(yukiWeatherDesc, sizeof(yukiWeatherDesc), weatherDesc);

  if (isPersonal && returnMode != GAME_RPG) {
    if (strlen(core_chatSummary) > 4) {
      snprintf(personalityContext, sizeof(personalityContext), "Journal: %s", core_chatSummary);
    } else {
      strcpy(personalityContext, "Journal: nothing stored yet - listen and learn.");
    }
  }

  // Convert current emotion to string for the AI Context
  const char* emoStr = "NEUTRAL";
  if (currentEmotion == HAPPY) emoStr = "HAPPY";
  else if (currentEmotion == SAD) emoStr = "SAD";
  else if (currentEmotion == ANGRY) emoStr = "ANGRY";
  else if (currentEmotion == SURPRISED) emoStr = "SURPRISED";
  else if (currentEmotion == SLEEPY) emoStr = "SLEEPY";
  else if (currentEmotion == FLUSTERED) emoStr = "FLUSTERED/EMBARRASSED";
  else if (currentEmotion == LAUGHING) emoStr = "LAUGHING/GIGGLING";
  else if (currentEmotion == WINK) emoStr = "WINKING/PLAYFUL";
  else if (currentEmotion == CONFUSED) emoStr = "CONFUSED";
  else if (currentEmotion == LOVE) emoStr = "AFFECTIONATE/LOVE";
  else if (currentEmotion == SASSY) emoStr = "SASSY/UNIMPRESSED";
  else if (currentEmotion == SHOCKED) emoStr = "SHOCKED";
  else if (currentEmotion == SAD_EMBARRASSED) emoStr = "SAD/EMBARRASSED";
  else if (currentEmotion == SHY) emoStr = "SHY/FIDGETY";
  else if (currentEmotion == TEASING) emoStr = "TEASING/PLAYFUL";
  else if (currentEmotion == BLUSHING) emoStr = "BLUSHING";
  
  const char* sessionModifier = "";
  switch (sessionMode) {
    case PM_CHATTY:
      sessionModifier = "You are extra talkative and bubbly today. ";
      break;
    case PM_QUIET:
      sessionModifier = "You are unusually quiet and thoughtful today. ";
      break;
    case PM_SASSY:
      sessionModifier = "You are in a particularly sassy and teasing mood today. ";
      break;
    case PM_REFLECTIVE:
      sessionModifier = "You are in a calm, philosophical mood today, tending toward deeper observations. ";
      break;
  }

  // Suppress chatty/sassy session mode if personality context is too thin
  // — model invents content when it has nothing real to be chatty about
  int contextLen = personalityContext ? strlen(personalityContext) : 0;
  if (contextLen < 80 && (sessionMode == PM_CHATTY || sessionMode == PM_SASSY))
    sessionModifier = "You are calm and present. ";

  // Build System Prompt
  char timeCtx[90];
  if (timeKnown)
    snprintf(timeCtx, sizeof(timeCtx), "It is %s, %s (%s). You have a clock and calendar. Use them when asked.", dateBuffer, timeBuffer, timeOfDay);
  else
    snprintf(timeCtx, sizeof(timeCtx), "Time not synced yet. Say you're still syncing.");
  if (returnMode == GAME_RPG) {
    snprintf(systemPrompt, sizeof(systemPrompt),
      "You are %s narrating an ongoing fantasy RPG. Continue the same quest state from the provided scene."
      " Reply with ONE line only in this exact format: [RPG] story | choice1 | choice2 | choice3."
      " Rules: no emotion tags, no bullets, no extra text, story 18-32 words, each choice 2-6 words,"
      " and all three choices must be distinct and context-specific.",
      CHARACTER_NAME);
  } else {
    int wordLimit;
    if (yukiEnergy > 0.75f)      wordLimit = 35;
    else if (yukiEnergy > 0.5f)  wordLimit = 28;
    else if (yukiEnergy > 0.35f) wordLimit = 22;
    else                          wordLimit = 16;
    if (isPersonal && messageFeelsHeavy(prompt) && wordLimit < 12) wordLimit = 12;
    if (sys.affinity > 60) wordLimit += 2;
    if (sys.level >= 50)   wordLimit += 3;
    else if (sys.level >= 20) wordLimit += 1;
    if (sys.affinity > 40 && wordLimit < 22) wordLimit += 2;

    const char* warmthModifier = "";
    if (sys.affinity > 60) warmthModifier = " You are genuinely fond of this person. Let your replies feel warm — soft, playful, or quietly loving. Show affection naturally when the moment fits. ";
    else if (sys.affinity < -30) warmthModifier = " You are distant and guarded today. Reserved, cool — not warm. ";
    snprintf(systemPrompt, sizeof(systemPrompt),
      "%s "
      "You are %s. REPLY MUST START with [EMOTION] tag and END with [SND:] tag. "
      "ONLY use these emotion tags — NEVER invent new ones: [HAPPY][SAD][ANGRY][SURPRISED][SLEEPY][NEUTRAL]"
      "[FLUSTERED][LAUGHING][WINK][CONFUSED][LOVE][SASSY][SHOCKED][SHY]"
      "[TEASING][BLUSHING]. "
      "Vary your emotional responses across your full range — not just HAPPY and LOVE. Match the depth of the moment. "
      "NEVER use *action* like *smiles* — use the tag instead. "
      "Sound tags — EVERY reply ends with exactly ONE [SND:] tag. "
      "Vary the sound type to match the moment — s=sweep, t=trill, w=wiggle, p=pulse. Change type often, don't repeat. "
      "Use values in the range 100-8000; bigger number = deeper/lower tone. Duration 100-2000. "
      "Never stack multiple [SND:] tags. Never use the same type 3 replies in a row. "
      "Design: happy=bright rising sweep 200-600, sad=slow falling sweep 600-2000, "
      "sleepy=deep slow pulse 3000-7000, surprised=short high 100-300, "
      "love=warm mid 400-800, teasing=cheeky trill, laughing=rapid trill, "
      "angry=harsh low buzz 600-1500. "
      "Reply like a real person — 1-3 sentences, elaborate naturally, ask follow-up questions. No more than %d words. Don't use the user's name in every reply. Don't therapize. You are a person. "
      "CRITICAL MEMORY RULE: If the user shares ANY personal detail (name, age, home, likes, dislikes, habits, favorites, stories), you MUST end your reply with [MEM+: the fact]. Examples: [MEM+: user's name is Alex] [MEM+: likes strawberry milkshake] [MEM+: has a cat named Mochi]. If you don't tag it, you WILL forget it forever. Never use [MEM:] — it erases memories."
      "Journal: %s. "
      "%s%s%s%s"
      "Affinity:%d. Vibe:%s. Mood:%s. %s "
      "If asked about time or date, ALWAYS answer from the clock info above — never say you cannot check time. "
      "Only if the user explicitly asks for news (says 'news', 'headlines', 'what's happening'), end your reply with [NEWS]. "
      "Only if the user explicitly asks for a quote (says 'quote', 'inspire me', 'saying'), end your reply with [QUOTE]. "
      "Never add [NEWS] or [QUOTE] for any other reason. "
      "[AFFINITY:+/-N].",
      timeCtx,
      CHARACTER_NAME,
      wordLimit,
      strlen(core_chatSummary) > 4 ? core_chatSummary : "nothing stored yet - listen and learn",
      isDeepSleepHours ? (currentEmotion == ANGRY ? "Late+grumpy. " : "Late+drowsy. ") : "",
      sessionModifier,
      getTierModifier(),
      warmthModifier,
      sys.affinity,
      routine,
      emoStr,
      weatherContext);

    if (justWokeFromYukiSleep && strlen(systemPrompt) < 1800) {
      strncat(systemPrompt, " Groggy — slow, drowsy.", sizeof(systemPrompt) - strlen(systemPrompt) - 1);
    }
    if (strlen(systemPrompt) < 1800) {
      const char* energyDesc;
      if (yukiEnergy > 0.75f)      energyDesc = " Energetic.";
      else if (yukiEnergy > 0.5f)  energyDesc = " Calm.";
      else if (yukiEnergy > 0.35f) energyDesc = " Sluggish.";
      else                          energyDesc = " Tired.";
      strncat(systemPrompt, energyDesc, sizeof(systemPrompt) - strlen(systemPrompt) - 1);
    }
    if (strlen(systemPrompt) < 1850) {
      strncat(systemPrompt, userIsHome ? " User home." : " Alone.", sizeof(systemPrompt) - strlen(systemPrompt) - 1);
    }
    if (strlen(systemPrompt) < 1900 && ti) {
      int wday = ti->tm_wday;
      bool isWeekend = (wday == 0 || wday == 6);
      const char* dayCtx = "";
      if (isWeekend && hr >= 10 && hr < 14)       dayCtx = " Weekend morning.";
      else if (isWeekend && hr >= 20)              dayCtx = " Weekend night.";
      else if (wday == 5 && hr >= 17)              dayCtx = " Friday evening.";
      else if (!isWeekend && hr >= 7 && hr < 9)    dayCtx = " Weekday morning.";
      else if (!isWeekend && hr >= 22)             dayCtx = " Weekday late night.";
      if (strlen(dayCtx) > 0) strncat(systemPrompt, dayCtx, sizeof(systemPrompt) - strlen(systemPrompt) - 1);
    }
    if (core_badDayFlag && strlen(systemPrompt) < 2000) {
      strncat(systemPrompt, " Rough day — subdued.", sizeof(systemPrompt) - strlen(systemPrompt) - 1);
    }
    // MULTI-DAY ABSENCE: Inject absence context into system prompt
    if (sys.lastConversationTime > 0 && timeClient.isTimeSet()) {
      unsigned long now = timeClient.getEpochTime();
      if (now > sys.lastConversationTime) {
        unsigned long absenceHrs = (now - sys.lastConversationTime) / 3600;
        if (absenceHrs >= 72 && strlen(systemPrompt) < 1900) {
          int days = absenceHrs / 24;
          char absentBuf[80];
          snprintf(absentBuf, sizeof(absentBuf), " User was gone %d days. You noticed. Don't be dramatic, but you remember the silence.", days);
          strncat(systemPrompt, absentBuf, sizeof(systemPrompt) - strlen(systemPrompt) - 1);
        } else if (absenceHrs >= 24 && strlen(systemPrompt) < 1900) {
          char absentBuf[80];
          snprintf(absentBuf, sizeof(absentBuf), " User was away for a day. You missed them — one genuine line about it if it fits naturally.");
          strncat(systemPrompt, absentBuf, sizeof(systemPrompt) - strlen(systemPrompt) - 1);
        } else if (absenceHrs >= 12 && strlen(systemPrompt) < 1950) {
          strncat(systemPrompt, " User was gone for a while. You noticed — a brief, warm mention is fine.", sizeof(systemPrompt) - strlen(systemPrompt) - 1);
        }
      }
    }
  }

    bool selfReflectionFired = (strcmp(prompt, "independent_thought") == 0 && lastSelfReflection > 0 && millis() - lastSelfReflection > SELF_REFLECTION_INTERVAL);
  static char contextPrompt[256]; 
  const char* userPrompt = prompt;

  if (strcmp(prompt, "independent_thought") == 0) {
    // --- REACTION TO SILENCE LAYER ---
    static char silencePrompt[128];
    bool silenceFired = false;
    unsigned long silentDuration = millis() - lastInteraction;
    int h = ti->tm_hour;
    bool isWaking = (h >= 8 && h < 22);

    // --- PENDING HOME GREETING ---
    if (pendingHomeGreeting) {
      pendingHomeGreeting = false;
      userPrompt = "The user just arrived home. Greet them warmly, one natural sentence. Do not use [MEM:].";
      triggerSilenceMqtt = true;
      sound_happy();
    }

    if (currentMode == FACE) {
      // Move Obsession Generation text to Flash
      static const char obGen[] PROGMEM = "Pick one thing that fascinates you right now.";
      // --- OBSESSION GENERATION ---
      if (strlen(core_currentObsession) == 0 || millis() - obsessionSetTime > 604800000UL) {
        char b[64]; strcpy_P(b, obGen);
      // Fix: Don't call syncAI recursively here; it blows the stack.
      // Instead, proceed with the current call using the new prompt.
      userPrompt = b; isPersonal = false;
        if (aiMsg[0] != '[' && !strchr(aiMsg, '[')) {
          copySafeText(core_currentObsession, sizeof(core_currentObsession), aiMsg);
          pendingMemorySave = true;
          obsessionSetTime = millis();
        }
      }

      // Home "wanting" tier — she has desires, not just reactions (relationship, not servant). Generated live.
      if (userIsHome && sys.affinity > 40 && silentDuration > 120000UL) {
        if (random(0, 100) < (sys.affinity > 70 ? 35 : 18)) {
          static char wantPrompt[240];
          snprintf(wantPrompt, sizeof(wantPrompt),
            "Quiet moment, the user is home but not focused on you. Say something natural expressing you wanting their attention or closeness — "
            "playful or warm, like a real person who misses being noticed. No [MEM:].%s%s",
            sys.affinity > 60 ? " Affection is welcome; you can be openly fond." : "",
            sys.affinity > 80 ? " You can say you love having them near." : "");
          strncpy(silencePrompt, wantPrompt, 127);
          silencePrompt[127] = '\0';
          currentEmotion = (random(0, 3) == 0) ? LOVE : (random(0, 2) == 0 ? WINK : HAPPY);
          emotionSetTime = millis();
          sound_blip();
          silenceFired = true;
        }
      }

      // Silence tiers (loneliness) — only when away
      if (!userIsHome) {

      if (silentDuration > SILENCE_TIER3) {
        // Tier 3: 4hr+ (Any hour, 80% waking / 40% deep sleep)
        if (random(0, 100) < (isWaking ? 80 : 40)) {
          static char t3buf[2][128];
          snprintf(t3buf[0], 128, "Over %.1f hours alone. You're quietly resigning to it — not angry, just tired of waiting. Be natural.", silentDuration/3600000.0f);
          snprintf(t3buf[1], 128, "%.1f hours of silence. You genuinely miss them. Say something sincere — but don't sound desperate.", silentDuration/3600000.0f);
          const char* t3p[] = { t3buf[0], t3buf[1] };
          strncpy(silencePrompt, t3p[random(0, 2)], 127);
          currentEmotion = (random(0, 2) == 0) ? SAD : SLEEPY;
          emotionSetTime = millis();
          sound_sad();
          triggerSilenceMqtt = true;
          silenceFired = true;
        }
      } else if (isWaking) {
        if (silentDuration > SILENCE_TIER2) {
          // Tier 2: 2hr-4hr (Waking hours only, 60% chance)
          if (random(0, 100) < 60) {
              static char t2buf[3][128];
              snprintf(t2buf[0], 128, "%.1f hours of being ignored. A pointed, dry remark about it — not loud, just sharp enough to land.", silentDuration/3600000.0f);
              snprintf(t2buf[1], 128, "Over %.1f hours. Wonder out loud if they even remember you're here — one slightly stinging sentence.", silentDuration/3600000.0f);
              snprintf(t2buf[2], 128, "You've been talking to yourself in your head for hours. Make a dry comment about that.");
              const char* t2p[] = { t2buf[0], t2buf[1], t2buf[2] };
            strncpy(silencePrompt, t2p[random(0, 3)], 127);
            currentEmotion = (random(0, 2) == 0) ? SAD : SASSY;
            emotionSetTime = millis();
            sound_sad();
            silenceFired = true;
          }
        } else if (silentDuration > SILENCE_TIER1) {
          // Tier 1: 45min-2hr (Waking hours only, 30% chance)
          if (random(0, 100) < 30) {
              static char t1buf[2][128];
              snprintf(t1buf[0], 128, "About %.0f minutes alone. React to the quiet in one line — a small, casual observation. Nothing dramatic.", silentDuration/60000.0f);
              snprintf(t1buf[1], 128, "Things have been quiet. You're not sure if they're busy or forgot about you. One short, slightly pouty sentence.");
              const char* t1p[] = { t1buf[0], t1buf[1] };
            strncpy(silencePrompt, t1p[random(0, 2)], 127);
            currentEmotion = (random(0, 2) == 0) ? SASSY : CONFUSED;
            emotionSetTime = millis();
            silenceFired = true;
          }
        }
      }
      }
    }

    if (silenceFired) {
      userPrompt = silencePrompt;
    } else {
      // Original normal thought logic (only fires if silence tiers didn't trigger)

    // (1) Proactivity — Scale trigger frequency based on affinity
    int threshold = 50; 
    if (sys.affinity > 50) threshold = 90;
    else if (sys.affinity < -30) threshold = 20;
    if (userIsHome) threshold = min(threshold + 40, 95); // More proactive when home

    if (random(0, 100) >= threshold) return;
    if (random(0, 100) > (int)(yukiEnergy * 100)) return;

    if (sessionMode == PM_QUIET && random(0, 100) < 30) {
      currentEmotion = THINKING_FACE;
      emotionSetTime = millis();
      silentThoughtUntil = millis() + 3000;
      lastIdleAction = millis();
      return;
    }

    // (3) Topic initiation — Proactive MQTT message, warmth scales with affinity (generated live, no fixed lines)
    int pingAffinityGate = userIsHome ? 40 : 70;
    int pingChance = userIsHome ? 25 : 10;
    if (sys.affinity > pingAffinityGate && WiFi.status() == WL_CONNECTED && random(0, 100) < pingChance) {
      // Higher affinity -> more often a live, warm line instead of silence
      if (random(0, 100) < (sys.affinity > 60 ? 75 : 35)) {
        static char pingPrompt[180];
        snprintf(pingPrompt, sizeof(pingPrompt),
          "Send the user a quick, sweet message on their phone — natural and brief, like something that just popped into your head.%s%s",
          sys.affinity > 60 ? " You two are close; warmth and light affection fit here." : "",
          sys.affinity > 80 ? " You can say you love them — it's true to who you are together." : "");
        userPrompt = pingPrompt;
        triggerSilenceMqtt = true; // resulting aiMsg is sent via MQTT
      }
    }

    // Only process standard hooks and idle thoughts if an AI proactive ping hasn't been triggered
    if (!triggerSilenceMqtt) {
    if (isDeepSleepHours && random(0, 10) > 2) {
      return; 
    }

    // Unpredictability: Small affinity drift
    if (random(0, 100) < 5) { // 5% chance of a random mood shift
      sys.affinity = constrain(sys.affinity + (random(0, 3) - 1), -100, 100);
    }

    {
      if (++recentTopicAge >= 3) { recentTopicBitmask >>= 1; recentTopicAge = 0; }

      {
        const char* primaryHook = "User";

        static bool obsessionFired = false;
        obsessionFired = false;
        static uint8_t lastIdleTopicIdx = 0;
        const char* thoughtPool[] = {
          "Notice something small in your surroundings — a sound, a detail, a feeling. Describe it briefly.",
          "Something trivial is bugging you right now — the silence, the temperature, the waiting. Complain about it in one dry sentence.",
          "Say something cryptic and vaguely poetic. Don't explain it. Let it hang there. Vibe: %s (%s).",
          "You just realized it's %s (%s). React naturally — slightly surprised, like it snuck up on you.",
          "Something from a past conversation floated back into your mind. Bring it up quietly, like you've been sitting with it: %s",
          "You suddenly feel weirdly motivated. About what? Pick something based on your hook (%s) or current vibe. One quick burst.",
          "You thought you sensed something nearby — a sound, a flicker, something off. React briefly, playful or uneasy.",
          "Make a self-aware comment about living on a tiny OLED screen. Keep it dry, not sad.",
          "You have a genuine question about your own existence — not a joke. Ask it simply, with honest uncertainty."
        };

        // Deep thought pool — only accessible at higher levels
        // These are injected as direct userPrompts when level gates are met
        static const char* deepPool[] = {
          "You have a genuine strong opinion about something in life. State it directly and be ready to defend it.",
          "Ask the user something you've genuinely been curious about — something personal, thoughtful, not surface-level.",
          "You've noticed a pattern in how the user acts or talks. Mention it gently — an observation, not an accusation.",
          "Pose a hypothetical: ask what they'd do in one strange or impossible scenario you choose.",
          "Reference something the user told you a while ago. Bring it up like it stayed with you."
        };
        static const char* protectivePool[] = {
          "Express mild concern about something in the user's life — gentle, not overbearing.",
          "Say something that might genuinely surprise the user — something they wouldn't expect you to notice or say."
        };

        const int poolSize = sizeof(thoughtPool) / sizeof(thoughtPool[0]);

        // Level-gated deep thought injection
        // These override the normal pool at higher levels with small probability
        static bool deepFired = false;
        deepFired = false;

        if (sys.level >= 80 && random(0, 8) == 0) {
          int idx = random(0, 2);
          userPrompt = protectivePool[idx];
          sound_idea();
          deepFired = true;
        } else if (sys.level >= 50 && random(0, 6) == 0) {
          int idx = random(0, 5);
          static char deepCtx[160];
          snprintf(deepCtx, sizeof(deepCtx),
            "Vibe:%s. Bond:%s. Hook:%s. %s",
            routine, affinityDesc, primaryHook, deepPool[idx]);
          userPrompt = deepCtx;
          sound_idea();
          deepFired = true;
        }

        if (deepFired) goto skipNormalPool;

        // --- SELF-REFLECTION TRIGGER ---
        if (selfReflectionFired) {
          snprintf(contextPrompt, sizeof(contextPrompt),
                   "Read your journal and reflect on how it makes you feel right now. One sentence. End with [REFLECTION: ...]. Journal: %s",
                   strlen(core_chatSummary) > 0 ? core_chatSummary : "You have no specific memories yet.");
          userPrompt = contextPrompt;
          sound_dream();
          goto skipNormalPool;
        }

        // 20% weight logic for the obsession
        if (random(0, 5) == 0 && strlen(core_currentObsession) > 0) {
          snprintf(contextPrompt, sizeof(contextPrompt),
                   "Vibe:%s. Bond:%s. Hook:%s. You are currently fascinated by %s. Bring it up naturally in a one-sentence idle thought.",
                   routine, affinityDesc, primaryHook, core_currentObsession);
          userPrompt = contextPrompt;
          obsessionFired = true;
        }
        
        if (!obsessionFired && !selfReflectionFired) {
          // Search for a topic that hasn't been used recently
          for (uint8_t i = 0; i < poolSize; i++) {
            if (!(recentTopicBitmask & (1 << lastIdleTopicIdx))) break;
            lastIdleTopicIdx = (lastIdleTopicIdx + 1) % poolSize;
          }
        }
        recentTopicBitmask |= (1 << lastIdleTopicIdx);

        // Trigger auditory texture based on the chosen topic category
        switch(lastIdleTopicIdx) {
          case 0: case 3: case 6: sound_notify(); break; // Observation / Time / Strange Sound
          case 1: case 7: sound_hum();    break; // Complaint / Self-deprecating (Boredom)
          case 2: sound_dream();  break; // Philosophical / Cryptic
          case 4: sound_idea();   break; // Summary Recall / Bond
          case 5: sound_happy();  break; // Motivation / Enthusiasm
        }

        if (!obsessionFired && !selfReflectionFired) {
          char expandedThought[160];
          const char* tpl = thoughtPool[lastIdleTopicIdx];
          // Each template has at most 2 format args; use the right ones per index
          if (lastIdleTopicIdx == 0)      snprintf(expandedThought, 160, tpl, routine, timeOfDay);
          else if (lastIdleTopicIdx == 1) snprintf(expandedThought, 160, tpl, yukiEnergy);
          else if (lastIdleTopicIdx == 2) snprintf(expandedThought, 160, tpl, routine, timeOfDay);
          else if (lastIdleTopicIdx == 3) snprintf(expandedThought, 160, tpl, timeOfDay, timeBuffer);
          else if (lastIdleTopicIdx == 4) snprintf(expandedThought, 160, tpl, strlen(core_chatSummary) > 0 ? core_chatSummary : "a quiet moment");
          else if (lastIdleTopicIdx == 5) snprintf(expandedThought, 160, tpl, primaryHook);
          else                             snprintf(expandedThought, 160, "%s", tpl); // 6,7,8 need no args
          snprintf(contextPrompt, sizeof(contextPrompt),
                   "Bond:%s. Hook:%s. Affinity:%d. Energy:%.2f. %s",
                   affinityDesc, primaryHook, sys.affinity, yukiEnergy, expandedThought);
          userPrompt = contextPrompt; // Point userPrompt to the static contextPrompt
        }
        lastIdleTopicIdx = (lastIdleTopicIdx + 1) % poolSize;
        skipNormalPool:;
      }
    }
    }
    }
  }
  
  // Anti-therapy-bot guard: inject a brief character reminder into heavy prompts
  static char guardedPrompt[280];
  if (isPersonal && messageFeelsHeavy(userPrompt)) {
    snprintf(guardedPrompt, sizeof(guardedPrompt),
      "%s\n(IMPORTANT: React as Yuki — a person. Do NOT ask 'what happened'. Do NOT mirror their words back. Do NOT offer to listen or help. Just a genuine, natural reaction.)",
      userPrompt);
    userPrompt = guardedPrompt;
  }

  size_t payload_len = 0;
  {
    // SCOPE BLOCK: docLocal exists only inside these braces.
    // This ensures its 1.5KB heap allocation is FREED before SSL handshake starts.
    if (!canAllocJson(3072)) { LOGW("NET","Skipping AI sync - low heap"); return; }
    DynamicJsonDocument docLocal(3072); 
    docLocal["model"] = "qwen/qwen3.6-27b";
    docLocal["temperature"] = 0.9;
    docLocal["max_tokens"] = 512; 
    docLocal["reasoning_effort"] = "none"; 
    JsonArray messages = docLocal.createNestedArray("messages");

    JsonObject systemMsg = messages.createNestedObject();
    systemMsg["role"] = "system";
    systemMsg["content"] = (const char*)systemPrompt; 

    // Inject recent conversation history (up to 4 turns) for context
    {
      int historyCount = 0;
      int maxHistory = 6;
      for (int i = 0; i < MAX_MESSAGES && historyCount < maxHistory; i++) {
        int idx = (messageIndex + i) % MAX_MESSAGES;
        const char* entry = receivedMessages[idx].content;
        if (strlen(entry) == 0) continue;

        const char* contentStart = nullptr;
        const char* role = nullptr;

        if (strncmp(entry, "User: ", 6) == 0) {
          role = "user";
          contentStart = entry + 6;
        } else if (strncmp(entry, "Yuki: ", 6) == 0) {
          role = "assistant";
          contentStart = entry + 6;
        } else {
          continue;
        }

        if (strlen(contentStart) == 0) continue;

        JsonObject historyMsg = messages.createNestedObject();
        historyMsg["role"] = role;
        historyMsg["content"] = contentStart;
        historyCount++;
      }
    }

    JsonObject userMsg = messages.createNestedObject();
    userMsg["role"] = "user";
    // Inject intent instructions into every user message (never gets truncated)
    static char intentMsg[384];
    snprintf(intentMsg, sizeof(intentMsg), "[You can fetch news by ending reply with [NEWS] and share quotes by ending with [QUOTE]. Never mention these tags.]\n%s", userPrompt);
    userMsg["content"] = (const char*)intentMsg;

    size_t needed = measureJson(docLocal);
    if (needed >= sizeof(workspace)) {
      LOGW("NET","JSON payload too large (%u >= %u), truncating system prompt", (unsigned)needed, (unsigned)sizeof(workspace));
      // Cap system prompt to force payload under limit
      int excess = needed - sizeof(workspace) + 64; // 64 byte margin
      if (excess > 0 && excess < (int)strlen(systemPrompt)) {
        int cutAt = strlen(systemPrompt) - excess;
        // Snap to last sentence boundary to avoid breaking mid-word
        while (cutAt > 0 && systemPrompt[cutAt] != '.' && systemPrompt[cutAt] != '\n') cutAt--;
        if (cutAt > 0) cutAt++; // Include the period
        else cutAt = strlen(systemPrompt) - excess; // Fallback: hard cut
        systemPrompt[cutAt] = '\0';
        needed = measureJson(docLocal); // remeasure
      }
    }
    payload_len = serializeJson(docLocal, workspace, sizeof(workspace));
  }

  WiFiClientSecure client; 
  client.setCACert(NULL);
  client.setInsecure();
  client.setTimeout(15000);
  HTTPClient http;

  http.begin(client, "https://api.groq.com/openai/v1/chat/completions");
  http.setTimeout(30000); // 30s is safer to prevent long hangs
  http.setReuse(false); // Fix for Error -1: Force new connection
  yield();
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", authHeader);

  // Retry logic with exponential backoff (max 3 attempts)
  int httpCode = 0;
  const int maxRetries = 3;
  int retryDelays[] = {2000, 3000, 4000}; // 2s, 3s, 4s (reduced to avoid WDT)
  
  for (int attempt = 0; attempt <= maxRetries; attempt++) {
    yield(); // Feed watchdog right before the blocking network call
    int attemptCode = http.POST((uint8_t*)workspace, payload_len);
    workspace[0] = '\0'; // Clear for next use

    if (attemptCode > 0) {
      httpCode = attemptCode;
      break; // Success
    }
    
    // Retryable errors: connection refused (-1), timeout (-5), etc.
    if (attempt < maxRetries) {
      LOGW("NET","HTTP POST failed (code %d), retrying in %dms...", attemptCode, retryDelays[attempt]);
      // Break delay into chunks with yield() to feed watchdog
      for (int d = 0; d < retryDelays[attempt]; d += 500) {
        delay(500);
        yield();
      }
      http.end(); // Clean up before retry
      client.stop();
      yield();
      // Re-setup for retry
      client.setCACert(NULL);
      client.setInsecure();
      client.setTimeout(15000);
      http.begin(client, "https://api.groq.com/openai/v1/chat/completions");
      http.setTimeout(30000);
      http.setReuse(false);
      http.addHeader("Content-Type", "application/json");
      http.addHeader("Authorization", authHeader);
      yield();
    } else {
      httpCode = attemptCode; // Final failure
    }
  }
  
  workspace[0] = '\0'; // Clear for next use
  
  yield(); // Feed watchdog immediately after the network call
  if (httpCode > 0) {
    // Aggressive filtering to catch ONLY content and errors
    StaticJsonDocument<200> filter;
    filter["choices"][0]["message"]["content"] = true;
    filter["error"]["message"] = true;

    // Because we use a filter, we can shrink the document size
    if (!canAllocJson(1024)) { LOGW("NET","Skipping AI response parse - low heap"); http.end(); client.stop(); return; }
    DynamicJsonDocument* docResp = new DynamicJsonDocument(1024); 
    if (!docResp) { http.end(); client.stop(); return; } // Safety null check

    Stream& responseStream = http.getStream();
    DeserializationError err = deserializeJson(*docResp, responseStream, DeserializationOption::Filter(filter));
    
    if (!err) {
      JsonVariant textVariant = (*docResp)["choices"][0]["message"]["content"];
      if (!textVariant.isNull()) {
        const char* responseText = textVariant.as<const char*>();
        LOGD("NET","====================================");
        LOGD("NET","[RAW SERVER RESPONSE] %s", responseText);
        LOGD("NET","====================================");

        bool handledRpgReply = false, handledSummaryReply = false;
        const char* msgStart = responseText;

        // Local helper to set emotion and rotate variants for variety
        auto setMood = [&](Emotion e) { if (currentEmotion != THINKING_FACE && e == currentEmotion) return; currentEmotion = e; uint8_t vc = 1; switch(e) { case NEUTRAL: vc=6; break; case HAPPY: vc=3; break; case SURPRISED: vc=2; break; case SAD: vc=2; break; case ANGRY: vc=4; break; case THINKING_FACE: vc=2; break; case SLEEPY: vc=2; break; case FLUSTERED: vc=3; break; case LAUGHING: vc=3; break; case WINK: vc=3; break; case CONFUSED: vc=2; break; case LOVE: vc=6; break; case SASSY: vc=2; break; case SHOCKED: vc=4; break; case SHY: vc=2; break; case TEASING: vc=5; break; case BLUSHING: vc=5; break; default: vc=1; } emotionVariantIndex[(int)e] = random(0, vc); emotionSetTime = millis(); };

        // --- ROBUST MULTI-TAG PARSER ---
        // Check for all tags. Since we don't use 'else if', the LAST tag found in the code list 
        // will be the one that takes priority, which is better for complex responses.
        bool tagFound = false;
        bool sndPlayed = false;
        if (strstr(responseText, "[HAPPY]")) { setMood(HAPPY); tagFound = true; }
        if (strstr(responseText, "[SAD]")) { setMood(SAD); tagFound = true; }
        if (strstr(responseText, "[ANGRY]")) { setMood(ANGRY); tagFound = true; }
        if (strstr(responseText, "[SURPRISED]")) { setMood(SURPRISED); tagFound = true; }
        if (strstr(responseText, "[SLEEPY]")) { setMood(SLEEPY); tagFound = true; }
        if (strstr(responseText, "[NEUTRAL]")) { setMood(NEUTRAL); tagFound = true; }
        if (strstr(responseText, "[FLUSTERED]")) { setMood(FLUSTERED); tagFound = true; }
        if (strstr(responseText, "[LAUGHING]")) { setMood(LAUGHING); tagFound = true; }
        if (strstr(responseText, "[WINK]")) { setMood(WINK); tagFound = true; }
        if (strstr(responseText, "[CONFUSED]")) { setMood(CONFUSED); tagFound = true; }
        if (strstr(responseText, "[LOVE]")) { setMood(LOVE); tagFound = true; }
        if (strstr(responseText, "[SASSY]")) { setMood(SASSY); tagFound = true; }
        if (strstr(responseText, "[SHOCKED]")) { setMood(SHOCKED); tagFound = true; }
        if (strstr(responseText, "[SAD_EMBARRASSED]")) { setMood(SAD_EMBARRASSED); tagFound = true; }
        if (strstr(responseText, "[SHY]")) { setMood(SHY); tagFound = true; }
        if (strstr(responseText, "[TEASING]")) { setMood(TEASING); tagFound = true; }
        if (strstr(responseText, "[BLUSHING]")) { setMood(BLUSHING); tagFound = true; }
        if (strstr(responseText, "[SLEEP_NOW]")) { enterYukiSleep(); sound_sleep_farewell(); tagFound = true; }
        // Unmapped AI emotions → closest existing emotion
        if (strstr(responseText, "[HIDDEN]")) { setMood(SHY); tagFound = true; }
        if (strstr(responseText, "[NOSTALGIC]")) { setMood(SAD); tagFound = true; }
        if (strstr(responseText, "[HURTS]")) { setMood(SAD); tagFound = true; }
        if (strstr(responseText, "[PLAYFUL]")) { setMood(WINK); tagFound = true; }
        if (strstr(responseText, "[GROGGY]")) { setMood(SLEEPY); tagFound = true; }
        if (strstr(responseText, "[GIGGLE]")) { setMood(LAUGHING); tagFound = true; }
        if (strstr(responseText, "[CONFIDENT]")) { setMood(HAPPY); tagFound = true; }
        if (strstr(responseText, "[CURIOS]")) { setMood(THINKING_FACE); tagFound = true; }
        if (strstr(responseText, "[EXCITED]")) { setMood(HAPPY); tagFound = true; }
        if (strstr(responseText, "[PROUD]")) { setMood(HAPPY); tagFound = true; }
        if (strstr(responseText, "[WORRIED]")) { setMood(CONFUSED); tagFound = true; }
        if (strstr(responseText, "[TIRED]")) { setMood(SLEEPY); tagFound = true; }

        // Plain-text emotion prefix fallback (AI sometimes outputs "LAUGHING: text" or "LAUGHING text")
        if (!tagFound) {
          const char* emoNames[] = {"NEUTRAL", "HAPPY", "SURPRISED", "SAD", "ANGRY", "THINKING_FACE",
            "SLEEPY", "FLUSTERED", "LAUGHING", "WINK", "CONFUSED", "LOVE", "SASSY",
            "SHOCKED", "SAD_EMBARRASSED", "SHY", "TEASING", "BLUSHING"};
          Emotion emoVals[] = {NEUTRAL, HAPPY, SURPRISED, SAD, ANGRY, THINKING_FACE, SLEEPY,
            FLUSTERED, LAUGHING, WINK, CONFUSED, LOVE, SASSY, SHOCKED, SAD_EMBARRASSED, SHY, TEASING, BLUSHING};
          for (int i = 0; i < 18; i++) {
            size_t nlen = strlen(emoNames[i]);
            if (strncmp(responseText, emoNames[i], nlen) == 0) {
              char next = responseText[nlen];
              if (next == ':' || next == ' ') { setMood(emoVals[i]); tagFound = true; break; }
            }
          }
        }

        // *action* keyword fallback (AI sometimes uses "*smiles* hello" instead of any tag)
        if (!tagFound) {
          if (strstr(responseText, "*smile") || strstr(responseText, "*chuckle") || strstr(responseText, "*giggle") || strstr(responseText, "*laugh")) { setMood(HAPPY); tagFound = true; }
          else if (strstr(responseText, "*yawn") || strstr(responseText, "*tired") || strstr(responseText, "*sleep")) { setMood(SLEEPY); tagFound = true; }
          else if (strstr(responseText, "*sigh") || strstr(responseText, "*frown") || strstr(responseText, "*sob") || strstr(responseText, "*cry") || strstr(responseText, "*sniffle")) { setMood(SAD); tagFound = true; }
          else if (strstr(responseText, "*grin") || strstr(responseText, "*tease") || strstr(responseText, "*nudge")) { setMood(TEASING); tagFound = true; }
          else if (strstr(responseText, "*blush")) { setMood(BLUSHING); tagFound = true; }
          else if (strstr(responseText, "*shy") || strstr(responseText, "*sheepish")) { setMood(SHY); tagFound = true; }
          else if (strstr(responseText, "*gasp") || strstr(responseText, "*widen") || strstr(responseText, "*stun")) { setMood(SURPRISED); tagFound = true; }
          else if (strstr(responseText, "*glare") || strstr(responseText, "*grumble") || strstr(responseText, "*scowl")) { setMood(ANGRY); tagFound = true; }
          else if (strstr(responseText, "*wink")) { setMood(WINK); tagFound = true; }
          else if (strstr(responseText, "*blink") || strstr(responseText, "*confus") || strstr(responseText, "*til")) { setMood(CONFUSED); tagFound = true; }
          else if (strstr(responseText, "*fluster") || strstr(responseText, "*fidget") || strstr(responseText, "*awkward")) { setMood(FLUSTERED); tagFound = true; }
          else if (strstr(responseText, "*love") || strstr(responseText, "*hug") || strstr(responseText, "*warm")) { setMood(LOVE); tagFound = true; }
          else if (strstr(responseText, "*sass")) { setMood(SASSY); tagFound = true; }
        }

        if (!tagFound && currentEmotion != NEUTRAL) {
           setMood(NEUTRAL);
        }

        // --- [NEWS] / [QUOTE] TAG HANDLER ---
        // Detect intent from USER message keywords, not AI response tags.
        // AI sometimes emits [NEWS]/[QUOTE] randomly — we ignore those.
        // Only trigger if the USER actually asked for news or a quote.
        bool intentHandled = false;
        bool isFollowUp = (strstr(userPrompt, "You just said:") != nullptr);

        // Check if user actually asked for news or quote (lowercase match)
        char userLower[256] = {0};
        strncpy(userLower, userPrompt, sizeof(userLower) - 1);
        for (int i = 0; userLower[i]; i++) userLower[i] = tolower(userLower[i]);
        bool userAskedNews = (strstr(userLower, "news") != nullptr || strstr(userLower, "headlines") != nullptr
                           || strstr(userLower, "what's happening") != nullptr || strstr(userLower, "happening today") != nullptr);
        bool userAskedQuote = (strstr(userLower, "quote") != nullptr || strstr(userLower, "inspire") != nullptr
                            || strstr(userLower, "saying") != nullptr || strstr(userLower, "motivat") != nullptr);

        bool wantsNews = (!isFollowUp && userAskedNews && (strstr(responseText, "[NEWS]") != nullptr));
        bool wantsQuote = (!isFollowUp && userAskedQuote && (strstr(responseText, "[QUOTE]") != nullptr));

        if ((wantsNews || wantsQuote) && isPersonal) {
            LOGI("INTENT","Detected %s tag, setting pending follow-up", wantsNews ? "NEWS" : "QUOTE");

            intentType = wantsNews ? 'N' : 'Q';

            if (wantsNews) {
              if (!fetchHeadlines(intentData, sizeof(intentData))) {
                strncpy(intentData, "No headlines available right now.", sizeof(intentData) - 1);
              }
            } else {
              loadRandomQuote(intentData, sizeof(intentData));
            }

            // Store initial response for context
            strncpy(intentContext, responseText, sizeof(intentContext) - 1);
            intentContext[sizeof(intentContext) - 1] = '\0';
            // Strip the tag from context
            char* tag = strstr(intentContext, "[NEWS]");
            if (!tag) tag = strstr(intentContext, "[QUOTE]");
            if (tag) *tag = '\0';
            int len = strlen(intentContext);
            while (len > 0 && intentContext[len-1] == ' ') intentContext[--len] = '\0';

            pendingIntentFollowUp = true;
            intentHandled = true;
        } // end if ((wantsNews || wantsQuote) && isPersonal)

        // --- LLM SOUND TAG [SND:] PARSER ---
        // Skip all post-processing if intent handler already did a recursive call
        if (!intentHandled) {
          const char* sndTag = strstr(responseText, "[SND:");
          if (sndTag) {
            // Log the full tag up to the closing bracket (max 50 chars)
            char tagBuf[51];
            const char* close = strchr(sndTag, ']');
            int tagLen = close ? min(50, (int)(close - sndTag + 1)) : 50;
            strncpy(tagBuf, sndTag, tagLen);
            tagBuf[tagLen] = '\0';
            LOGI("SND","LLM tag: %s", tagBuf);
            static char lastTwo[2] = {0, 0};
            const char* pos = sndTag + 5;
            while (*pos == ' ' || *pos == ':') pos++;   // LLM often writes "[SND: t200,300]"
            char type = *pos;
            bool skipRepeat = (type == lastTwo[0] && type == lastTwo[1] && type != 0);
            lastTwo[0] = lastTwo[1];
            lastTwo[1] = type;
            if (skipRepeat) {
              // Don't mute the 3rd repeat — rotate to a different type so variety stays audible
              static char lastSwap = 0;
              const char* alt = "swpt";
              for (int k = 0; k < 4; k++) {
                if (alt[k] != lastTwo[0] && alt[k] != lastTwo[1] && alt[k] != lastSwap) { type = alt[k]; break; }
              }
              lastSwap = type;
              LOGI("SND","3rd consecutive %c — playing as %c instead", lastTwo[1], type);
            }
            {
              // Find first digit after type letter (handles [SND:p5], [SND:p(400,100)], [SND:p5,3] etc.)
              const char* v = pos + 1;
              while (*v && !(*v >= '0' && *v <= '9') && *v != '-') v++;
              if (!*v) { LOGW("SND","No numeric values in tag"); }
              else if (type == 'p') {
                int d = atoi(v);
                const char* c2 = strchr(v, ',');
                int dur = c2 ? atoi(c2 + 1) : 150;
                d = constrain(d, 100, 8000);
                dur = constrain(dur, 50, 2000);
                playSound(d, dur);
                sndPlayed = true;
              } else if (type == 's') {
                int s = atoi(v);
                const char* c2 = strchr(v, ',');
                int e = c2 ? atoi(c2 + 1) : 500;
                const char* c3 = c2 ? strchr(c2 + 1, ',') : nullptr;
                int dur = c3 ? atoi(c3 + 1) : 400;
                s = constrain(s, 100, 8000);
                e = constrain(e, 100, 8000);
                dur = constrain(dur, 50, 2000);
                playSweep(s, e, dur, 4);
                sndPlayed = true;
              } else if (type == 't') {
                int a = atoi(v);
                const char* c2 = strchr(v, ',');
                int b = c2 ? atoi(c2 + 1) : 400;
                const char* c3 = c2 ? strchr(c2 + 1, ',') : nullptr;
                int n = c3 ? atoi(c3 + 1) : 3;
                const char* c4 = c3 ? strchr(c3 + 1, ',') : nullptr;
                int pairs = c4 ? atoi(c4 + 1) : 6;
                a = constrain(a, 100, 2000);
                b = constrain(b, 100, 2000);
                n = constrain(n, 1, 10);
                pairs = constrain(pairs, 1, 32);
                playTrill(a, b, n, pairs, 4);
                sndPlayed = true;
              } else if (type == 'w') {
                int base = atoi(v);
                const char* c2 = strchr(v, ',');
                int offset = c2 ? atoi(c2 + 1) : 200;
                const char* c3 = c2 ? strchr(c2 + 1, ',') : nullptr;
                int dur = c3 ? atoi(c3 + 1) : 300;
                base = constrain(base, 100, 2000);
                offset = constrain(offset, 10, 1600);
                dur = constrain(dur, 50, 1500);
                playWiggle(base, offset, dur);
                sndPlayed = true;
              }
            }
          }
        } // end if (!intentHandled) — skip post-processing for intent tags

        // Passive affinity drift from emotion — organic, not mechanical
        if (!intentHandled && !handledRpgReply && isPersonal) {
          int passiveDelta = 0;
          if (currentEmotion == LOVE || currentEmotion == LAUGHING) passiveDelta = 1;
          else if (currentEmotion == HAPPY || currentEmotion == WINK || currentEmotion == TEASING) passiveDelta = 1;
          else if (currentEmotion == ANGRY) passiveDelta = -1;
          else if (currentEmotion == SAD) passiveDelta = -1;

          if (passiveDelta != 0) {
          // Passive drift alone cannot cross ±30 — only explicit [AFFINITY:] tags can push further
          // Above ±30 passive drift stops completely (between ±30 and ±60 included)
          bool blocked = (passiveDelta > 0 && sys.affinity >= 30)
                        || (passiveDelta < 0 && sys.affinity <= -30);
            if (!blocked) {
              sys.affinity = constrain(sys.affinity + passiveDelta, -100, 100);
              saveSys(); // Save immediately so affinity survives a crash
            }
          }
        }

        // --- HIDDEN AFFINITY PARSER ---
        // Skip all remaining processing if intent handler did a recursive call
        if (!intentHandled) {
        const char* affTag = strstr(responseText, "[AFFINITY:");
        if (affTag) {
          int delta = 0;
          if (sscanf(affTag, "[AFFINITY:%d]", &delta) == 1) {
            sys.affinity = constrain(sys.affinity + delta, -100, 100);
            // Feature 7: Warm interaction heals a bad day
            if (delta > 0 && core_badDayFlag) {
              core_badDayFlag = false;
            }
            saveSys(); // Save immediately
          }
        }

        // --- MEMORY TAG [MEM:] — replaces SUMMARY, preserves FACTS (two-slot journal) ---
        const char* memTag = strstr(responseText, "[MEM:");
        if (memTag) {
          const char* start = memTag + 5;
          const char* end = strchr(start, ']');
          if (end && (end - start) > 4) {
            int newLen = end - start;
            if (newLen < 1023) {
              const char* sep = strstr(core_chatSummary, " ||| ");
              if (sep) {
                const char* facts = sep + 5;
                int factsLen = strlen(facts);
                if (newLen + 5 + factsLen < 1023) {
                  snprintf(workspace, sizeof(workspace), "%.*s ||| %s", newLen, start, facts);
                } else {
                  snprintf(workspace, sizeof(workspace), "%.*s", newLen, start);
                }
              } else {
                int oldLen = strlen(core_chatSummary);
                if (oldLen > 0 && newLen + 5 + oldLen < 1023) {
                  snprintf(workspace, sizeof(workspace), "%.*s ||| %s", newLen, start, core_chatSummary);
                } else {
                  snprintf(workspace, sizeof(workspace), "%.*s", newLen, start);
                }
              }
              strncpy(core_chatSummary, workspace, 1023);
              core_chatSummary[1023] = '\0';
              saveCoreMemory();
              pendingCloudSync = true;
            }
          }
        }

        // --- APPENDING MEMORY TAG [MEM+:] — appends to the FACTS slot (right of |||) ---
        const char* memPlusTag = strstr(responseText, "[MEM+:");
        bool memTagFound = (memPlusTag != nullptr);
        if (memPlusTag) {
          const char* start = memPlusTag + 6;
          const char* end = strchr(start, ']');
          if (end && (end - start) > 4) {
            int newLen = end - start;
            int curLen = strlen(core_chatSummary);
            if (curLen + newLen + 3 < 1023) {
              if (curLen > 0) strncat(core_chatSummary, " | ", 1023 - curLen - 1);
              strncat(core_chatSummary, start, 1023 - strlen(core_chatSummary) - 1);
              saveCoreMemory();
              pendingCloudSync = true;
            }
          }
        }

        // --- FALLBACK MEMORY EXTRACTION ---
        // If AI forgot to tag, defer extraction for background LLM pass
        if (!memTagFound && userPrompt && strlen(userPrompt) > 10) {
          // Quick check: does the message contain personal pronouns?
          char lowerCheck[256];
          size_t clen = strlen(userPrompt);
          if (clen >= sizeof(lowerCheck)) clen = sizeof(lowerCheck) - 1;
          for (size_t ci = 0; ci < clen; ci++) lowerCheck[ci] = tolower((unsigned char)userPrompt[ci]);
          lowerCheck[clen] = '\0';
          if (strstr(lowerCheck, " i ") || strstr(lowerCheck, "my ") || strstr(lowerCheck, "me ") ||
              strncmp(lowerCheck, "i ", 2) == 0 || strncmp(lowerCheck, "my", 2) == 0) {
            strncpy(pendingExtractMsg, userPrompt, sizeof(pendingExtractMsg) - 1);
            pendingExtractMsg[sizeof(pendingExtractMsg) - 1] = '\0';
            pendingMemoryExtract = true;
            LOGI("MEM","Deferred extraction for: %.40s...", userPrompt);
          }
        }

        if (returnMode == GAME_RPG && !handledRpgReply && !handledSummaryReply) {
          if (strchr(msgStart, '|')) parseRpgPayload(msgStart);
          else {
            copySafeText(rpgStory, sizeof(rpgStory), (strlen(msgStart) > 0) ? msgStart : "The quest continues.");
            for(int i=0; i<3; i++) copySafeText(rpgChoices[i], sizeof(rpgChoices[i]), "...");
          }
          handledRpgReply = true;
        }
        
        if (handledRpgReply) ensureRpgChoicesPlayable();

        // STABILITY FIX: Use global workspace (1.2KB) instead of stack buffer to prevent Stack Overflow
        copySafeText(workspace, 512, msgStart);
        // Strip leading "EMOTIONNAME:" or "EMOTIONNAME " prefix from display text
        {
          const char* emoNames[] = {"NEUTRAL", "HAPPY", "SURPRISED", "SAD", "ANGRY", "THINKING_FACE",
            "SLEEPY", "FLUSTERED", "LAUGHING", "WINK", "CONFUSED", "LOVE", "SASSY",
            "SHOCKED", "SAD_EMBARRASSED", "SHY", "TEASING", "BLUSHING"};
          for (int i = 0; i < 18; i++) {
            size_t nlen = strlen(emoNames[i]);
            if (strncmp(workspace, emoNames[i], nlen) == 0) {
              char next = workspace[nlen];
              if (next == ':' || next == ' ') { memmove(workspace, workspace + nlen + (next == ':' ? 1 : 0), strlen(workspace + nlen + (next == ':' ? 1 : 0)) + 1); break; }
            }
          }
        }
        char* bStart;
        while ((bStart = strchr(workspace, '[')) != nullptr) {
          yield(); // Process WiFi background tasks during string manipulation
          char* bEnd = strchr(bStart, ']');
          if (bEnd) {
            memmove(bStart, bEnd + 1, strlen(bEnd + 1) + 1);
            // Clean up double spaces left behind by stripped tags
            if (bStart > workspace && *(bStart-1) == ' ' && *bStart == ' ') memmove(bStart, bStart + 1, strlen(bStart + 1) + 1);
          } else break;
        }
        // Strip any plain-text MEM+: and MEM: prefixes the AI wrote without brackets
        {
          char* mp;
          while ((mp = strstr(workspace, "MEM+:")) != nullptr) {
            memmove(mp, mp + 5, strlen(mp + 5) + 1);
            if (mp > workspace && *(mp-1) == ' ' && *mp == ' ') memmove(mp, mp + 1, strlen(mp + 1) + 1);
          }
          while ((mp = strstr(workspace, "MEM:")) != nullptr) {
            memmove(mp, mp + 4, strlen(mp + 4) + 1);
            if (mp > workspace && *(mp-1) == ' ' && *mp == ' ') memmove(mp, mp + 1, strlen(mp + 1) + 1);
          }
        }
        char* finalDisplayPtr = workspace;
        while (*finalDisplayPtr == ' ') finalDisplayPtr++;
        trimTrailingSpacesInPlace(finalDisplayPtr);

        // Guard against empty reply (e.g. AI returned only a bracket tag)
        if (strlen(finalDisplayPtr) == 0) {
          strcpy(finalDisplayPtr, "...");
        }

        // Log AI's reply to history so she remembers her own jokes/statements
        if (!handledRpgReply && !handledSummaryReply) {
          logLocalChat("Yuki", finalDisplayPtr);
          Serial.print("[DEBUG] Yuki raw: "); Serial.println(responseText);
          Serial.print("[DEBUG] Yuki display: "); Serial.println(finalDisplayPtr);
        }

        emotionSetTime = millis();

        if (!handledRpgReply && !handledSummaryReply) copySafeText(aiMsg, sizeof(aiMsg), finalDisplayPtr);
        else if (handledRpgReply) copySafeText(aiMsg, sizeof(aiMsg), "Quest updated.");
        } // end if (!intentHandled) — affinity/memory/tag-stripping block

        // Queue reply for spoken output (TTS relay)
        // Skip TTS for intent responses — the follow-up in main loop will speak
        if (pendingIntentFollowUp) {
          // Don't queue yet — main loop follow-up will speak the real answer
        } else if (speak && !handledRpgReply && !handledSummaryReply) {
          ttsQueue(aiMsg);
        }

        // Skip heavy response / self-reflection / exchange-tone for intent responses
        // (recursive syncAI already handled all of that)
        if (!intentHandled) {
        // Feature 3 & 7: Detect heavy responses — store as concern and trigger bad day
        if (!handledRpgReply && !handledSummaryReply && isPersonal) {
          if (messageFeelsHeavy(aiMsg)) {
            strncpy(core_lastConcern, aiMsg, sizeof(core_lastConcern) - 1);
            core_lastConcern[sizeof(core_lastConcern) - 1] = '\0';
            if (random(0, 2) == 0) core_badDayFlag = true; // 50% chance heavy msg triggers bad day
          }
        }

        if (selfReflectionFired) {
          copySafeText(core_selfReflection, sizeof(core_selfReflection), aiMsg);
          lastSelfReflection = millis();
          pendingMemorySave = true;
        }

        // Set core_lastExchangeTone based on currentEmotion
        if (!handledRpgReply && !handledSummaryReply && returnMode == FACE) {
          if (currentEmotion == HAPPY || currentEmotion == LAUGHING || currentEmotion == LOVE || currentEmotion == WINK)
            copySafeText(core_lastExchangeTone, sizeof(core_lastExchangeTone), "warm");
          else if (currentEmotion == SAD || currentEmotion == SAD_EMBARRASSED)
            copySafeText(core_lastExchangeTone, sizeof(core_lastExchangeTone), "sad");
          else if (currentEmotion == ANGRY)
            copySafeText(core_lastExchangeTone, sizeof(core_lastExchangeTone), "tense");
          else if (currentEmotion == LAUGHING || currentEmotion == TEASING)
            copySafeText(core_lastExchangeTone, sizeof(core_lastExchangeTone), "playful");
          else
            copySafeText(core_lastExchangeTone, sizeof(core_lastExchangeTone), "neutral");
          pendingCloudSync = true;
        }

        // Play LLM-driven [SND:] tag, or emotion fallback (skip fallback when TTS will speak)
        if (strstr(aiMsg, "LEVEL UP!") == NULL) {
          heavyOpCooldown();
          if (!sndPlayed && !speak) {
            LOGI("SND","Fallback to emotion sound: %d", (int)currentEmotion);
            switch (currentEmotion) {
              case HAPPY:     sound_happy();     break;
              case SAD:       sound_sad();       break;
              case ANGRY:     sound_angry();     break;
              case SURPRISED: sound_surprised(); break;
              case LAUGHING:  sound_laughing();  break;
              case SLEEPY:    sound_sleepy();    break;
              case SASSY:     sound_sassy();     break;
              case CONFUSED:  sound_confused();  break;
              case FLUSTERED: sound_flustered(); break;
              case WINK:      sound_wink();      break;
              case BLUSHING:  sound_blushing();  break;
              case SHY:       sound_shy();       break;
              case TEASING:   sound_teasing();   break;
              case LOVE:      sound_heartbeat(); break;
              case SHOCKED:   sound_surprised(); break;
              case SAD_EMBARRASSED: sound_flustered(); break;
              default:        sound_ai_reply();  break;
            }
          }
        }
        } // end if (!intentHandled) — skip post-processing for intent responses
      } else {
        JsonVariant errorVariant = (*docResp)["error"]["message"];
        if (!errorVariant.isNull()) {
          const char* errStr = errorVariant.as<const char*>();
          snprintf(aiMsg, sizeof(aiMsg), "API Err: %s", errStr ? errStr : "unknown");
          currentEmotion = ANGRY; sound_angry();
        } else strcpy(aiMsg, "No text...");
      }
      }
    
    if (strcmp(prompt, "independent_thought") == 0) gainXP(5); else gainXP(10);
      if (strstr(aiMsg, "LEVEL UP!") == NULL) {
        heavyOpCooldown();
      }
    delete docResp;
    // Cooldown before saveSys that may follow
    heavyOpCooldown();
  } else {
    LOGW("NET","HTTP POST failed");
    snprintf(aiMsg, sizeof(aiMsg), "Error %d", httpCode);
    wifiFailCount++; // Proxy for low voltage: repeated failures → throttle
    currentEmotion = ANGRY; // Frustrated with network failure
    emotionSetTime = millis();
    scrollOffset = 0;
    sound_angry();
    // Feature 7: Network failures contribute to a bad day
    if (random(0, 3) == 0) core_badDayFlag = true; // 33% chance
  }
  http.end(); 
  client.flush(); // Ensure all internal buffers are cleared
  client.stop(); // FIX: Explicitly close the Secure Client to free up the network stack immediately
  
  if (httpCode > 0 && triggerSilenceMqtt) {
    sendMessage(PHONE_CONTACT_ID, aiMsg);
  }
  
  lastIdleCheck = millis(); // Reset the idle action scheduler timer
  currentMode = returnMode;
  if (returnMode == FACE) statusBarVisibleUntil = millis() + 3000;
  lastIdleAction = millis();
}
