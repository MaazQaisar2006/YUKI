// mqtt_handler.h
#pragma once
#include <PubSubClient.h>
#include <LittleFS.h>
#include <Adafruit_SSD1306.h>
#include "debug.h"

extern unsigned long lastInteraction;
extern unsigned long lastIdleCheck;
extern bool yukiSleeping;
extern bool pendingWakeReply;
extern char pendingWakeMsg[640];
// Access variables defined in the main .ino file
extern bool pendingResponse;
extern bool pendingGeocode;
extern char pendingGeocodeCity[64];
extern char pendingGeocodeSender[32];
extern char responsePrompt[256];
extern char lastSenderID[32];
extern char core_chatSummary[1024];
extern int rpgHP;
extern int rpgGold;
extern char rpgStory[256];
extern char rpgChoices[3][40];
extern int rpgChoiceIdx;
extern bool pendingCloudSync;
extern bool pendingSysSave;
extern bool mqttForceReconnect;
extern bool cloudRestored;
extern bool localMemoryLoaded;
extern unsigned long mqttBootedAt;
extern char aiMsg[256];
extern char workspace[3200];
extern struct Config sys;
extern int messageIndex;
extern const int MAX_MESSAGES;
extern Message receivedMessages[];

extern void gainXP(int amount);
extern bool canAllocJson(size_t sz);
extern Adafruit_SSD1306 display;
extern IPAddress phoneIP;
extern unsigned long lastPresencePulse;

static const char* memoryV2TopicPrefix = "scout-net/memory_v2/";
extern PubSubClient mqttClient;

inline bool syncStructuredMemoryToCloud() {
  if (!mqttClient.connected()) return false;
  char topic[128];
  char payload[320];
  uint8_t count = 0;
  uint8_t expected = 0;
  bool allPublished = true;
  for (uint8_t slot = 0; slot < memoryV2GetFactCount(); slot++) {
    if (!memoryV2BuildCloudFact(slot, payload, sizeof(payload))) continue;
    expected++;
    snprintf(topic, sizeof(topic), "%s%s/fact/%u", memoryV2TopicPrefix, DEVICE_ID, slot);
    if (mqttClient.publish(topic, payload, true)) count++;
    else allPublished = false;

    uint32_t archiveKey = 0;
    int archiveCategory = -1;
    if (memoryV2BuildArchiveFact(slot, payload, sizeof(payload), &archiveKey, &archiveCategory)) {
      snprintf(topic, sizeof(topic), "%s%s/archive/%08lx", memoryV2TopicPrefix, DEVICE_ID,
               (unsigned long)archiveKey);
      if (!mqttClient.publish(topic, payload, true)) allPublished = false;
      if (archiveCategory >= 0) {
        snprintf(topic, sizeof(topic), "%s%s/archive/latest/%d", memoryV2TopicPrefix, DEVICE_ID, archiveCategory);
        if (!mqttClient.publish(topic, payload, true)) allPublished = false;
      }
    } else {
      allPublished = false;
    }
    yield();
  }
  snprintf(topic, sizeof(topic), "%s%s/commit", memoryV2TopicPrefix, DEVICE_ID);
  snprintf(payload, sizeof(payload), "%lu,%u", (unsigned long)memoryV2GetRevision(), expected);
  if (!mqttClient.publish(topic, payload, true)) allPublished = false;
  LOGI("CLOUD", "Structured memory sync revision=%lu facts=%u", (unsigned long)memoryV2GetRevision(), count);
  return allPublished && mqttClient.connected();
}

inline void clearCloudKey(const char* k); // Forward declaration

