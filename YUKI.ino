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
#include <ESP32Ping.h>

// Include our new modules
#include "secrets.h"
#include "config.h"
#include "debug.h"
#include "memory_v2.h"

// RTC memory for shutdown flag (persists across restarts)
RTC_DATA_ATTR int shutdownFlag = 0;

Mode currentMode = FACE;
PersonalityMode sessionMode;

struct Config sys;

// Volatile conversation-test mode, controlled from the serial monitor.
// It never changes the persisted baseline and is disabled after reboot.
volatile bool conversationTestMode = false;
int conversationTestSavedAffinity = 0;
bool conversationTestTurnInProgress = false;

// --- GLOBAL VARIABLES ---
char workspace[4096]; // Bounded chat context and AI request payload
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
unsigned long pendingResponseAt = 0;
bool pendingResponseFailureNotice = false;
char pendingResponseFailureRecipient[32] = "";
char pendingResponseFailureText[160] = "";
uint32_t foregroundTurnSequence = 0;
uint32_t activeForegroundTurnId = 0;
bool pendingLevelUpAck = false;
char responsePrompt[256] = ""; // Buffer to hold the incoming message
bool pendingGeocode = false;   // Flag to trigger Location Search
char pendingGeocodeCity[64] = "";
char pendingGeocodeSender[32] = "";

bool pendingCloudSync = false; // Deferred LittleFS/MQTT sync flag
bool pendingMemorySave = false; // New: Deferred Memory.json save
bool pendingMemoryExtract = false; // Deferred memory extraction from user message
bool pendingMemoryConsolidation = false; // Deferred memory consolidation during sleep
unsigned long lastConsolidation = 0; // Timestamp of last memory consolidation
bool localMemoryLoaded = false; // Track if local memory loaded successfully (for cloud merge)
static SemaphoreHandle_t coreMemorySaveMutex = NULL;

// --- BACKGROUND MEMORY EXTRACTION TASK ---
TaskHandle_t memExtractTaskHandle = NULL;
SemaphoreHandle_t memExtractMutex = NULL;  // Protects shared journal and structured memory
SemaphoreHandle_t memoryWorkMutex = NULL;  // Prevents extraction TLS from overlapping AI chat TLS
#define MEM_EXTRACT_QUEUE_SIZE 8
#define MEM_EXTRACT_MIN_HEAP 64000
// The worker already owns its stack from heap; allow its idle floor to be
// lower than the pre-start threshold so it does not wait on memory it reserved.
#define MEM_EXTRACT_WORKER_MIN_HEAP 56000
#define MEM_EXTRACT_IDLE_GAP_MS 60000UL
static char memExtractQueue[MEM_EXTRACT_QUEUE_SIZE][1152] = {};
static uint32_t memExtractQueueEnqueuedAt[MEM_EXTRACT_QUEUE_SIZE] = {};
static uint8_t memExtractQueueHead = 0;
static uint8_t memExtractQueueTail = 0;
static uint8_t memExtractQueueCount = 0;
static volatile uint32_t memExtractNotBefore = 0;
extern volatile bool ttsPlaying;
extern volatile bool ttsPendingSpeak;
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
char conversationSessionNotes[CONVERSATION_SESSION_NOTES_CAPACITY] = "";
volatile bool memoryFictionQuarantined = false; // RAM-only scene quarantine; cleared only at an explicit boundary/reboot
volatile bool memoryFictionBoundaryPending = false;
char pendingDurableMemoryCandidate[MEMORY_V2_FACT_LEN] = "";
uint32_t pendingDurableMemoryCandidateAt = 0;
bool pendingDurableMemoryConfirmationAsked = false;
portMUX_TYPE pendingDurableMemoryCandidateMux = portMUX_INITIALIZER_UNLOCKED;
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
unsigned long lastPhysicalInteraction = 0;

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
bool presenceInitialized = false;
uint8_t presenceReachableSamples = 0;
int8_t pendingPresenceReaction = 0;
unsigned long pendingPresenceReactionAt = 0;
static const unsigned long PRESENCE_REACTION_MAX_AGE = 300000UL;

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
unsigned long nextPulseInterval = 40000;
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
unsigned long pendingReturnAbsenceHours = 0;

// Forward declarations for Yuki sleep functions
void enterYukiSleep();
void wakeYukiSleep();
void exitGroggy();
void syncAI(const char* prompt, bool isPersonal, Mode returnMode, bool bypassCooldown, bool speak);

bool setConversationTestMode(bool enabled) {
  if (enabled == conversationTestMode) return true;
  if (enabled) {
    if (!memoryWorkMutex || xSemaphoreTake(memoryWorkMutex, pdMS_TO_TICKS(20000)) != pdTRUE) {
      LOGW("TEST", "Could not enter conversation test mode: memory worker did not reach a safe boundary");
      Serial.println("YUKI_TEST=ON refused; memory worker busy. Retry after it finishes.");
      return false;
    }
    conversationTestSavedAffinity = sys.affinity;
    sys.affinity = 0;
    conversationTestMode = true;
    LOGI("TEST", "Conversation test mode ON; affinity baseline=%d controlled=0; persistence, idle dialogue, and sleep suspended", conversationTestSavedAffinity);
    Serial.printf("YUKI_TEST=ON affinity_baseline=%d affinity=0 facts=%u revision=%lu heap=%u; send YUKI_TEST OFF to exit\n",
                  conversationTestSavedAffinity, (unsigned)memoryV2GetFactCount(),
                  (unsigned long)memoryV2GetRevision(), ESP.getFreeHeap());
    xSemaphoreGive(memoryWorkMutex);
  } else {
    conversationTestMode = false;
    conversationTestTurnInProgress = false;
    sys.affinity = conversationTestSavedAffinity;
    LOGI("TEST", "Conversation test mode OFF; restored RAM affinity=%d", sys.affinity);
    Serial.printf("YUKI_TEST=OFF affinity restored to %d\n", sys.affinity);
  }
  return true;
}

static void pollConversationTestCommand() {
  static char command[40];
  static uint8_t used = 0;
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\r' || c == '\n') {
      command[used] = '\0';
      if (strcmp(command, "YUKI_TEST ON") == 0) setConversationTestMode(true);
      else if (strcmp(command, "YUKI_TEST OFF") == 0) setConversationTestMode(false);
      else if (strcmp(command, "YUKI_TEST STATUS") == 0) {
        Serial.printf("YUKI_TEST=%s affinity=%d emotion=%d facts=%u revision=%lu heap=%u\n",
                      conversationTestMode ? "ON" : "OFF", sys.affinity,
                      (int)currentEmotion,
                      (unsigned)memoryV2GetFactCount(),
                      (unsigned long)memoryV2GetRevision(), ESP.getFreeHeap());
      }
      used = 0;
    } else if (used + 1 < sizeof(command) && c >= 32 && c <= 126) {
      command[used++] = c;
    } else if (used + 1 >= sizeof(command)) {
      used = 0;
    }
  }
}

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

// --- LIGHTWEIGHT MEMORY EXTRACTION (BACKGROUND TASK) ---
// Runs in a FreeRTOS task so SSL handshake on weak WiFi doesn't block the main loop.
// The task waits on a notification; requestMemoryExtract() signals it.

#define MEM_EXTRACT_TASK_STACK 8192
#define MEM_EXTRACT_TASK_PRIO   1