// --- MQTT CLIENT ---
#include "secrets.h"
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// Renamed to syncMemoryToCloud to avoid conflict with saveCoreMemory() in the main .ino
inline bool syncMemoryToCloud() {
  // Local save (LittleFS) — serialize into workspace then write atomically
  if (canAllocJson(3072)) {
    DynamicJsonDocument doc(3072);
    doc["journal"] = core_chatSummary;
    doc["rpgHP"] = rpgHP; doc["rpgGold"] = rpgGold;
    doc["rpgStory"] = rpgStory;
    doc["rpgC1"] = rpgChoices[0]; doc["rpgC2"] = rpgChoices[1]; doc["rpgC3"] = rpgChoices[2];
    doc["rpgSel"] = rpgChoiceIdx; doc["aff"] = sys.affinity;
    size_t p = serializeJson(doc, workspace, sizeof(workspace));
    atomicWriteFile("/core.json", workspace, p);
    yield();
  } else {
    LOGW("MQTT","Skipping syncMemoryToCloud local save - low heap");
  }
  // Cloud Sync (MQTT Retained) - Full personality backup
  if (mqttClient.connected()) {
    bool cloudOk = true;
    auto pub = [&](const char* k, const char* v) {
      if (!v || strlen(v) == 0) return; // Skip empty
      if (strlen(v) > 2000) { LOGW("CLOUD","Payload too large for %s, skipping", k); cloudOk = false; return; }
      snprintf(workspace, 128, "scout-net/memory/%s/%s", DEVICE_ID, k);
      if (!mqttClient.publish(workspace, v, true)) {
        cloudOk = false;
        LOGW("CLOUD", "Publish failed for %s (%u bytes)", k, (unsigned int)strlen(v));
      } else {
        LOGD("CLOUD", "Published %s (%u bytes)", k, (unsigned int)strlen(v));
      }
    };
    // Core personality
    pub("summary", core_chatSummary);
    pub("obsession", core_currentObsession);
    char b[12];
    char buf[12];
    itoa(core_knownDays, buf, 10); pub("kdays", buf);
    ltoa(obsessionSetTime, buf, 10); pub("obsessionTime", buf);
    pub("lastExchangeTone", core_lastExchangeTone);
    pub("lastConcern", core_lastConcern);
    char flagBuf[2]; flagBuf[0] = core_badDayFlag ? '1' : '0'; flagBuf[1] = '\0'; pub("badDayFlag", flagBuf);
    flagBuf[0] = missedMorning ? '1' : '0'; pub("missedMorning", flagBuf);
    flagBuf[0] = missedMorningMentioned ? '1' : '0'; pub("missedMorningMentioned", flagBuf);
    pub("selfReflection", core_selfReflection);
    // Affinity, XP, level
    itoa(sys.affinity, b, 10); pub("aff", b);
    itoa(sys.xp, b, 10); pub("xp", b);
    itoa(sys.level, b, 10); pub("level", b);
    // RPG state
    itoa(rpgHP, b, 10); pub("rpgHP", b); itoa(rpgGold, b, 10); pub("rpgGold", b);
    pub("rpgStory", rpgStory);
    pub("rpgC1", rpgChoices[0]); pub("rpgC2", rpgChoices[1]); pub("rpgC3", rpgChoices[2]);
    itoa(rpgChoiceIdx, b, 10); pub("rpgSel", b);
    // Personality context (chunked)
    if (strlen(personalityContext) > 0) {
      if (strlen(personalityContext) <= 1024) {
        pub("personalityContext", personalityContext);
      } else {
        char chunk1[1025]; char chunk2[1025];
        strncpy(chunk1, personalityContext, 1024); chunk1[1024] = '\0';
        strncpy(chunk2, personalityContext + 1024, 1024); chunk2[1024] = '\0';
        pub("personalityContext_1", chunk1);
        if (strlen(chunk2) > 0) pub("personalityContext_2", chunk2);
      }
    }
    // System prompt (chunked)
    if (strlen(systemPrompt) > 0) {
      if (strlen(systemPrompt) <= 1024) {
        pub("systemPrompt", systemPrompt);
      } else {
        char chunk1[1025]; char chunk2[1025];
        strncpy(chunk1, systemPrompt, 1024); chunk1[1024] = '\0';
        strncpy(chunk2, systemPrompt + 1024, 1024); chunk2[1024] = '\0';
        pub("systemPrompt_1", chunk1);
        if (strlen(chunk2) > 0) pub("systemPrompt_2", chunk2);
      }
    }
    LOGI("CLOUD","Full personality sync complete (%u topics)", 25);
    cloudOk = syncStructuredMemoryToCloud() && cloudOk;
    return cloudOk && mqttClient.connected();
  }
  return false;
}

void saveSys();
void sendMessage(const char* recipientID, const char* message);
bool geocodeLocation(const char* cityName);
// --- MQTT TOPICS ---
char statusTopic[128]; // Our status topic (for broadcasting presence)
const char* allStatusTopic = "scout-net/devices/+/status"; // Wildcard to discover other users

// This function is the heart of MQTT. It runs every time a message arrives.
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Reduced from 800 to 512 to avoid stack overflow on WiFi task (shared 4KB stack)
  char message[512];
  unsigned int msg_len = min((unsigned int)sizeof(message) - 1, length);
  memcpy(message, payload, msg_len);
  message[msg_len] = '\0';

  // --- DEDUPLICATION: Ignore duplicate messages from broker queue ---
  // Deduplicate complete topic+payload pairs without storing whole messages.
  static char msgHashes[50][17];
  static int msgHashHead = 0;
  static int msgHashCount = 0;

  uint32_t hashA = 2166136261UL;
  uint32_t hashB = 0x9e3779b9UL;
  for (const char* p = topic; *p; p++) {
    uint8_t value = (uint8_t)*p;
    hashA = (hashA ^ value) * 16777619UL;
    hashB ^= value + 0x9e3779b9UL + (hashB << 6) + (hashB >> 2);
  }
  for (unsigned int i = 0; i < length; i++) {
    uint8_t value = payload[i];
    hashA = (hashA ^ value) * 16777619UL;
    hashB ^= value + 0x9e3779b9UL + (hashB << 6) + (hashB >> 2);
  }
  char msgHash[17];
  snprintf(msgHash, sizeof(msgHash), "%08lx%08lx", (unsigned long)hashA, (unsigned long)hashB);

  bool isMemoryTopic = strncmp(topic, "scout-net/memory/", 17) == 0 ||
                       strncmp(topic, "scout-net/memory_v2/", 20) == 0;
  bool isStructuredMemoryTopic = strncmp(topic, "scout-net/memory_v2/", 20) == 0;

  // Check circular buffer for duplicate
  bool isDuplicate = false;
  if (!isStructuredMemoryTopic) {
    for (int i = 0; i < msgHashCount; i++) {
      if (strcmp(msgHashes[i], msgHash) == 0) {
        isDuplicate = true;
        break;
      }
    }
  }
  if (isDuplicate) { 
    LOGD("MQTT","Duplicate message ignored");
    return; 
  }

  // Structured memory must be processed on every retained replay and search.
  if (!isStructuredMemoryTopic) {
    strncpy(msgHashes[msgHashHead], msgHash, sizeof(msgHashes[0]) - 1);
    msgHashes[msgHashHead][sizeof(msgHashes[0]) - 1] = '\0';
    msgHashHead = (msgHashHead + 1) % 50;
    if (msgHashCount < 50) msgHashCount++;
  }

  if (!isMemoryTopic) LOGD("MQTT", "Recv Topic: %s | Msg: %s", topic, message);

  // --- CLOUD MEMORY SYNC ---
  // Layer 3: Only overwrite local fields if they're empty (local data takes priority)
  static char memPrefixMatch[64]; 
  snprintf(memPrefixMatch, sizeof(memPrefixMatch), "scout-net/memory/%s/", DEVICE_ID);
  if (strncmp(topic, memPrefixMatch, strlen(memPrefixMatch)) == 0) {
    const char* key = topic + strlen(memPrefixMatch);
    // Numeric fields: always safe to update from cloud
    if (strcmp(key, "rpgHP") == 0) rpgHP = atoi(message);
    else if (strcmp(key, "rpgGold") == 0) rpgGold = atoi(message);
    else if (strcmp(key, "aff") == 0) sys.affinity = atoi(message);
    else if (strcmp(key, "xp") == 0)    { sys.xp    = atoi(message); pendingSysSave = true; }
    else if (strcmp(key, "level") == 0) { sys.level  = atoi(message); pendingSysSave = true; }
    else if (strcmp(key, "tz") == 0)    { sys.timeZoneOffset = atol(message); pendingSysSave = true; }
    else if (strcmp(key, "rpgSel") == 0) rpgChoiceIdx = atoi(message);
    else if (strcmp(key, "kdays") == 0) { if (core_knownDays == 0) core_knownDays = atoi(message); }
    else if (strcmp(key, "obsessionTime") == 0) { if (obsessionSetTime == 0) obsessionSetTime = atol(message); }
    else if (strcmp(key, "badDayFlag") == 0) core_badDayFlag = (message[0] == '1');
    else if (strcmp(key, "missedMorning") == 0) missedMorning = (message[0] == '1');
    else if (strcmp(key, "missedMorningMentioned") == 0) missedMorningMentioned = (message[0] == '1');
    // Text fields: overwrite if local is empty OR if this was a crash reboot (cloud is fresher)
    else if (strcmp(key, "summary") == 0) {
      bool shouldRestore = (strlen(core_chatSummary) == 0) || (esp_reset_reason() == ESP_RST_PANIC);
      if (shouldRestore && strlen(message) > strlen(core_chatSummary)) {
        strncpy(core_chatSummary, message, 1023);
        core_chatSummary[1023] = '\0';
        pendingMemorySave = true;
        LOGI("CLOUD","Restored summary from cloud (reason: %s)", strlen(core_chatSummary) == 0 ? "empty" : "crash recovery");
      }
    }
    else if (strcmp(key, "rpgStory") == 0) { if (strlen(rpgStory) <= 16) { strncpy(rpgStory, message, 255); rpgStory[255] = '\0'; } }
    else if (strcmp(key, "obsession") == 0) { if (strlen(core_currentObsession) == 0) { strncpy(core_currentObsession, message, 31); core_currentObsession[31] = '\0'; } }
    else if (strcmp(key, "lastExchangeTone") == 0) { if (strlen(core_lastExchangeTone) == 0) { strncpy(core_lastExchangeTone, message, 15); core_lastExchangeTone[15] = '\0'; } }
    else if (strcmp(key, "lastConcern") == 0) { if (strlen(core_lastConcern) == 0) { strncpy(core_lastConcern, message, 63); core_lastConcern[63] = '\0'; } }
    else if (strcmp(key, "selfReflection") == 0) { if (strlen(core_selfReflection) == 0) { strncpy(core_selfReflection, message, 255); core_selfReflection[255] = '\0'; } }
    else if (strcmp(key, "personalityContext") == 0) { if (strlen(personalityContext) == 0) { strncpy(personalityContext, message, 1151); personalityContext[1151] = '\0'; } }
    else if (strcmp(key, "personalityContext_1") == 0) { if (strlen(personalityContext) == 0) { strncpy(personalityContext, message, 1024); personalityContext[1024] = '\0'; } }
    else if (strcmp(key, "personalityContext_2") == 0) { 
      size_t curLen = strlen(personalityContext);
      if (curLen < 1150) strncat(personalityContext, message, 1150 - curLen); 
    }
    else if (strcmp(key, "systemPrompt") == 0) { if (strlen(systemPrompt) == 0) { strncpy(systemPrompt, message, 2047); systemPrompt[2047] = '\0'; } }
    else if (strcmp(key, "systemPrompt_1") == 0) { if (strlen(systemPrompt) == 0) { strncpy(systemPrompt, message, 1024); systemPrompt[1024] = '\0'; } }
    else if (strcmp(key, "systemPrompt_2") == 0) { 
      size_t curLen = strlen(systemPrompt);
      if (curLen < 2046) strncat(systemPrompt, message, 2046 - curLen); 
    }
    else if (strcmp(key, "rpgC1") == 0) { strncpy(rpgChoices[0], message, 39); rpgChoices[0][39] = '\0'; }
    else if (strcmp(key, "rpgC2") == 0) { strncpy(rpgChoices[1], message, 39); rpgChoices[1][39] = '\0'; }
    else if (strcmp(key, "rpgC3") == 0) { strncpy(rpgChoices[2], message, 39); rpgChoices[2][39] = '\0'; }
    LOGD("CLOUD","Restored key: %s", key);
    return; // Don't process memory sync as a DM
  }

  char v2Prefix[96];
  snprintf(v2Prefix, sizeof(v2Prefix), "%s%s/", memoryV2TopicPrefix, DEVICE_ID);
  if (strncmp(topic, v2Prefix, strlen(v2Prefix)) == 0) {
    const char* key = topic + strlen(v2Prefix);
    if (strncmp(key, "fact/", 5) == 0) {
      memoryV2AcceptCloudFact(message);
    } else if (strncmp(key, "archive/", 8) == 0) {
      memoryV2AcceptCloudArchiveFact(message);
    } else if (strcmp(key, "commit") == 0) {
      unsigned long revision = 0;
      unsigned int expected = 0;
      if (sscanf(message, "%lu,%u", &revision, &expected) == 2) {
        memoryV2NoteCloudCommit((uint32_t)revision, (uint8_t)expected);
      }
    }
    return;
  }

  // Keep early boot chatter out of the UI, but allow retained memory restore
  // and an explicit archive lookup to complete during the boot grace window.
  if (mqttBootedAt > 0 && millis() - mqttBootedAt < 5000) {
    LOGD("MQTT", "Ignoring non-memory message during boot grace period");
    return;
  }

  // Simplified DM handling: ignore everything except from the phone
  char phoneTopic[128];
  snprintf(phoneTopic, sizeof(phoneTopic), "scout-net/dm/%s/from/%s", DEVICE_ID, PHONE_CONTACT_ID);
  if (strcmp(topic, phoneTopic) != 0) return;

  const char* senderId = PHONE_CONTACT_ID;
  
  strncpy(lastSenderID, senderId, sizeof(lastSenderID) - 1);
    lastSenderID[sizeof(lastSenderID) - 1] = '\0';

    // The entire payload is now the message content. No more parsing needed.
    const char* messageContent = message;

    // Store the full original message in our circular buffer for viewing
    char formattedLogMsg[150];
    snprintf(formattedLogMsg, sizeof(formattedLogMsg), "%s: %s", lastSenderID, messageContent);
    strncpy(receivedMessages[messageIndex].content, formattedLogMsg, sizeof(receivedMessages[0].content) - 1);
    receivedMessages[messageIndex].content[sizeof(receivedMessages[0].content) - 1] = '\0';
    messageIndex = (messageIndex + 1) % MAX_MESSAGES;

    // --- COMMAND PARSER ---
    // 1. Remote Terminal (Commands start with >)
    if (messageContent[0] == '>') {
      // Security: Only allow primary user
      if (strcmp(lastSenderID, PHONE_CONTACT_ID) != 0) {
        sendMessage(lastSenderID, "Access Denied.");
        return;
      }

      char cmd[32]; cmd[0] = '\0'; char arg[64]; arg[0] = '\0';
      // Parse ">cmd arg" (Fixed to capture spaces in argument)
      sscanf(messageContent + 1, "%31s %63[^\n]", cmd, arg);

      // Use a static buffer to avoid heap fragmentation and stack overflow
      static char termOutputBuffer[512];
      termOutputBuffer[0] = '\0';

      if (strcmp(cmd, "ipconfig") == 0) {
        snprintf(termOutputBuffer, 512, "IP: %s\nSignal: %lddBm", WiFi.localIP().toString().c_str(), WiFi.RSSI());
      }
      else if (strcmp(cmd, "ping") == 0) {
        IPAddress remote(8,8,8,8);
        // Resolve hostname if provided
        if (strlen(arg) > 0) {
           WiFi.hostByName(arg, remote);
        }
        
        WiFiClient pinger;
        unsigned long start = millis();
        if (pinger.connect(remote, 53)) {
          snprintf(termOutputBuffer, 512, "Reply from %s: %lums. Online.", remote.toString().c_str(), millis() - start);
          pinger.stop();
        } else {
          strcpy(termOutputBuffer, "Request timed out.");
        }
      }
      else if (strcmp(cmd, "ls") == 0) {
        File root = LittleFS.open("/");
        strcpy(termOutputBuffer, "Files:\n");
        int termUsed = strlen(termOutputBuffer);
        File file = root.openNextFile();
        while (file) {
          if (termUsed > 400) { safeAppend(termOutputBuffer, sizeof(termOutputBuffer), &termUsed, "..."); break; }
          char line[64];
          snprintf(line, sizeof(line), "%-16s %u B\n", file.name(), (unsigned int)file.size());
          safeAppend(termOutputBuffer, sizeof(termOutputBuffer), &termUsed, "%s", line);
          file = root.openNextFile();
          yield();
        }
        root.close();
      }
      else if (strcmp(cmd, "calc") == 0) {
        int a, b; char op;
        if (sscanf(arg, "%d %c %d", &a, &op, &b) == 3) {
          long long result = 0;
          bool validOp = true;
          if (op == '+') result = (long long)a + b;
          else if (op == '-') result = (long long)a - b;
          else if (op == '*') result = (long long)a * b;
          else if (op == '/') result = (b != 0) ? (long long)a / b : 0;
          else validOp = false;
          if (validOp) snprintf(termOutputBuffer, sizeof(termOutputBuffer), "%lld", result);
          else strcpy(termOutputBuffer, "Usage: >calc 5 * 5");
        } else {
          strcpy(termOutputBuffer, "Usage: >calc 5 * 5");
        }
      }
      else if (strcmp(cmd, "write") == 0) {
         char fname[16]; char content[48];
         // Usage: >write note.txt Hello World
         if (sscanf(arg, "%15s %47[^\n]", fname, content) == 2) {
           char path[24]; snprintf(path, sizeof(path), "/%s", fname);
           File f = LittleFS.open(path, "w");
           if (f) { f.print(content); f.close(); snprintf(termOutputBuffer, 512, "Saved %s", fname); }
           else strcpy(termOutputBuffer, "Error writing file.");
         } else strcpy(termOutputBuffer, "Usage: >write f.txt text");
      }
      else if (strcmp(cmd, "cat") == 0) {
        char path[70];
        if (arg[0] != '/') snprintf(path, sizeof(path), "/%s", arg); else strncpy(path, arg, sizeof(path));
        if (LittleFS.exists(path)) {
          File f = LittleFS.open(path, "r");
          int len = f.readBytes(termOutputBuffer, 511);
          termOutputBuffer[len] = '\0';
          f.close();
        } else {
          strcpy(termOutputBuffer, "File not found.");
        }
      }
      else if (strcmp(cmd, "rm") == 0) {
         char path[70];
         if (arg[0] != '/') snprintf(path, sizeof(path), "/%s", arg); else strncpy(path, arg, sizeof(path));
         if (LittleFS.remove(path)) strcpy(termOutputBuffer, "Deleted."); else strcpy(termOutputBuffer, "Fail.");
      }
      else if (strcmp(cmd, "reboot") == 0) {
        sendMessage(lastSenderID, "Rebooting...");
        sound_shutdown();
        delay(1000); ESP.restart();
      }
      else if (strcmp(cmd, "memory") == 0 || strcmp(cmd, "free") == 0) {
        snprintf(termOutputBuffer, 512, "Free Heap: %u bytes", ESP.getFreeHeap());
      }
      else if (strcmp(cmd, "oledon") == 0) {
        display.ssd1306_command(SSD1306_DISPLAYON);
        strcpy(termOutputBuffer, "OLED ON");
      }
      else if (strcmp(cmd, "oledoff") == 0) {
        display.ssd1306_command(SSD1306_DISPLAYOFF);
        strcpy(termOutputBuffer, "OLED OFF");
      }
      else if (strcmp(cmd, "soundon") == 0) {
        sys.soundOn = true;
        strcpy(termOutputBuffer, "Sound ON");
      }
      else if (strcmp(cmd, "soundoff") == 0) {
        sys.soundOn = false;
        strcpy(termOutputBuffer, "Sound OFF");
      }
      else if (strcmp(cmd, "viewmem") == 0) {
        if (strlen(core_chatSummary) > 0) {
          snprintf(termOutputBuffer, 512, "Journal: %s", core_chatSummary);
        } else {
          strcpy(termOutputBuffer, "Journal is empty.");
        }
      }
      else if (strcmp(cmd, "resetmem") == 0) {
        core_chatSummary[0] = '\0';
        saveCoreMemory();
        clearCloudKey("summary");
        strcpy(termOutputBuffer, "Memory wiped.");
      }
      else if (strcmp(cmd, "setphoneip") == 0) {
        if (phoneIP.fromString(arg)) {
          strncpy(sys.phoneIP, arg, sizeof(sys.phoneIP) - 1);
          sys.phoneIP[sizeof(sys.phoneIP) - 1] = '\0';
          saveSys();
          snprintf(termOutputBuffer, 512, "Phone IP set to %s", arg);
        } else {
          strcpy(termOutputBuffer, "Invalid IP. Usage: >setphoneip 192.168.1.50");
        }
      }
      else if (strcmp(cmd, "phoneip") == 0) {
        snprintf(termOutputBuffer, 512, "Phone IP: %s", phoneIP.toString().c_str());
      }
      else if (strcmp(cmd, "scan") == 0) {
        // Check if the configured phone IP is reachable on multiple ports
        snprintf(termOutputBuffer, 512, "Probing %s...", phoneIP.toString().c_str());
        sendMessage(lastSenderID, termOutputBuffer);
        termOutputBuffer[0] = '\0';
        int termUsed = 0;
        int ports[] = {80, 443, 8080, 8443, 53};
        const char* portNames[] = {"HTTP", "HTTPS", "8080", "8443", "DNS"};
        int openPort = -1;
        for (int p = 0; p < 5; p++) {
          WiFiClient probe;
          probe.setTimeout(1500);
          unsigned long start = millis();
          if (probe.connect(phoneIP, ports[p])) {
            char line[64];
            snprintf(line, sizeof(line), "%s(%d): OK %lums\n", portNames[p], ports[p], millis() - start);
            safeAppend(termOutputBuffer, sizeof(termOutputBuffer), &termUsed, "%s", line);
            if (openPort < 0) openPort = ports[p];
            probe.stop();
          } else {
            char line[64];
            snprintf(line, sizeof(line), "%s(%d): closed\n", portNames[p], ports[p]);
            safeAppend(termOutputBuffer, sizeof(termOutputBuffer), &termUsed, "%s", line);
          }
          yield();
        }
        if (openPort >= 0) {
          char ok[64];
          snprintf(ok, sizeof(ok), "Phone ONLINE (port %d open)", openPort);
          safeAppend(termOutputBuffer, sizeof(termOutputBuffer), &termUsed, "%s", ok);
          userIsHome = true;
          lastPresencePulse = millis();
        } else {
          safeAppend(termOutputBuffer, sizeof(termOutputBuffer), &termUsed, "Phone UNREACHABLE");
        }
      }
      else if (strcmp(cmd, "hlp") == 0 || strcmp(cmd, "help") == 0) {
        strcpy(termOutputBuffer, "CMDS: calc, write, ping, scan, ls, cat, rm, free, reboot, oledon/off, soundon/off, viewmem, resetmem, setphoneip, phoneip");
      }
      else {
        snprintf(termOutputBuffer, 512, "Unknown: %s", cmd);
      }

      sendMessage(lastSenderID, termOutputBuffer);
      return; // Stop processing (don't send to AI)
    }
    
    else {
      // --- REGULAR MESSAGE ---
      if (yukiSleeping) {
        // Queue the message and flag for wake — don't sound or process yet
        strncpy(pendingWakeMsg, messageContent, sizeof(pendingWakeMsg) - 1);
        pendingWakeMsg[sizeof(pendingWakeMsg) - 1] = '\0';
        pendingWakeReply = true;
        lastActivity = millis(); // Prevent hardware sleep timeout
        lastInteraction = millis();
        return;
      }
      // Show thinking indicator instead of echoing user's message
      snprintf(aiMsg, sizeof(aiMsg), "Thinking...");
      scrollOffset = 0;
      lastInteraction = millis(); // Receiving a message is an interaction
      lastActivity = millis(); // Reset sleep timer on incoming MQTT message
      lastIdleCheck = millis();   // Reset the idle scheduler
      if (timeClient.isTimeSet()) sys.lastConversationTime = timeClient.getEpochTime();
      sound_notify(); // Play a distinct sound for new messages
      gainXP(10); // XP for receiving a message

      // Trigger the AI to reply in the main loop with only the parsed content
      strncpy(responsePrompt, messageContent, sizeof(responsePrompt) - 1);
      responsePrompt[sizeof(responsePrompt) - 1] = '\0';
      pendingResponse = true;
      lastActivity = millis(); // Reset sleep timer on incoming MQTT message
    }
  }