static void memExtractWorker(void *arg) {
  (void)arg;
  static char retryingJob[1152] = "";
  static uint8_t jobRetryCount = 0;
  for (;;) {
    ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
    if (conversationTestMode || memoryFictionQuarantined || memoryFictionBoundaryPending) {
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000));
      continue;
    }
    static char jobMsg[1152] = "";
    uint32_t jobEnqueuedAt = 0;
    jobMsg[0] = '\0';
    if (!memExtractMutex || xSemaphoreTake(memExtractMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
      LOGW("MEM", "Queue mutex timeout");
      vTaskDelay(pdMS_TO_TICKS(1000));
      xTaskNotifyGive(memExtractTaskHandle);
      continue;
    }
    if (memExtractQueueCount > 0) {
      strncpy(jobMsg, memExtractQueue[memExtractQueueTail], sizeof(jobMsg) - 1);
      jobMsg[sizeof(jobMsg) - 1] = '\0';
      jobEnqueuedAt = memExtractQueueEnqueuedAt[memExtractQueueTail];
    }
    xSemaphoreGive(memExtractMutex);

    if (strlen(jobMsg) == 0) continue;
    if (WiFi.status() != WL_CONNECTED) {
      LOGW("MEM", "Extraction held in queue: no WiFi");
      vTaskDelay(pdMS_TO_TICKS(5000));
      xTaskNotifyGive(memExtractTaskHandle);
      continue;
    }

    // Extraction is optional background work. Keep its TLS/JSON allocations out of
    // the foreground AI reply and TTS window, where transient heap pressure is high.
    uint32_t waitStarted = millis();
    uint8_t safeHeapSamples = 0;
    while ((int32_t)(millis() - memExtractNotBefore) < 0 || pendingResponse ||
           activeForegroundTurnId != 0 || millis() - lastInteraction < MEM_EXTRACT_IDLE_GAP_MS ||
           ttsPlaying || ttsPendingSpeak ||
           ESP.getFreeHeap() < MEM_EXTRACT_WORKER_MIN_HEAP) {
      // If a new interaction arrives while this worker is waiting between
      // queued jobs, drop its task stack immediately. The queued job remains
      // intact and the main loop can recreate the worker after the next quiet
      // window. Keeping this 8 KB stack alive during chat reduces headroom for
      // the foreground TLS response parser.
      if (pendingResponse || activeForegroundTurnId != 0 ||
          millis() - lastInteraction < MEM_EXTRACT_IDLE_GAP_MS ||
          ttsPlaying || ttsPendingSpeak) {
        bool workerDetached = false;
        if (memExtractMutex &&
            xSemaphoreTake(memExtractMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
          if (memExtractTaskHandle == xTaskGetCurrentTaskHandle()) {
            memExtractTaskHandle = NULL;
            workerDetached = true;
          }
          xSemaphoreGive(memExtractMutex);
        }
        if (workerDetached) {
          LOGI("MEM", "Foreground activity; queued job retained and worker stopped to release stack");
          vTaskDelete(NULL);
        }
        LOGW("MEM", "Foreground activity detected; extraction worker could not detach safely");
        vTaskDelay(pdMS_TO_TICKS(500));
        continue;
      }
      safeHeapSamples = 0;
      if (millis() - waitStarted >= 120000) {
        LOGW("MEM", "No safe extraction window (heap=%u); oldest job remains queued",
             ESP.getFreeHeap());
        vTaskDelay(pdMS_TO_TICKS(5000));
        xTaskNotifyGive(memExtractTaskHandle);
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(500));
    }
    if (millis() - waitStarted >= 120000) continue;
    // Require a short stable window, not a single heap sample between allocations.
    while (safeHeapSamples < 3) {
      if (!pendingResponse && activeForegroundTurnId == 0 &&
          millis() - lastInteraction >= MEM_EXTRACT_IDLE_GAP_MS &&
          !ttsPlaying && !ttsPendingSpeak &&
          ESP.getFreeHeap() >= MEM_EXTRACT_WORKER_MIN_HEAP &&
          (int32_t)(millis() - memExtractNotBefore) >= 0) {
        safeHeapSamples++;
      } else {
        safeHeapSamples = 0;
      }
      if (millis() - waitStarted >= 120000) break;
      vTaskDelay(pdMS_TO_TICKS(300));
    }
    if (safeHeapSamples < 3) {
      LOGW("MEM", "No stable extraction window (heap=%u); oldest job remains queued",
           ESP.getFreeHeap());
      vTaskDelay(pdMS_TO_TICKS(5000));
      xTaskNotifyGive(memExtractTaskHandle);
      continue;
    }

    heavyOpCooldown();
    LOGI("MEM","Running extraction for: %.40s (heap=%u queue=%u oldest_age_ms=%lu)",
         jobMsg, ESP.getFreeHeap(), memExtractQueueCount,
         (unsigned long)(millis() - jobEnqueuedAt));

    char authHeader[128];
    snprintf(authHeader, sizeof(authHeader), "Bearer %s", API_KEY);

    static char extractPrompt[2304];
    snprintf(extractPrompt, sizeof(extractPrompt),
      "Classify a personal detail from the CURRENT USER MESSAGE only. Recent dialogue is context only for fiction, referents, and corrections. "
      "Use recent conversation only to tell whether the message is fictional/roleplay, who a detail refers to, or whether it corrects an earlier statement. "
      "Never save facts about a fictional character or hypothetical scenario as facts about the real user. "
      "If the detail is fictional, quoted, about someone else, or its referent is unclear, output NONE. "
      "Do not save one-off completed events such as a single class, presentation, errand, purchase, or meal unless the user explicitly says it is recurring, ongoing, or important to remember. "
      "Output exactly one tag: [MEM+: h: fact], [MEM+: LOW: fact], [MEM?: h: fact], [MEM?: LOW: fact], or [MEM+: NONE]. "
      "Use MEM+ only for a clear, explicit, lasting fact, preference, ongoing context, or correction that should be remembered without asking. "
      "Use MEM? only when the detail seems potentially useful but the user has not made clear that it is lasting or worth retaining; it will be held temporarily and Yuki will ask permission. "
      "Prefer NONE over MEM? when the detail is trivial, fleeting, or unlikely to help later. "
      "Use h for identity or an explicit stable preference. Use LOW for a stable interest, habit, or ongoing context. "
      "Use NONE for temporary moods, passing remarks, one-time plans, questions, greetings, fictional content, or details that are not useful later. "
      "Write a concise, self-contained fact about the user (for example, 'h: User studies computer science'). "
      "Do not repeat the user's first-person wording after adding 'User ...'. Never infer a preference from one casual mention, invent details, or output the placeholder word 'fact'. "
      "Never turn an assistant statement or a character's detail into a user fact. "
      "Input: %s",
      jobMsg);

    static StaticJsonDocument<4096> doc;
    static char body[4096];
    doc.clear();
    // Keep extraction aligned with the model selected for foreground chat.
    doc["model"] = sys.currentModel;
    doc["temperature"] = 0.1;
    doc["reasoning_effort"] = "low";
    doc["max_tokens"] = 64;
    JsonArray messages = doc.createNestedArray("messages");
    JsonObject sysMsg = messages.createNestedObject();
    sysMsg["role"] = "system";
    sysMsg["content"] = "Classify only the CURRENT USER MESSAGE; recent conversation is context for fiction, referents, and corrections. Never save fictional, quoted, or other people's facts about the user. MEM+ is only for clear explicit lasting facts; MEM? is for potentially useful but ambiguous durability and requires user confirmation; NONE for temporary, trivial, fictional, or uncertain referents. Output exactly one of [MEM+: h: fact], [MEM+: LOW: fact], [MEM?: h: fact], [MEM?: LOW: fact], [MEM+: NONE].";
    JsonObject userMsg = messages.createNestedObject();
    userMsg["role"] = "user";
    userMsg["content"] = extractPrompt;

    size_t expectedBodyLength = measureJson(doc);
    size_t bodyLength = expectedBodyLength < sizeof(body)
        ? serializeJson(doc, body, sizeof(body))
        : 0;
    if (bodyLength == 0 || bodyLength != expectedBodyLength) {
      LOGE("MEM", "Could not serialize extraction request; oldest job remains queued");
      vTaskDelay(pdMS_TO_TICKS(5000));
      xTaskNotifyGive(memExtractTaskHandle);
      continue;
    }

    WiFiClientSecure extractClient;
    extractClient.setCACert(NULL);
    extractClient.setInsecure();
    extractClient.setTimeout(7000);
    extractClient.setHandshakeTimeout(7);
    HTTPClient http;

    if (!memoryWorkMutex ||
        xSemaphoreTake(memoryWorkMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
      LOGW("MEM", "AI network busy; oldest extraction remains queued");
      vTaskDelay(pdMS_TO_TICKS(5000));
      xTaskNotifyGive(memExtractTaskHandle);
      continue;
    }
    LOGI("MEM", "Extraction acquired AI network mutex");
    if (pendingResponse || activeForegroundTurnId != 0 ||
        millis() - lastInteraction < MEM_EXTRACT_IDLE_GAP_MS || ttsPlaying ||
        ttsPendingSpeak || ESP.getFreeHeap() < 40000) {
      xSemaphoreGive(memoryWorkMutex);
      LOGW("MEM", "Extraction window closed before HTTP (heap=%u); oldest job remains queued",
           ESP.getFreeHeap());
      vTaskDelay(pdMS_TO_TICKS(5000));
      xTaskNotifyGive(memExtractTaskHandle);
      continue;
    }
    if (conversationTestMode || memoryFictionQuarantined || memoryFictionBoundaryPending) {
      xSemaphoreGive(memoryWorkMutex);
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000));
      continue;
    }

    bool httpReady = http.begin(extractClient, "https://api.groq.com/openai/v1/chat/completions");
    http.setTimeout(7000);
    http.setConnectTimeout(3000);
    // Foreground chat disables keep-alive because it has caused connection
    // refused (-1) errors on this device; use the same fresh-connection policy.
    http.setReuse(false);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", authHeader);
    uint32_t extractionStartedAt = millis();
    LOGI("MEM", "Extraction HTTP begin ready=%d model=%s timeout=7000ms payload=%u wifi=%d rssi=%d heap=%u",
         httpReady, sys.currentModel, (unsigned)bodyLength, (int)WiFi.status(),
         WiFi.RSSI(), ESP.getFreeHeap());
    int httpCode = httpReady ? http.POST((uint8_t*)body, bodyLength) : -1;
    char tlsError[96] = {};
    int tlsErrorCode = 0;
    if (httpCode <= 0) tlsErrorCode = extractClient.lastError(tlsError, sizeof(tlsError));
    doc.clear();
    body[0] = '\0';
    LOGI("MEM", "Extraction task stack high-water=%u",
         (unsigned)uxTaskGetStackHighWaterMark(NULL));
    LOGI("MEM", "Extraction HTTP status: %d elapsed_ms=%lu model=%s tls_err=%d '%s'",
         httpCode, (unsigned long)(millis() - extractionStartedAt), sys.currentModel,
         tlsErrorCode, tlsError);
    if (conversationTestMode || memoryFictionQuarantined || memoryFictionBoundaryPending) {
      if (conversationTestMode)
        LOGI("TEST", "MEM_SUPPRESS reason=test_mode path=inflight_extraction_result; job retained");
      else
        LOGI("MEM", "MEM_SUPPRESS reason=fiction_quarantine path=inflight_extraction_result; job retained");
      http.end();
      xSemaphoreGive(memoryWorkMutex);
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000));
      continue;
    }
    if (httpCode > 0 && httpCode == 200) {
      static StaticJsonDocument<200> responseFilter;
      static StaticJsonDocument<512> respDoc;
      responseFilter.clear();
      responseFilter["choices"][0]["message"]["content"] = true;
      responseFilter["choices"][0]["message"]["reasoning_content"] = true;
      responseFilter["choices"][0]["text"] = true;
      responseFilter["error"]["message"] = true;
      respDoc.clear();
      Stream& responseStream = http.getStream();
      DeserializationError jsonErr =
          deserializeJson(respDoc, responseStream, DeserializationOption::Filter(responseFilter));
      if (jsonErr) {
        LOGW("MEM","JSON stream parse error: %s", jsonErr.c_str());
      } else {
        const char* content = respDoc["choices"][0]["message"]["content"];
        if (!content || strlen(content) == 0) {
          content = respDoc["choices"][0]["message"]["reasoning_content"] | "";
          if (!content || strlen(content) == 0) content = respDoc["choices"][0]["text"] | "";
        }
        LOGI("MEM","Extraction raw: '%s' (model=%s)", content ? content : "(null)", sys.currentModel);
        if (content && strlen(content) > 0) {
          if (strstr(content, "NONE")) {
            LOGI("MEM","Extraction returned NONE for: %.40s", jobMsg);
          }
          const char* candidateTag = strstr(content, "[MEM?:");
          const char* tag = candidateTag ? candidateTag : strstr(content, "[MEM+:");
          if (tag) {
            const char* start = tag + 6;
            const char* end = strchr(start, ']');
            // Guard: reject if another tag's bracket leaked in (malformed tag)
            if (end && memchr(start, '[', end - start) != NULL) end = NULL;
            if (end && (end - start) > 4) {
              int factLen = end - start;
              if (factLen < MEMORY_V2_FACT_LEN) {
                char fact[MEMORY_V2_FACT_LEN];
                strncpy(fact, start, factLen);
                fact[factLen] = '\0';
                char* factStart = fact;
                while (*factStart == ' ' || *factStart == '\t') factStart++;
                if (factStart != fact) memmove(fact, factStart, strlen(factStart) + 1);
                bool isNone = (strcmp(fact, "NONE") == 0);
                bool hasValidKind = strncmp(fact, "h: ", 3) == 0 || strncmp(fact, "LOW: ", 5) == 0;
                bool isValid = !isNone && hasValidKind && memoryV2UsableText(fact);
                if (candidateTag && isValid) {
                  bool candidateStored = false;
                  bool candidateOccupied = false;
                  portENTER_CRITICAL(&pendingDurableMemoryCandidateMux);
                  if (pendingDurableMemoryCandidate[0] &&
                      (uint32_t)(millis() - pendingDurableMemoryCandidateAt) >= 30UL * 60UL * 1000UL) {
                    pendingDurableMemoryCandidate[0] = '\0';
                    pendingDurableMemoryConfirmationAsked = false;
                  }
                  if (!pendingDurableMemoryCandidate[0]) {
                    strncpy(pendingDurableMemoryCandidate, fact, sizeof(pendingDurableMemoryCandidate) - 1);
                    pendingDurableMemoryCandidate[sizeof(pendingDurableMemoryCandidate) - 1] = '\0';
                    pendingDurableMemoryCandidateAt = millis();
                    pendingDurableMemoryConfirmationAsked = false;
                    candidateStored = true;
                  } else {
                    candidateOccupied = true;
                  }
                  portEXIT_CRITICAL(&pendingDurableMemoryCandidateMux);
                  if (candidateStored) LOGI("MEM", "Confirmation candidate queued (RAM only): %.70s", fact);
                  else if (candidateOccupied) LOGI("MEM", "Confirmation candidate not queued: one is already pending");
                } else if (isValid) {
                  memoryV2ObserveFact(fact);
                  if (xSemaphoreTake(memExtractMutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
                    pendingCloudSync = true;
                    if (!strstr(core_chatSummary, fact)) {
                      int curLen = strlen(core_chatSummary);
                      if (curLen + strlen(fact) + 3 < 1023) {
                        if (curLen > 0) strncat(core_chatSummary, " | ", 1023 - curLen - 1);
                        strncat(core_chatSummary, fact, 1023 - strlen(core_chatSummary) - 1);
                        saveCoreMemory();
                        LOGI("MEM","LLM extracted: %s", fact);
                      } else {
                        LOGW("MEM", "Legacy memory journal full; structured fact still queued for save");
                      }
                    }
                    xSemaphoreGive(memExtractMutex);
                  } else {
                    LOGW("MEM","Extract mutex timeout after structured fact update");
                  }
                } else if (!isNone) {
                  LOGW("MEM", "Extraction rejected malformed/invalid fact tag");
                }
              }
            }
          } else {
            LOGI("MEM","No memory tag in extraction response: %.60s", content);
          }
        }
      }
    } else {
      LOGW("MEM","Extract API failed: %d", httpCode);
    }
    bool retryableFailure = httpCode <= 0 || httpCode == 408 ||
                            httpCode == 429 || httpCode >= 500;
    bool retryJob = false;
    uint32_t retryDelayMs = 30000UL;
    if (retryableFailure) {
      if (strcmp(retryingJob, jobMsg) != 0) {
        strncpy(retryingJob, jobMsg, sizeof(retryingJob) - 1);
        retryingJob[sizeof(retryingJob) - 1] = '\0';
        jobRetryCount = 0;
      }
      retryJob = true;
      if (jobRetryCount < 2) {
        jobRetryCount++;
        LOGW("MEM", "Extraction job retained for retry %u/2 (HTTP=%d)",
             jobRetryCount, httpCode);
      } else {
        // Keep the FIFO head through a full failed retry cycle. Back off before
        // starting another cycle instead of silently losing a real user fact.
        jobRetryCount = 0;
        retryingJob[0] = '\0';
        retryDelayMs = 60000UL;
        LOGW("MEM", "Extraction still unavailable after 3 attempts; job retained, retrying in 60s (HTTP=%d)",
             httpCode);
      }
    } else {
      retryingJob[0] = '\0';
      jobRetryCount = 0;
    }
    bool queueAcknowledged = retryJob;
    if (!retryJob) {
      if (xSemaphoreTake(memExtractMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        if (memExtractQueueCount > 0 &&
            strcmp(memExtractQueue[memExtractQueueTail], jobMsg) == 0) {
          uint8_t completedSlot = memExtractQueueTail;
          memExtractQueueTail = (memExtractQueueTail + 1) % MEM_EXTRACT_QUEUE_SIZE;
          memExtractQueueCount--;
          memExtractQueueEnqueuedAt[completedSlot] = 0;
          queueAcknowledged = true;
          LOGI("MEM", "Extraction job removed from queue");
        } else {
          LOGE("MEM", "Queue head changed unexpectedly; extraction completion not acknowledged");
        }
        xSemaphoreGive(memExtractMutex);
      } else {
        LOGE("MEM", "Queue mutex timeout after extraction; job remains queued");
      }
    }
    http.end();
    xSemaphoreGive(memoryWorkMutex);
    if (retryJob) {
      bool workerDetached = false;
      if (xSemaphoreTake(memExtractMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        memExtractNotBefore = millis() + retryDelayMs;
        if (memExtractTaskHandle == xTaskGetCurrentTaskHandle()) {
          // The queued job survives its retry delay, but the idle worker must
          // release its stack so a later foreground TLS request has the heap.
          memExtractTaskHandle = NULL;
          workerDetached = true;
        }
        xSemaphoreGive(memExtractMutex);
      }
      if (workerDetached) {
        LOGI("MEM", "Retry job retained; stopping worker to release stack for %lu ms",
             (unsigned long)retryDelayMs);
        vTaskDelete(NULL);
      }
      LOGW("MEM", "Could not detach retry worker; keeping queued job and retrying shortly");
      vTaskDelay(pdMS_TO_TICKS(1000));
      if (memExtractTaskHandle) xTaskNotifyGive(memExtractTaskHandle);
      continue;
    }
    if (!queueAcknowledged) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      xTaskNotifyGive(memExtractTaskHandle);
      continue;
    }
    bool moreQueued = false;
    bool queueStateRead = false;
    TaskHandle_t workerToNotify = NULL;
    if (xSemaphoreTake(memExtractMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
      queueStateRead = true;
      moreQueued = memExtractQueueCount > 0;
      if (moreQueued) {
        workerToNotify = memExtractTaskHandle;
      } else if (memExtractTaskHandle == xTaskGetCurrentTaskHandle()) {
        // Releasing an idle worker's stack restores enough heap for deferred
        // local-memory saves and cloud syncs to pass their 64 KB safety gate.
        memExtractTaskHandle = NULL;
      }
      xSemaphoreGive(memExtractMutex);
    }
    if (moreQueued) {
      if (workerToNotify) xTaskNotifyGive(workerToNotify);
    } else if (queueStateRead) {
      LOGI("MEM", "Extraction queue empty; stopping worker to release stack");
      vTaskDelete(NULL);
    } else {
      vTaskDelay(pdMS_TO_TICKS(1000));
      if (memExtractTaskHandle) xTaskNotifyGive(memExtractTaskHandle);
    }
  }
}

// Called from yuki_net.h when AI forgot to tag - copies msg and signals the task
void requestMemoryExtract(const char* msg) {
  if (conversationTestMode) { LOGI("TEST", "MEM_SUPPRESS reason=test_mode path=extract_enqueue"); return; }
  if (memoryFictionQuarantined || memoryFictionBoundaryPending) { LOGI("MEM", "MEM_SUPPRESS reason=fiction_quarantine path=extract_enqueue"); return; }
  if (!msg || strlen(msg) == 0) return;
  if (WiFi.status() != WL_CONNECTED) return;
  if (memoryV2LooksLikeQuestion(msg)) {
    LOGD("MEM", "Extraction skipped: unpunctuated question");
    return;
  }
  // Questions ask for memory; they are not new user facts.
  const char* first = msg;
  while (*first == ' ' || *first == '"' || *first == '\'') first++;
  char lowerMessage[256];
  strncpy(lowerMessage, first, sizeof(lowerMessage) - 1);
  lowerMessage[sizeof(lowerMessage) - 1] = '\0';
  for (char* p = lowerMessage; *p; p++) *p = tolower((unsigned char)*p);
  if (strncmp(lowerMessage, "find ", 5) == 0 || strncmp(lowerMessage, "search ", 7) == 0) {
    LOGD("MEM", "Extraction skipped: lookup command");
    return;
  }
  if (strstr(lowerMessage, "recall") || strstr(lowerMessage, "what do you remember") ||
      strstr(lowerMessage, "what do you know about me") || strstr(lowerMessage, "what you know about me") ||
      strstr(lowerMessage, "all the facts") || strstr(lowerMessage, "all facts") ||
      strstr(lowerMessage, "all these facts") || strstr(lowerMessage, "all this facts") ||
      strstr(lowerMessage, "list all facts") || strstr(lowerMessage, "list the facts") || strstr(lowerMessage, "list facts") ||
      strstr(lowerMessage, "facts i told") || strstr(lowerMessage, "facts i shared") ||
      strstr(lowerMessage, "what i shared") || strstr(lowerMessage, "what i've shared") ||
      strstr(lowerMessage, "what have i shared") || strstr(lowerMessage, "what i told you") ||
      strstr(lowerMessage, "what have i told") || strstr(lowerMessage, "state all the facts")) {
    LOGD("MEM", "Extraction skipped: memory recall request");
    return;
  }
  if (*first == '?' || strstr(first, "?") != NULL ||
      strncasecmp(first, "what ", 5) == 0 || strncasecmp(first, "what's ", 7) == 0 ||
      strncasecmp(first, "what is", 7) == 0 || strncasecmp(first, "whats", 5) == 0 ||
      strncasecmp(first, "who ", 4) == 0 || strncasecmp(first, "which ", 6) == 0 ||
      strncasecmp(first, "do you ", 7) == 0 || strncasecmp(first, "did i ", 6) == 0 ||
      strncasecmp(first, "have i ", 7) == 0 || strncasecmp(first, "am i ", 5) == 0) {
    LOGD("MEM", "Extraction skipped: question");
    return;
  }
  bool startsGreeting = strncmp(lowerMessage, "hello", 5) == 0 ||
                        strncmp(lowerMessage, "hey", 3) == 0 ||
                        strncmp(lowerMessage, "hi ", 3) == 0 ||
                        strcmp(lowerMessage, "hi") == 0;
  bool containsPersonalCue = strstr(lowerMessage, " my ") || strncmp(lowerMessage, "my ", 3) == 0 ||
                             strstr(lowerMessage, " i ") || strncmp(lowerMessage, "i ", 2) == 0 ||
                             strstr(lowerMessage, "i'm") || strstr(lowerMessage, "i am ") ||
                             strstr(lowerMessage, "i like ") || strstr(lowerMessage, "i love ");
  if (strlen(first) < 64 && startsGreeting && !containsPersonalCue) {
    LOGD("MEM", "Extraction skipped: greeting/small talk");
    return;
  }
  if (strlen(first) < 64 &&
      (strstr(lowerMessage, "how are you") || strstr(lowerMessage, "how are u") ||
       strstr(lowerMessage, "how's it going") || strstr(lowerMessage, "hows it going") ||
       strstr(lowerMessage, "good morning") || strstr(lowerMessage, "good afternoon") ||
       strstr(lowerMessage, "good evening")) &&
      !strstr(lowerMessage, " i ") && strncmp(lowerMessage, "i ", 2) != 0 &&
      !strstr(lowerMessage, " my ") && strncmp(lowerMessage, "my ", 3) != 0) {
    LOGD("MEM", "Extraction skipped: greeting/small talk");
    return;
  }
  if (!memExtractMutex) memExtractMutex = xSemaphoreCreateMutex();
  if (!memoryWorkMutex) {
    LOGW("MEM", "Extract skipped: shared AI network mutex unavailable");
    return;
  }
  // Include a small amount of dialogue context so the worker can distinguish
  // real disclosures from details introduced for a fictional scene.
  static char extractionInput[1152];
  int inputUsed = snprintf(extractionInput, sizeof(extractionInput),
                           "CURRENT USER MESSAGE:\n%.220s\nRECENT DIALOGUE:",
                           msg);
  int historyIndices[6];
  int historyCount = 0;
  for (int offset = 2; offset <= MAX_MESSAGES && historyCount < 6; offset++) {
    int idx = (messageIndex + MAX_MESSAGES - offset) % MAX_MESSAGES;
    const char* entry = receivedMessages[idx].content;
    if ((strncmp(entry, "User: ", 6) != 0 && strncmp(entry, "Yuki: ", 6) != 0) || !entry[6]) continue;
    historyIndices[historyCount++] = idx;
  }
  for (int i = historyCount - 1; i >= 0 && inputUsed > 0 &&
       (size_t)inputUsed < sizeof(extractionInput) - 8; i--) {
    const char* entry = receivedMessages[historyIndices[i]].content;
    const char* role = (strncmp(entry, "User: ", 6) == 0) ? "User" : "Yuki";
    int wrote = snprintf(extractionInput + inputUsed, sizeof(extractionInput) - inputUsed,
                         "\n%s: %.100s", role, entry + 6);
    if (wrote <= 0 || (size_t)wrote >= sizeof(extractionInput) - inputUsed) break;
    inputUsed += wrote;
  }

  // Enqueue the message and snapshot the worker handle under the same mutex.
  // This prevents notifying a task handle just as an idle worker exits.
  TaskHandle_t workerToNotify = NULL;
  uint8_t queuedCount = 0;
  if (xSemaphoreTake(memExtractMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    if (memExtractQueueCount >= MEM_EXTRACT_QUEUE_SIZE) {
      xSemaphoreGive(memExtractMutex);
      LOGE("MEM", "Extraction queue full; rejected new job without discarding queued jobs");
      return;
    }
    strncpy(memExtractQueue[memExtractQueueHead], extractionInput, sizeof(memExtractQueue[0]) - 1);
    memExtractQueue[memExtractQueueHead][sizeof(memExtractQueue[0]) - 1] = '\0';
    memExtractQueueEnqueuedAt[memExtractQueueHead] = millis();
    memExtractQueueHead = (memExtractQueueHead + 1) % MEM_EXTRACT_QUEUE_SIZE;
    memExtractQueueCount++;
    queuedCount = memExtractQueueCount;
    uint32_t nextEligibleAt = millis() + MEM_EXTRACT_IDLE_GAP_MS;
    if ((int32_t)(memExtractNotBefore - millis()) <
        (int32_t)MEM_EXTRACT_IDLE_GAP_MS) {
      memExtractNotBefore = nextEligibleAt;
    }
    workerToNotify = memExtractTaskHandle;
    xSemaphoreGive(memExtractMutex);
  } else {
    LOGW("MEM", "Queue mutex timeout while enqueueing");
    return;
  }
  LOGI("MEM","Queued extraction (%u/%u) oldest_age_ms=0: %.40s...",
       queuedCount, MEM_EXTRACT_QUEUE_SIZE, msg);
  if (workerToNotify) {
    xTaskNotifyGive(workerToNotify);
  } else {
    LOGI("MEM", "Extraction queued; worker start deferred until foreground heap recovers");
  }
}


// Memory consolidation: called during sleep to compress journal
// Sends facts to LLM, asks for a compressed summary of what matters
void consolidateMemory() {
  if (conversationTestMode) { LOGI("TEST", "MEM_SUPPRESS reason=test_mode path=consolidation"); return; }
  if (memoryFictionQuarantined || memoryFictionBoundaryPending) { LOGI("MEM", "MEM_SUPPRESS reason=fiction_quarantine path=consolidation"); return; }
  if (strlen(core_chatSummary) < 200) return; // Not enough to consolidate
  if (WiFi.status() != WL_CONNECTED) return;
  if (!canAllocJson(1024)) return;

  LOGI("MEM","Starting consolidation, journal=%u chars", (unsigned)strlen(core_chatSummary));
  lastConsolidation = millis();

  // Build consolidation prompt
  char consolidPrompt[320];
  snprintf(consolidPrompt, sizeof(consolidPrompt),
    "Review these memories. Some are long-term important (identity, name, core relationship facts), "
    "others are minor one-off details. Compress into a short 2-3 sentence summary of what matters most. "
    "Output ONLY [MEM: your summary]. Journal: %s",
    core_chatSummary);

  // Use syncAI with a quiet return — this will update the summary slot via [MEM:] handler
  syncAI(consolidPrompt, false, FACE, true, false);
  LOGI("MEM","Consolidation complete, journal now %u chars", (unsigned)strlen(core_chatSummary));
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
  if (memExtractMutex && xSemaphoreTake(memExtractMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    memExtractQueueHead = 0;
    memExtractQueueTail = 0;
    memExtractQueueCount = 0;
    xSemaphoreGive(memExtractMutex);
  }
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

inline void heapLogAndWarn(const char* tag) {
#if ENABLE_HEAP_LOG
  uint32_t freeh = ESP.getFreeHeap();
  LOGD("HEAP","%s free=%u", tag, freeh);
  // Avoid extra filesystem allocations when the device is already under pressure.
  // Truncate if file exceeds 1KB to prevent filling LittleFS
  File f;
  if (freeh >= HEAP_WARN_THRESHOLD + 8192) f = LittleFS.open("/last_heap.log", "a");
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
    // Free heap can dip temporarily during TLS/TTS. A forced reboot here interrupted
    // healthy requests and created a reset loop; callers already defer large JSON work.
    LOGW("HEAP", "Low heap warning (%u bytes); deferring optional work", freeh);
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
bool pendingWeatherReaction = false;

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

// CRC32 for file integrity (polynomial 0xEDB88320, reflected, no table needed)
static uint32_t crc32Calc(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      crc = (crc >> 1) ^ (0xEDB88320 & (-(int32_t)(crc & 1)));
    }
  }
  return crc ^ 0xFFFFFFFF;
}

// Copy a file (used for backup after atomic write)
static bool copyFile(const char* src, const char* dst) {
  File fIn = LittleFS.open(src, "r");
  if (!fIn) return false;
  File fOut = LittleFS.open(dst, "w");
  if (!fOut) { fIn.close(); return false; }
  while (fIn.available()) {
    uint8_t buf[128];
    size_t n = fIn.read(buf, sizeof(buf));
    if (fOut.write(buf, n) != n) { fIn.close(); fOut.close(); return false; }
  }
  fIn.close();
  fOut.close();
  return true;
}

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
  // Let LittleFS replace the destination in its rename operation. Removing
  // the old file first creates a power-loss window with no valid primary copy.
  bool ok = LittleFS.rename(tmpPath, path);
  if (!ok) {
    LOGE("FS","AtomicWrite: rename failed for %s; previous file preserved", path);
    LittleFS.remove(tmpPath);
  }
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

// Importance-aware memory eviction: prefer removing LOW facts over h facts
void evictMemoryFacts(char* journal, size_t maxLen) {
  if (!journal) return;
  size_t len = strlen(journal);
  if (len <= maxLen) return; // No eviction needed

  // Split facts by " | " delimiter
  // Strategy: evict LOW-priority facts first (oldest first), then h facts if needed
  size_t targetLen = maxLen - 20; // Leave some headroom

  // Pass 0: evict LOW facts; if still over, capString keeps the end
  static char reduced[1024];
  static char ebuf[1024];

  // Rebuild by scanning facts left to right, dropping LOW ones
  reduced[0] = '\0';
  bool first = true;
  bool evictedSomething = false;

  strncpy(ebuf, journal, sizeof(ebuf) - 1);
  ebuf[sizeof(ebuf) - 1] = '\0';

  char* token = strtok(ebuf, "|");
  while (token) {
    while (*token == ' ') token++;

    bool isLow = (strncmp(token, "LOW:", 4) == 0 || strncmp(token, "low:", 4) == 0);
    if (isLow && strlen(journal) > targetLen) {
      evictedSomething = true;
    } else {
      if (!first) strncat(reduced, " | ", sizeof(reduced) - strlen(reduced) - 1);
      strncat(reduced, token, sizeof(reduced) - strlen(reduced) - 1);
      first = false;
    }
    token = strtok(NULL, "|");
  }

  if (evictedSomething) {
    strncpy(journal, reduced, maxLen);
    journal[maxLen] = '\0';
    LOGI("MEM","Evicted LOW facts, journal now %u chars", (unsigned)strlen(journal));
  }

  // Final fallback: if still over limit, use capString (keep end)
  len = strlen(journal);
  if (len > maxLen) {
    size_t keep = maxLen;
    memmove(journal, journal + len - keep, keep);
    journal[keep] = '\0';
    LOGI("MEM","Hard-capped journal to %u chars", (unsigned)keep);
  }
}

// Apply all string caps
void enforceStringCaps() {
  evictMemoryFacts(core_chatSummary, CHAT_SUMMARY_MAX);
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
    LOGW("FS","CRITICAL: Only %.1f%% free (%u/%u bytes) - preserving saved data", freePct, freeBytes, totalBytes);
    LOGW("FS", "Preserving saved data; removing only expendable scan cache");
    if (LittleFS.exists("/scans.json")) LittleFS.remove("/scans.json");
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

static void acknowledgePhysicalWake() {
  lastPhysicalInteraction = millis();
  lastInteraction = millis();
  currentEmotion = SLEEPY;
  emotionSetTime = millis();
  static const char* const wakeReplies[] = {"Mmm? I'm up.", "Oh—I'm awake.", "Give me a second... hi."};
  strncpy(aiMsg, wakeReplies[random(0, 3)], sizeof(aiMsg) - 1);
  aiMsg[sizeof(aiMsg) - 1] = '\0';
  scrollOffset = 0;
  logLocalChat("Event", "The user woke Yuki with a physical control.");
  if (sys.soundOn && sys.voiceOn && random(3) == 0) ttsQueue(aiMsg);
}

// --- YUKI SLEEP FUNCTION DEFINITIONS ---
void enterYukiSleep() {
  // Trigger memory consolidation if journal has enough content and it's been a while
  if (strlen(core_chatSummary) > 200 && millis() - lastConsolidation > 3600000UL) {
    pendingMemoryConsolidation = true;
  }
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
  if (conversationTestMode) { LOGI("TEST", "PERSIST_SUPPRESS reason=test_mode path=sys_save"); return; }
  if (!canAllocJson(2048)) { heapLogAndWarn("saveSys_skip"); return; }
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
  doc["mdl"] = sys.currentModel;
  // CRC32: serialize with placeholder, compute, re-serialize with real value
  doc["crc"] = (uint32_t)0;
  size_t p = serializeJson(doc, workspace, sizeof(workspace));
  uint32_t crc = crc32Calc((const uint8_t*)workspace, p);
  doc["crc"] = crc;
  p = serializeJson(doc, workspace, sizeof(workspace));
  if (atomicWriteFile("/sys.json", workspace, p)) {
    copyFile("/sys.json", "/sys.bak"); // Backup for crash recovery
  }
}

void gainXP(int amount) {
  if (conversationTestMode) { LOGD("TEST", "PERSIST_SUPPRESS reason=test_mode path=xp_gain"); return; }
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

bool saveCoreMemory() {
  if (conversationTestMode) { LOGI("TEST", "PERSIST_SUPPRESS reason=test_mode path=core_memory_save"); return false; }
  if (memoryFictionQuarantined || memoryFictionBoundaryPending) { LOGI("MEM", "PERSIST_SUPPRESS reason=fiction_quarantine path=core_memory_save"); return false; }
  if (coreMemorySaveMutex && xSemaphoreTake(coreMemorySaveMutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
    LOGW("FS", "Core memory save skipped: save mutex timeout");
    return false;
  }
  if (!canAllocJson(3072)) {
    heapLogAndWarn("saveCore_skip");
    if (coreMemorySaveMutex) xSemaphoreGive(coreMemorySaveMutex);
    return false;
  }
  enforceStringCaps(); // Apply caps before saving
  DynamicJsonDocument doc(3072);
  doc["journal"] = core_chatSummary;
  doc["obsession"] = core_currentObsession;
  doc["kdays"] = core_knownDays;
  doc["obsessionTime"] = (unsigned long)obsessionSetTime;
  doc["lastConsolidation"] = (unsigned long)lastConsolidation;
  // CRC32: serialize without checksum first, compute, then re-serialize with checksum
  doc["crc"] = (uint32_t)0;
  static char coreMemorySaveBuffer[3200];
  size_t p = serializeJson(doc, coreMemorySaveBuffer, sizeof(coreMemorySaveBuffer));
  if (p == 0 || p >= sizeof(coreMemorySaveBuffer)) {
    LOGE("FS", "Core memory JSON exceeds save buffer");
    if (coreMemorySaveMutex) xSemaphoreGive(coreMemorySaveMutex);
    return false;
  }
  uint32_t crc = crc32Calc((const uint8_t*)coreMemorySaveBuffer, p);
  doc["crc"] = crc;
  p = serializeJson(doc, coreMemorySaveBuffer, sizeof(coreMemorySaveBuffer));
  bool saved = p > 0 && p < sizeof(coreMemorySaveBuffer) &&
               atomicWriteFile("/memory.json", coreMemorySaveBuffer, p);
  if (saved) {
    copyFile("/memory.json", "/memory.bak"); // Backup for crash recovery
  } else {
    LOGE("FS", "Core memory save failed");
  }
  if (coreMemorySaveMutex) xSemaphoreGive(coreMemorySaveMutex);
  return saved;
}

void loadCoreMemory() {
  // Try primary file, then backup, with CRC32 validation
  const char* paths[] = {"/memory.json", "/memory.bak"};
  for (int attempt = 0; attempt < 2; attempt++) {
    File f = LittleFS.open(paths[attempt], "r");
    if (!f) continue;
    if (!canAllocJson(3072)) { LOGW("FS","Skipping loadCoreMemory - low heap"); f.close(); return; }
    DynamicJsonDocument doc(3072);
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) { LOGW("FS","%s parse failed: %s", paths[attempt], err.c_str()); continue; }
    // Verify CRC32 if present (use static buffer to avoid stack overflow)
    if (doc.containsKey("crc")) {
      uint32_t stored = doc["crc"] | (uint32_t)0;
      doc["crc"] = (uint32_t)0;
      static char verifyBuf[3072]; // Static to avoid stack overflow on boot
      size_t vp = serializeJson(doc, verifyBuf, sizeof(verifyBuf));
      uint32_t computed = crc32Calc((const uint8_t*)verifyBuf, vp);
      if (stored != computed) {
        LOGW("FS","%s CRC mismatch (stored=%lu computed=%lu)", paths[attempt], (unsigned long)stored, (unsigned long)computed);
        continue; // Try next path (backup)
      }
      LOGI("FS","%s CRC OK (verified=%lu)", paths[attempt], (unsigned long)stored);
    }
    // Valid file — load it
    strncpy(core_chatSummary, doc["journal"] | "", sizeof(core_chatSummary) - 1);
    core_chatSummary[sizeof(core_chatSummary) - 1] = '\0';
    strncpy(core_currentObsession, doc["obsession"] | "", sizeof(core_currentObsession) - 1);
    core_currentObsession[sizeof(core_currentObsession) - 1] = '\0';
    core_knownDays = doc["kdays"] | 0;
    strncpy(core_lastExchangeTone, doc["lxt"] | "", sizeof(core_lastExchangeTone) - 1);
    core_lastExchangeTone[sizeof(core_lastExchangeTone) - 1] = '\0';
    obsessionSetTime = doc["obsessionTime"] | 0;
    lastConsolidation = doc["lastConsolidation"] | 0;
    enforceStringCaps(); // Apply caps after loading
    LOGI("FS","Loaded memory from %s (journal=%u chars)", paths[attempt], (unsigned)strlen(core_chatSummary));
    localMemoryLoaded = (strlen(core_chatSummary) > 0);
    return; // Success
  }
  LOGW("FS","All memory files failed — starting fresh");
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
  coreMemorySaveMutex = xSemaphoreCreateMutex();
  if (!coreMemorySaveMutex) LOGE("BOOT", "Core memory save mutex allocation failed");
  memExtractMutex = xSemaphoreCreateMutex();
  if (!memExtractMutex) LOGE("BOOT", "Shared memory mutex allocation failed");
  memoryWorkMutex = xSemaphoreCreateMutex();
  if (!memoryWorkMutex) LOGE("BOOT", "AI network mutex allocation failed");
  // Preserve NVS across normal boots. Erasing it here also clears state used by
  // ESP32 libraries; initialize it only when needed and retain existing data.
  esp_err_t nvsErr = nvs_flash_init();
  if (nvsErr == ESP_ERR_NVS_NO_FREE_PAGES || nvsErr == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    LOGE("BOOT", "NVS needs recovery (%d); leaving it intact to protect saved state", nvsErr);
  } else if (nvsErr != ESP_OK) {
    LOGW("BOOT", "NVS init failed (%d)", nvsErr);
  }

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
    // Never format automatically here: a mount error must not erase settings
    // and memory. Continue in degraded mode and report the issue for recovery.
    Serial.println("LittleFS mount failed — preserving flash; continuing without filesystem");
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

  const char* sysPaths[] = {"/sys.json", "/sys.bak"};
  for (int sysAttempt = 0; sysAttempt < 2; sysAttempt++) {
    File f = LittleFS.open(sysPaths[sysAttempt], "r");
    if (!f) continue;
    if (!canAllocJson(1536)) { LOGW("FS","Skipping sys.json load - low heap"); f.close(); break; }
    DynamicJsonDocument d(1536);
    DeserializationError err = deserializeJson(d, f);
    f.close();
    if (err) { LOGW("FS","%s parse failed: %s", sysPaths[sysAttempt], err.c_str()); continue; }
    // Verify CRC32 if present (use static buffer to avoid stack overflow on boot)
    if (d.containsKey("crc")) {
      uint32_t stored = d["crc"] | (uint32_t)0;
      d["crc"] = (uint32_t)0;
      static char verifyBuf[1536]; // Static to avoid stack overflow on boot
      size_t vp = serializeJson(d, verifyBuf, sizeof(verifyBuf));
      uint32_t computed = crc32Calc((const uint8_t*)verifyBuf, vp);
      if (stored != computed) {
        LOGW("FS","%s CRC mismatch (stored=%lu computed=%lu)", sysPaths[sysAttempt], (unsigned long)stored, (unsigned long)computed);
        continue;
      }
      LOGI("FS","%s CRC OK", sysPaths[sysAttempt]);
    }
    // Valid file — load all fields
    strncpy(sys.ssid, d["s"] | "", sizeof(sys.ssid) - 1); sys.ssid[sizeof(sys.ssid)-1] = '\0';
    strncpy(sys.pass, d["p"] | "", sizeof(sys.pass) - 1); sys.pass[sizeof(sys.pass)-1] = '\0';
    sys.soundOn = d["snd"] | true;
    sys.xp = d["x"] | 0;
    sys.level = d["l"] | 1;
    sys.autoThink = d["at"] | true;
    sys.affinity = d["aff"] | 0;
    currentEmotion = (Emotion)(int)(d["lastEmo"] | 0);
    if (currentEmotion == ANGRY || currentEmotion == SAD || currentEmotion == SHOCKED || currentEmotion == SLEEPY) currentEmotion = NEUTRAL;
    emotionSetTime = millis();
    sys.latitude = d["lat"] | 33.68; sys.longitude = d["lon"] | 73.04;
    sys.scrollSpeed = d["scrl"] | 1; sys.timeZoneOffset = d["tz"] | 18000;
    rpgHP = d["rpgHP"] | 100; rpgGold = d["rpgG"] | 0;
    strncpy(rpgStory, d["rpgStory"] | "Loading story...", sizeof(rpgStory) - 1); rpgStory[sizeof(rpgStory)-1] = '\0';
    strncpy(rpgChoices[0], d["rpgC1"] | "...", sizeof(rpgChoices[0]) - 1); rpgChoices[0][sizeof(rpgChoices[0])-1] = '\0';
    strncpy(rpgChoices[1], d["rpgC2"] | "...", sizeof(rpgChoices[1]) - 1); rpgChoices[1][sizeof(rpgChoices[1])-1] = '\0';
    strncpy(rpgChoices[2], d["rpgC3"] | "...", sizeof(rpgChoices[2]) - 1); rpgChoices[2][sizeof(rpgChoices[2])-1] = '\0';
    rpgChoiceIdx = d["rpgSel"] | 0; if (rpgChoiceIdx < 0 || rpgChoiceIdx > 2) rpgChoiceIdx = 0;
    sys.guessHighScore = d["ghs"] | 999;
    strncpy(sys.phoneIP, d["pip"] | DEFAULT_PHONE_IP, sizeof(sys.phoneIP) - 1); sys.phoneIP[sizeof(sys.phoneIP)-1] = '\0';
    phoneIP.fromString(sys.phoneIP);
    sys.soundVolume = d["sv"] | 50; sys.ambientSounds = d["amb"] | true;
    sys.bootSound = d["bt"] | true; sys.soundScents = d["sc"] | true;
    sys.beepsOn = d["beep"] | true; sys.voiceOn = d["vOn"] | true;
    sys.voicePulse = d["vPulse"] | 55; sys.voicePolarity = d["vPol"] | 0;
    sys.voiceDeadband = d["vDead"] | 0.0f; sys.voiceSpread = d["vSprd"] | 1;
    sys.voiceGain = d["vGain"] | 125;
    strncpy(sys.voiceName, d["vName"] | "en-US-AriaNeural", sizeof(sys.voiceName) - 1); sys.voiceName[sizeof(sys.voiceName)-1] = '\0';
    sys.voiceRate = d["vRate"] | 10; sys.screenSaver = d["ssv"] | true; sys.voicePitch = d["vPitch"] | 0;
    strncpy(sys.innerThread, d["ith"] | "", sizeof(sys.innerThread) - 1); sys.innerThread[sizeof(sys.innerThread)-1] = '\0';
    if (strlen(sys.innerThread)) { strncpy(pulseMemory[0], sys.innerThread, 159); pulseMemory[0][159] = '\0'; pulseMemoryCount = 1; }
    strncpy(sys.mqttServer, d["mqttSrv"] | "", sizeof(sys.mqttServer) - 1); sys.mqttServer[sizeof(sys.mqttServer)-1] = '\0';
    sys.mqttPort = d["mqttPort"] | 0;
    strncpy(sys.deviceId, d["devId"] | "", sizeof(sys.deviceId) - 1); sys.deviceId[sizeof(sys.deviceId)-1] = '\0';
    strncpy(sys.characterName, d["charNm"] | "", sizeof(sys.characterName) - 1); sys.characterName[sizeof(sys.characterName)-1] = '\0';
    strncpy(sys.phoneContactId, d["phCtc"] | "", sizeof(sys.phoneContactId) - 1); sys.phoneContactId[sizeof(sys.phoneContactId)-1] = '\0';
    strncpy(sys.phoneIP_mqtt, d["pipMqtt"] | "", sizeof(sys.phoneIP_mqtt) - 1); sys.phoneIP_mqtt[sizeof(sys.phoneIP_mqtt)-1] = '\0';
    strncpy(sys.ttsRelayUrl, d["ttsUrl"] | "", sizeof(sys.ttsRelayUrl) - 1); sys.ttsRelayUrl[sizeof(sys.ttsRelayUrl)-1] = '\0';
    sys.txPower = d["txPow"] | 20;
    sys.lastConversationTime = d["lct"] | 0; sys.lastQuoteRefresh = d["lqr"] | 0;
    strncpy(sys.currentModel, d["mdl"] | "qwen/qwen3.6-27b", sizeof(sys.currentModel) - 1); sys.currentModel[sizeof(sys.currentModel)-1] = '\0';
    LOGI("FS","Loaded sys from %s", sysPaths[sysAttempt]);
    break; // Success — stop trying
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
  memoryV2Load(); // Load additive structured memory from its own file
  memoryV2Report();
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
          sessionMode = PM_QUIET;
          yukiEnergy = min(yukiEnergy + 0.3f, 1.0f);
          LOGI("ABSENT", "Long absence: %lu hrs; no relationship penalty", (unsigned long)absenceHrs);
        } else if (absenceHrs >= 24) {
          yukiEnergy = min(yukiEnergy + 0.2f, 1.0f);
          LOGI("ABSENT", "Day-long absence: %lu hrs; no relationship penalty", (unsigned long)absenceHrs);
        } else if (absenceHrs >= 12) {
          yukiEnergy = min(yukiEnergy + 0.15f, 1.0f);
          LOGI("ABSENT", "Noticed absence: %lu hrs", (unsigned long)absenceHrs);
        } else if (absenceHrs >= 2) {
          yukiEnergy = min(yukiEnergy + 0.1f, 1.0f);
          LOGI("ABSENT", "Return after quiet: %lu hrs", (unsigned long)absenceHrs);
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
  lastPresencePing = millis() - PING_INTERVAL; // Establish the initial phone-presence baseline promptly
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
  pollConversationTestCommand();
  // --- CRITICAL: FLUSH SAVES BEFORE AI/WIFI SPIKES ---
  // We move these to the very top to ensure data is safe before power-hungry tasks
  if (!conversationTestMode && !pendingResponse && activeForegroundTurnId == 0 &&
      ESP.getFreeHeap() >= MEM_EXTRACT_MIN_HEAP &&
      !memoryFictionQuarantined && !memoryFictionBoundaryPending && memoryV2Dirty) {
    heavyOpCooldown();
    memoryV2SaveIfNeeded();
    yield();
  }
  static unsigned long lastCloudSyncAttempt = 0;
  static unsigned long cloudSyncRetryMs = 5000;
  if (!conversationTestMode && !pendingResponse && activeForegroundTurnId == 0 &&
      ESP.getFreeHeap() >= MEM_EXTRACT_MIN_HEAP &&
      !memoryFictionQuarantined && !memoryFictionBoundaryPending && pendingCloudSync && mqttClient.connected() &&
      (lastCloudSyncAttempt == 0 || millis() - lastCloudSyncAttempt >= cloudSyncRetryMs)) {
    lastCloudSyncAttempt = millis();
    heavyOpCooldown();
    // Clear first so a background extraction that finishes during sync can
    // raise the flag again without its newer update being lost.
    pendingCloudSync = false;
    if (syncMemoryToCloud()) {
      cloudSyncRetryMs = 5000;
    } else {
      pendingCloudSync = true;
      LOGW("CLOUD", "Sync incomplete; retry in %lu ms", cloudSyncRetryMs);
      cloudSyncRetryMs = min(cloudSyncRetryMs * 2, 60000UL);
    }
    yield();
  }
  if (!conversationTestMode && !pendingResponse && activeForegroundTurnId == 0 &&
      ESP.getFreeHeap() >= MEM_EXTRACT_MIN_HEAP &&
      !memoryFictionQuarantined && !memoryFictionBoundaryPending && pendingMemorySave) {
    heavyOpCooldown();
    saveCoreMemory();
    yield();
    pendingMemorySave = false;
  }
  // Memory extraction now runs in background task (memExtractWorker)
  // requestMemoryExtract() in yuki_net.h signals the task directly
  if (!conversationTestMode && !pendingResponse && activeForegroundTurnId == 0 &&
      ESP.getFreeHeap() >= MEM_EXTRACT_MIN_HEAP &&
      !memoryFictionQuarantined && !memoryFictionBoundaryPending && pendingMemoryConsolidation) {
    consolidateMemory();
    yield();
    pendingMemoryConsolidation = false;
  }
  // Keep extraction jobs queued if their worker could not be allocated during
  // a low-heap foreground reply. Start the worker later at a safe idle point.
  static uint32_t lastMemExtractWorkerCreateAttempt = 0;
  if (!conversationTestMode && !pendingResponse && activeForegroundTurnId == 0 &&
      !memoryFictionQuarantined && !memoryFictionBoundaryPending &&
      WiFi.status() == WL_CONNECTED && ESP.getFreeHeap() >= MEM_EXTRACT_MIN_HEAP &&
      (int32_t)(millis() - memExtractNotBefore) >= 0 &&
      millis() - lastInteraction >= MEM_EXTRACT_IDLE_GAP_MS &&
      !ttsPlaying && !ttsPendingSpeak &&
      !memExtractTaskHandle && memExtractMutex &&
      (lastMemExtractWorkerCreateAttempt == 0 ||
       millis() - lastMemExtractWorkerCreateAttempt >= 10000UL)) {
    bool extractionQueued = false;
    if (xSemaphoreTake(memExtractMutex, 0) == pdTRUE) {
      extractionQueued = memExtractQueueCount > 0;
      xSemaphoreGive(memExtractMutex);
    }
    if (extractionQueued) {
      lastMemExtractWorkerCreateAttempt = millis();
      if (xTaskCreate(memExtractWorker, "memExtract", MEM_EXTRACT_TASK_STACK, NULL,
                      MEM_EXTRACT_TASK_PRIO, &memExtractTaskHandle) == pdPASS) {
        LOGI("MEM", "Deferred extraction worker started (heap=%u)", ESP.getFreeHeap());
        xTaskNotifyGive(memExtractTaskHandle);
      } else {
        LOGW("MEM", "Deferred extraction worker start failed (heap=%u); jobs remain queued",
             ESP.getFreeHeap());
      }
    }
  }
  if (!conversationTestMode && pendingSysSave) {
    heavyOpCooldown();
    saveSys();
    yield();
    pendingSysSave = false;
  }

  // If Wi-Fi remains unavailable past the foreground deadline, show a clear
  // local failure and retain one MQTT notice until connectivity returns.
  if (pendingResponse && WiFi.status() != WL_CONNECTED && pendingResponseAt != 0 &&
      millis() - pendingResponseAt >= 30000UL) {
    activeForegroundTurnId = ++foregroundTurnSequence;
    if (activeForegroundTurnId == 0) activeForegroundTurnId = ++foregroundTurnSequence;
    LOGW("NET", "AI_REQ_FAIL turn=%lu stage=wifi_unavailable elapsed_ms=%lu free=%u largest=%u",
         (unsigned long)activeForegroundTurnId,
         (unsigned long)(millis() - pendingResponseAt), ESP.getFreeHeap(),
         (unsigned)aiLargestFreeBlock());
    activeForegroundTurnId = 0;
    copySafeText(aiMsg, sizeof(aiMsg), "I couldn't reach the chat service because Wi-Fi stayed offline.");
    currentEmotion = CONFUSED;
    emotionSetTime = millis();
    currentMode = FACE;
    drawAesthetica();
    strncpy(pendingResponseFailureRecipient, lastSenderID,
            sizeof(pendingResponseFailureRecipient) - 1);
    pendingResponseFailureRecipient[sizeof(pendingResponseFailureRecipient) - 1] = '\0';
    copySafeText(pendingResponseFailureText, sizeof(pendingResponseFailureText), aiMsg);
    pendingResponseFailureNotice = true;
    pendingResponse = false;
    pendingResponseAt = 0;
  }
  if (pendingResponseFailureNotice && WiFi.status() == WL_CONNECTED && mqttClient.connected()) {
    sendMessage(pendingResponseFailureRecipient, pendingResponseFailureText);
    pendingResponseFailureNotice = false;
    pendingResponseFailureRecipient[0] = '\0';
    pendingResponseFailureText[0] = '\0';
  }

  // --- IDLE WiFi POWER SAVING ---
  // Put WiFi in modem-sleep during idle to reduce baseline draw
  if (WiFi.status() == WL_CONNECTED && millis() - lastHeavyOp > 2000) {
    WiFi.setSleep(true);
  } else if (WiFi.status() == WL_CONNECTED) {
    WiFi.setSleep(false);
  }

  // --- SLEEP CHECK ---
  if (!conversationTestMode && millis() - lastActivity > sleepTimeout && !yukiSleeping) {
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
      heavyOpCooldown();
      acknowledgePhysicalWake();
      heavyOpCooldown();
      drawAesthetica();
    } else if (autoWoke) {
      heavyOpCooldown();
      syncAI("You just woke up from a nap on your own. Say one brief, gentle, groggy line about feeling sleepy. Do not ask a question or invent details about the room, time, or user.", false, FACE, false, true);
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
  if (!conversationTestMode && currentMode == FACE && !yukiSleeping && !yukiGroggy && !pendingResponse &&
      ESP.getFreeHeap() >= MEM_EXTRACT_MIN_HEAP &&
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
        // All actions are now meaningful — every pulse costs energy
        consecutiveNothingCount = 0;
        pulseBurstCount++;
        yukiEnergy -= 0.005f;
        if (yukiEnergy < 0.1f) yukiEnergy = 0.1f;
        if (pulseBurstCount >= 3 || random(0, 10) >= 3) {
          pulseBurstCount = 0;
        }
      }

      if (pulseBurstCount == 0) {
        unsigned long minsSinceInteraction = (millis() - lastInteraction) / 60000UL;
        float interactionFactor = 1.0f;
        if (minsSinceInteraction > 10) interactionFactor = 1.5f;
        float energyFactor = 1.0f + (1.0f - yukiEnergy) * 0.5f;
        nextPulseInterval = (unsigned long)(random(90000, 180000) * energyFactor * interactionFactor);
      }
      lastYukiPulse = millis();
      lastIdleCheck = millis();
    }
  }

  // --- MICRO-EXPRESSION FREQUENCY (Change #9) ---
  // Fires independently of pulse timer — random face twitches every 60s, only when neutral
  static unsigned long lastMicroExprCheck = 0;
  if (currentMode == FACE && !yukiSleeping && !yukiGroggy && !pendingResponse &&
      currentEmotion == NEUTRAL && millis() - lastMicroExprCheck > 60000 && random(4) == 0) {
    lastMicroExprCheck = millis();
    if (microExpressionUntil == 0) { // don't stack on existing micro-expression
      Emotion microEmotions[] = {HAPPY, SHY, WINK, THINKING_FACE, LOVE, CONFUSED, SASSY};
      microEmotion = microEmotions[random(7)];
      microExpressionUntil = millis() + 500;
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
  if (!pendingResponse && !pendingIntentFollowUp && ESP.getFreeHeap() >= MEM_EXTRACT_MIN_HEAP &&
      millis() - lastWellbeingGuardCheck > 28800000UL && WiFi.status() == WL_CONNECTED) {
    lastWellbeingGuardCheck = millis();
    lastWellbeingCheck = millis();
    syncAI("Look at recent conversation and memory. Write ONE sentence about how the user seems emotionally. Do not store this as a durable personal fact or emit a memory tag. Be honest.", true, FACE, true);
  }

  // Level-up guard
  if (!pendingResponse && !pendingIntentFollowUp && ESP.getFreeHeap() >= MEM_EXTRACT_MIN_HEAP && pendingLevelUpAck &&
      millis() - lastLevelUpGuardCheck > 60000) {
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

  // Yuki sleep auto-enter (deep night, critically low energy, or sustained sleepiness)
  if (!conversationTestMode && !yukiSleeping && !yukiGroggy) {
    int idleHr = timeClient.getHours();
    bool deepHours = (idleHr >= 23 || idleHr < 5);
    bool wakeGrace = (!hasSleptSinceBoot || millis() - lastWakeTime > 180000);
    bool recentlyTalked = (millis() - lastInteraction < 300000); // 5 min grace after conversation

    // Deep night + sleepy + low energy = guaranteed sleep
    if (!recentlyTalked && deepHours && currentEmotion == SLEEPY && yukiEnergy < 0.3f && millis() - lastInteraction > 300000 && wakeGrace) {
      enterYukiSleep(); sound_sleep_farewell();
    }
    // Critically low energy = guaranteed sleep
    else if (!recentlyTalked && yukiEnergy < 0.15f && millis() - lastInteraction > 600000 && wakeGrace) {
      currentEmotion = SLEEPY; emotionSetTime = millis();
      enterYukiSleep(); sound_sleep_farewell();
    }
    // Sustained sleepiness: if SLEEPY face has been showing for >30s with no interaction, she naturally falls asleep
    else if (!recentlyTalked && currentEmotion == SLEEPY && millis() - emotionSetTime > 30000 && millis() - lastInteraction > sleepTimeout && wakeGrace) {
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
      conversationTestTurnInProgress = conversationTestMode;
      activeForegroundTurnId = ++foregroundTurnSequence;
      if (activeForegroundTurnId == 0) activeForegroundTurnId = ++foregroundTurnSequence;
      LOGI("NET", "AI_TURN_DISPATCH id=%lu wait_ms=%lu prompt_chars=%u",
           (unsigned long)activeForegroundTurnId,
           (unsigned long)(millis() - pendingResponseAt),
           (unsigned)strlen(responsePrompt));
      syncAI(responsePrompt, isPersonalReply, FACE, false, true);
      activeForegroundTurnId = 0;
      conversationTestTurnInProgress = false;
      sendMessage(lastSenderID, aiMsg);
      pendingResponse = false;
      pendingResponseAt = 0;
    }
  }

  // --- HANDLE PENDING INTENT FOLLOW-UP (news/quote) ---
  // After the initial AI response with [NEWS]/[QUOTE] tag, we do a second
  // syncAI call here in the main loop (not recursively inside syncAI).
  if (pendingIntentFollowUp && WiFi.status() == WL_CONNECTED) {
    if (ttsPlaying || ttsPendingSpeak || millis() - lastIdleAction < 6000) {
      // Let current speech release its socket/buffers before opening news TLS.
    } else {
      if (intentType == 'N' && intentData[0] == '\0' &&
          !fetchHeadlines(intentData, sizeof(intentData))) {
        strncpy(intentData, "No headlines available right now.", sizeof(intentData) - 1);
        intentData[sizeof(intentData) - 1] = '\0';
      }
      bool newsUnavailable = intentType == 'N' &&
          strncmp(intentData, "No headlines available right now.", 33) == 0;
      pendingIntentFollowUp = false;
      if (newsUnavailable) {
        // Do not ask the model to embellish a failed feed using its earlier
        // unsourced lead-in; answer plainly when no verified stories arrived.
        strncpy(aiMsg, "I couldn't reach the news feed just now, so I don't have verified headlines.", sizeof(aiMsg) - 1);
        aiMsg[sizeof(aiMsg) - 1] = '\0';
        currentEmotion = NEUTRAL;
        emotionSetTime = millis();
        currentMode = FACE;
        drawAesthetica();
        if (sys.soundOn && sys.voiceOn) ttsQueue(aiMsg);
        sendMessage(lastSenderID, aiMsg);
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
      currentEmotion = THINKING_FACE;
      drawAesthetica();
      syncAI(followUp, true, FACE, false, true);
      sendMessage(lastSenderID, aiMsg);
      }
    }
  }

  // Speak presence changes only when Yuki is free; keep one recent transition
  // queued so busy periods do not silently discard the reaction.
  if (pendingPresenceReaction != 0 &&
      WiFi.status() == WL_CONNECTED &&
      currentMode == FACE &&
      !yukiSleeping &&
      !yukiGroggy &&
      !pendingResponse &&
      !pendingIntentFollowUp &&
      !pendingIntentFollowUp &&
      !pendingGeocode &&
      !ttsPlaying &&
      !ttsPendingSpeak &&
      !lowVoltageMode &&
      millis() - lastIdleAction >= 6000 &&
      millis() - lastInteraction >= 5000) {
    int8_t reaction = pendingPresenceReaction;
    pendingPresenceReaction = 0;
    if (reaction > 0) {
    syncAI("[System Event: The user's phone is now reachable on the home network. If a greeting feels natural, offer one brief, warm line; don't assume the user has just arrived or imply they owe you attention.]", false, FACE, true, true);
    } else {
    syncAI("[System Event: The user's phone has been unreachable on the home network for several minutes. Only offer a gentle goodbye if it feels natural; don't claim certainty that the user left, and never guilt them.]", false, FACE, true, true);
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
  if (!pendingResponse && !pendingIntentFollowUp && ESP.getFreeHeap() >= MEM_EXTRACT_MIN_HEAP &&
      WiFi.status() == WL_CONNECTED && millis() - lastWeatherUpdate > 1800000) { // Every 30 minutes
    updateWeather();
    lastWeatherUpdate = millis();
  }

  // Run weather-triggered AI work only after updateWeather() has returned.
  // This avoids nesting the large chat/TLS path inside the weather/TLS path.
  if (pendingWeatherReaction &&
      WiFi.status() == WL_CONNECTED &&
      currentMode == FACE &&
      !yukiSleeping &&
      !yukiGroggy &&
      !pendingResponse &&
      !pendingIntentFollowUp &&
      ESP.getFreeHeap() >= MEM_EXTRACT_MIN_HEAP &&
      !pendingGeocode &&
      !ttsPlaying &&
      !ttsPendingSpeak &&
      !lowVoltageMode &&
      millis() - lastIdleAction >= 6000 &&
      millis() - lastInteraction >= 5000) {
    pendingWeatherReaction = false;
    LOGI("NET", "Dispatching deferred weather reaction");
    syncAI("The weather shifted. If it's notable (rain, snow, storm), briefly mention it. Otherwise skip.", false, FACE, false);
  }

  // Periodic heap warning; low free heap alone is not grounds for a reset.
  if (millis() - lastHeapLog > HEAP_LOG_INTERVAL) {
    lastHeapLog = millis();
    heapLogAndWarn("periodic");
  }

  // --- MQTT Connection Manager ---
  if (WiFi.status() == WL_CONNECTED) {
    handleLocalPulse();
    yield();

    // --- PRESENCE PING (every 2min) ---
    if (millis() - lastPresencePing > PING_INTERVAL) {
      lastPresencePing = millis();
      bool phoneIpConfigured = phoneIP != IPAddress(0, 0, 0, 0);
      if (phoneIpConfigured) {
        bool alive = Ping.ping(phoneIP, 3);
        if (!presenceInitialized) {
          presenceInitialized = true;
          userIsHome = alive;
          prevUserIsHome = alive;
          if (alive) lastPresencePulse = millis();
          LOGI("PULSE", "Initial phone presence: %s", alive ? "home" : "away");
        } else if (alive) {
          presenceReachableSamples = min((int)presenceReachableSamples + 1, 2);
          lastPresencePulse = millis();
          if (presenceReachableSamples >= 2 && !userIsHome) {
            userIsHome = true;
            LOGI("PULSE", "Phone presence confirmed (home=YES)");
          } else if (userIsHome) {
            LOGI("PULSE", "Phone detected (home=YES)");
          }
        } else {
          presenceReachableSamples = 0;
          if (lastPresencePulse > 0 && millis() - lastPresencePulse > PRESENCE_TIMEOUT && userIsHome) {
            userIsHome = false;
            LOGI("PULSE", "Phone unreachable for timeout (home=NO)");
          }
        }
      } else {
        LOGW("PULSE", "Phone presence check skipped: no phone IP configured");
      }
    }

    // --- PRESENCE TRANSITION (welcome / farewell) ---
    if (userIsHome != prevUserIsHome) {
      prevUserIsHome = userIsHome;
      if (userIsHome) {
        sound_welcome_back();
        pendingPresenceReaction = 1;
      } else {
        sound_blip();
        pendingPresenceReaction = -1;
      }
      pendingPresenceReactionAt = millis();
      LOGI("PULSE", "Presence reaction queued: %s", userIsHome ? "arrival" : "departure");
    }

    if (pendingPresenceReaction != 0) {
      if (millis() - pendingPresenceReactionAt > PRESENCE_REACTION_MAX_AGE) {
        pendingPresenceReaction = 0;
        LOGI("PULSE", "Presence reaction expired");
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
      acknowledgePhysicalWake();
      drawAesthetica();
      pendingWakeReply = false; // Discard any queued message; button wake takes priority
      pendingWakeMsg[0] = '\0';
      pendingReturnAbsenceHours = 0;
    } else if (pendingWakeReply) {
      pendingWakeReply = false;
      wakeYukiSleep();
      char wakePrompt[896];
      if (pendingReturnAbsenceHours >= 72) {
        snprintf(wakePrompt, sizeof(wakePrompt),
          "You were asleep when the user sent this message: '%.520s'. You are still a little groggy; react briefly, then read and answer what they actually said. They have been away about %lu days, so a simple welcome back is optional if it fits. Do not guilt them or guess why.",
          pendingWakeMsg, pendingReturnAbsenceHours / 24);
      } else if (pendingReturnAbsenceHours >= 24) {
        snprintf(wakePrompt, sizeof(wakePrompt),
          "You were asleep when the user sent this message: '%.520s'. You are still a little groggy; react briefly, then read and answer what they actually said. A simple welcome back is optional if it fits. Do not guilt them or guess why.",
          pendingWakeMsg);
      } else if (pendingReturnAbsenceHours >= 12) {
        snprintf(wakePrompt, sizeof(wakePrompt),
          "You were asleep when the user sent this message: '%.520s'. You are still a little groggy; react briefly, then read and answer what they actually said. A simple welcome back is optional if it fits. Do not guilt them or guess why.",
          pendingWakeMsg);
      } else {
        snprintf(wakePrompt, sizeof(wakePrompt),
          "You were asleep when the user sent this message: '%.520s'. You are still a little groggy; react briefly, then read and answer what they actually said.",
          pendingWakeMsg);
      }
      activeForegroundTurnId = ++foregroundTurnSequence;
      if (activeForegroundTurnId == 0) activeForegroundTurnId = ++foregroundTurnSequence;
      LOGI("NET", "AI_TURN_DISPATCH id=%lu source=sleep_wake prompt_chars=%u",
           (unsigned long)activeForegroundTurnId, (unsigned)strlen(wakePrompt));
      syncAI(wakePrompt, true, FACE, true, true);
      activeForegroundTurnId = 0;
      sendMessage(lastSenderID, aiMsg);
      drawAesthetica();
      pendingWakeMsg[0] = '\0';
    } else if (millis() - yukiSleepSince > yukiSleepDuration) {
      wakeYukiSleep();
      activeForegroundTurnId = ++foregroundTurnSequence;
      if (activeForegroundTurnId == 0) activeForegroundTurnId = ++foregroundTurnSequence;
      LOGI("NET", "AI_TURN_DISPATCH id=%lu source=self_wake", (unsigned long)activeForegroundTurnId);
      syncAI("You just woke up from deep sleep on your own. Say one brief, gentle, groggy line about feeling sleepy. Do not ask a question or invent details about the room, time, or user.", false, FACE, true, true);
      activeForegroundTurnId = 0;
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
    case MODEL_SELECT:      handleModelSelectMode();    break;
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
    if (ttsPlaying) {
      ttsCadenceTicks++;
      if (millis() - ttsCadenceLast >= 1000) {
        ttsCadenceLast = millis();
        LOGI("TTS", "loop alive: %u passes/s", (unsigned)ttsCadenceTicks);
        ttsCadenceTicks = 0;
      }
    } else {
      // Don't count idle loop passes and attribute them to the next playback.
      ttsCadenceTicks = 0;
      ttsCadenceLast = millis();
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