// Forward declaration
void updateMqttServer();
void restoreFromCloud();

// Returns true on success, false on failure. Caller should manage backoff.
bool mqttReconnect() {
  // Just try to connect once. The main loop will handle the 5-second retry interval.
  LOGI("MQTT","Attempting MQTT connection...");
  mqttClient.setKeepAlive(90);   // fewer keepalive pings: less TX burst + heat
  // Apply dynamic MQTT config (server/port from MQTT Config menu)
  updateMqttServer();
  // Set the Last Will and Testament (LWT)
  const char* lwt_message = "Offline";
  if (mqttClient.connect(DEVICE_ID, statusTopic, 1, true, lwt_message)) {
    // Clear stale subscriptions from broker queue first
    mqttClient.unsubscribe(allStatusTopic);
    char memTopic[128];
    snprintf(memTopic, sizeof(memTopic), "scout-net/memory/%s/+", DEVICE_ID);
    mqttClient.unsubscribe(memTopic);
    char allDmTopic[128];
    snprintf(allDmTopic, sizeof(allDmTopic), "scout-net/dm/%s/from/+", DEVICE_ID);
    mqttClient.unsubscribe(allDmTopic);

    LOGI("MQTT","connected");
    mqttBootedAt = millis(); // Start grace period for retained messages
    // Announce we are online and subscribe fresh
    mqttClient.publish(statusTopic, "Online", true);
    mqttClient.subscribe(allStatusTopic);
    
    snprintf(memTopic, sizeof(memTopic), "scout-net/memory/%s/+", DEVICE_ID);
    mqttClient.subscribe(memTopic);
    char v2Topic[128];
    snprintf(v2Topic, sizeof(v2Topic), "%s%s/fact/+", memoryV2TopicPrefix, DEVICE_ID);
    mqttClient.subscribe(v2Topic);
    snprintf(v2Topic, sizeof(v2Topic), "%s%s/commit", memoryV2TopicPrefix, DEVICE_ID);
    mqttClient.subscribe(v2Topic);

    char allDmTopic2[128];
    snprintf(allDmTopic2, sizeof(allDmTopic2), "scout-net/dm/%s/from/+", DEVICE_ID);
    mqttClient.subscribe(allDmTopic2);
    LOGD("MQTT","Subscribed to wildcard DM topic: %s", allDmTopic2);
    
    // Restore personality from cloud after successful connection
    if (!cloudRestored) {
      restoreFromCloud();
      cloudRestored = true;
    }
  } else {
    LOGW("MQTT","failed to connect, rc=%d - try again later", mqttClient.state());
    return false;
  }
  return true;
}

void updateMqttServer() {
  const char* server = (strlen(sys.mqttServer) ? sys.mqttServer : mqtt_server);
  int port = sys.mqttPort ? sys.mqttPort : mqtt_port;
  mqttClient.setServer(server, port);
}

void setupMqtt() {
  // Construct the generic topic paths for this device.
  snprintf(statusTopic, sizeof(statusTopic), "scout-net/devices/%s/status", DEVICE_ID);

  updateMqttServer();
  // Retained personality chunks can exceed PubSubClient's 512-byte default.
  // Leave room for the MQTT topic/header plus the 1 KB context chunks.
  if (!mqttClient.setBufferSize(2304)) {
    LOGW("MQTT", "Could not grow packet buffer; large retained syncs may fail");
  }
  mqttClient.setCallback(mqttCallback);
}

bool memoryV2SearchCloud(const char* query, char* target, size_t targetSize) {
  if (!target || targetSize == 0) return false;
  target[0] = '\0';
  if (!query || !mqttClient.connected()) return false;

  memoryV2CloudSearchBegin(query);
  char topic[128];
  int category = memoryV2QueryCategory(query);
  if (category >= 0) {
    snprintf(topic, sizeof(topic), "%s%s/archive/latest/%d", memoryV2TopicPrefix, DEVICE_ID, category);
  } else {
    snprintf(topic, sizeof(topic), "%s%s/archive/#", memoryV2TopicPrefix, DEVICE_ID);
  }
  if (!mqttClient.subscribe(topic)) {
    memoryV2CloudSearchEnd(target, targetSize);
    LOGW("CLOUD", "Memory search subscribe failed");
    return false;
  }

  const unsigned long started = millis();
  const unsigned long maxWait = category >= 0 ? 800 : 1800;
  const unsigned long minWait = 250;
  const unsigned long quietWait = 220;
  while (mqttClient.connected() && millis() - started < maxWait) {
    mqttClient.loop();
    yield();
    if (millis() - started >= minWait && millis() - memoryV2CloudSearchLastRx >= quietWait) break;
    delay(3);
  }
  mqttClient.unsubscribe(topic);
  memoryV2CloudSearchEnd(target, targetSize);
  // A retained latest pointer can be missing or contain a rejected malformed
  // value while an older valid fact remains in the append-only archive.
  if (category >= 0 && target[0] == '\0' && mqttClient.connected()) {
    memoryV2CloudSearchBegin(query);
    snprintf(topic, sizeof(topic), "%s%s/archive/#", memoryV2TopicPrefix, DEVICE_ID);
    if (mqttClient.subscribe(topic)) {
      const unsigned long fallbackStarted = millis();
      while (mqttClient.connected() && millis() - fallbackStarted < 1800) {
        mqttClient.loop();
        yield();
        if (millis() - fallbackStarted >= 300 && millis() - memoryV2CloudSearchLastRx >= 220) break;
        delay(3);
      }
      mqttClient.unsubscribe(topic);
      memoryV2CloudSearchEnd(target, targetSize);
      LOGI("CLOUD", "Memory archive fallback category=%d hits=%u result=%u bytes",
           category, memoryV2CloudHitCount, (unsigned)strlen(target));
    } else {
      memoryV2CloudSearchEnd(target, targetSize);
      LOGW("CLOUD", "Memory archive fallback subscribe failed");
    }
  }
  LOGI("CLOUD", "Memory search category=%d hits=%u result=%u bytes",
       category, memoryV2CloudHitCount, (unsigned)strlen(target));
  return target[0] != '\0';
}

void sendMessage(const char* recipientID, const char* message) {
  if (mqttClient.connected()) {
    char recipientTopic[128];
    // New topic format: scout-net/dm/{RECIPIENT}/from/{ME}
    snprintf(recipientTopic, sizeof(recipientTopic), "scout-net/dm/%s/from/%s", recipientID, DEVICE_ID);

    // The payload is now just the message itself. The sender is in the topic.
    mqttClient.publish(recipientTopic, message);

    // Also add the sent message to our own local message list for a complete chat history
    char localSentMsg[150];
    snprintf(localSentMsg, sizeof(localSentMsg), "To %s: %s", recipientID, message);
    strncpy(receivedMessages[messageIndex].content, localSentMsg, sizeof(receivedMessages[0].content) - 1);
    receivedMessages[messageIndex].content[sizeof(receivedMessages[0].content) - 1] = '\0';
    messageIndex = (messageIndex + 1) % MAX_MESSAGES;
  }
}

inline void clearCloudKey(const char* k) {
  if (mqttClient.connected()) {
    char topic[128];
    snprintf(topic, sizeof(topic), "scout-net/memory/%s/%s", DEVICE_ID, k);
    mqttClient.publish(topic, "", true); // Publish empty retained msg to wipe
  }
}

inline void clearCloudMemory() {
  const char* keys[] = {"name", "loc", "nick", "xf", "rpgStory", "summary", "rpgHP", "aff", "rpgGold", "bday", "rpgC1", "rpgC2", "rpgC3", "rpgSel"};
  for (int i = 0; i < 14; i++) clearCloudKey(keys[i]);
}

// Cloud Personality Restore - Call after MQTT connect, before loading local files
void restoreFromCloud() {
  if (!mqttClient.connected()) {
    LOGW("CLOUD","Restore skipped - MQTT not connected");
    return;
  }
  LOGI("CLOUD","Starting personality restore from cloud...");
  unsigned long startMs = millis();
  
  // Subscribe to all memory topics to receive retained messages
  char topicBuf[128];
  snprintf(topicBuf, sizeof(topicBuf), "scout-net/memory/%s/+", DEVICE_ID);
  mqttClient.subscribe(topicBuf);
  LOGD("CLOUD","Subscribed to %s for restore", topicBuf);
  
  // Wait for retained messages to arrive (max 3 seconds)
  unsigned long startWait = millis();
  while (millis() - startWait < 3000) {
    mqttClient.loop();
    yield();
    delay(10);
  }
  
  // If local memory was empty and cloud filled gaps, save to disk
  if (!localMemoryLoaded && strlen(core_chatSummary) > 0) {
    LOGI("CLOUD","Cloud restored empty local memory — saving to disk");
    saveCoreMemory();
    localMemoryLoaded = true; // Mark as loaded so we don't overwrite again
  }
  
  unsigned long elapsed = millis() - startMs;
  LOGI("CLOUD","Personality restore complete in %lu ms", elapsed);
  cloudRestored = true; // Mark cloud restore as complete
}
