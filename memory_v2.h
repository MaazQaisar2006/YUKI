#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "debug.h"

// Phase 2: structured memory runs beside the existing journal.
#define MEMORY_V2_MAX_FACTS 24
#define MEMORY_V2_FACT_LEN 96

extern char workspace[3200];
extern bool canAllocJson(size_t sz);
extern bool pendingCloudSync;
extern SemaphoreHandle_t memExtractMutex;
static uint32_t crc32Calc(const uint8_t* data, size_t len);
static bool copyFile(const char* src, const char* dst);
static bool atomicWriteFile(const char* path, const char* buf, size_t len);

struct MemoryV2Guard {
  bool locked;
  explicit MemoryV2Guard(TickType_t timeout = pdMS_TO_TICKS(1000))
      : locked(!memExtractMutex || xSemaphoreTake(memExtractMutex, timeout) == pdTRUE) {}
  ~MemoryV2Guard() { if (locked && memExtractMutex) xSemaphoreGive(memExtractMutex); }
};

enum MemoryV2Kind : uint8_t {
  MEMORY_V2_SUMMARY = 0,
  MEMORY_V2_FACT = 1
};

struct MemoryV2Fact {
  char text[MEMORY_V2_FACT_LEN];
  uint32_t firstSeen;
  uint32_t lastSeen;
  uint32_t sequence;
  uint16_t seenCount;
  uint8_t importance;
  uint8_t kind;
  bool active;
};

static MemoryV2Fact memoryV2Facts[MEMORY_V2_MAX_FACTS] = {};
static uint8_t memoryV2FactCount = 0;
static volatile bool memoryV2Dirty = false;
static uint32_t memoryV2Revision = 0;
static MemoryV2Fact memoryV2CloudFacts[MEMORY_V2_MAX_FACTS] = {};
static uint32_t memoryV2CloudRevision = 0;
static uint8_t memoryV2CloudExpected = 0;
static uint32_t memoryV2CloudSeenMask = 0;
static uint32_t memoryV2CloudCommitRevision = 0;
static uint8_t memoryV2CloudCommitExpected = 0;
static uint32_t memoryV2LastMergedCloudRevision = 0;
static bool memoryV2CloudSearchActive = false;
static char memoryV2CloudSearchQuery[160] = {};
static uint8_t memoryV2CloudHitCount = 0;
static uint32_t memoryV2CloudSearchLastRx = 0;

#define MEMORY_V2_CLOUD_HITS 8
struct MemoryV2CloudHit {
  char text[MEMORY_V2_FACT_LEN];
  uint32_t sequence;
  uint16_t seenCount;
  uint8_t importance;
  uint8_t kind;
  bool active;
};
static MemoryV2CloudHit memoryV2CloudHits[MEMORY_V2_CLOUD_HITS] = {};

static inline bool memoryV2CommitCloud(uint32_t revision, uint8_t expected);
static inline bool memoryV2NoteCloudCommit(uint32_t revision, uint8_t expected);

static inline void memoryV2Normalize(const char* source, char* target, size_t targetSize) {
  if (!target || targetSize == 0) return;
  target[0] = '\0';
  if (!source) return;

  while (*source == ' ' || *source == '\n' || *source == '\r' || *source == '\t') source++;
  strncpy(target, source, targetSize - 1);
  target[targetSize - 1] = '\0';

  size_t length = strlen(target);
  while (length > 0 && (target[length - 1] == ' ' || target[length - 1] == '\n' ||
                        target[length - 1] == '\r' || target[length - 1] == '\t')) {
    target[--length] = '\0';
  }
}

static inline bool memoryV2SameText(const char* left, const char* right) {
  if (!left || !right) return false;
  while (*left && *right) {
    char a = *left++;
    char b = *right++;
    if (a >= 'A' && a <= 'Z') a = (char)(a + ('a' - 'A'));
    if (b >= 'A' && b <= 'Z') b = (char)(b + ('a' - 'A'));
    if (a != b) return false;
  }
  return *left == '\0' && *right == '\0';
}

// Voice transcription often omits the question mark. Never treat a question
// as a first-person fact just because it contains phrases such as "I like".
static inline bool memoryV2LooksLikeQuestion(const char* message) {
  if (!message) return false;
  if (strchr(message, '?')) return true;
  char lower[MEMORY_V2_FACT_LEN];
  memoryV2Normalize(message, lower, sizeof(lower));
  for (char* p = lower; *p; p++) *p = tolower((unsigned char)*p);
  // Speech transcripts often prepend a conjunction to a follow-up question.
  char* start = lower;
  while (strncmp(start, "and ", 4) == 0 || strncmp(start, "but ", 4) == 0 ||
         strncmp(start, "also ", 5) == 0) {
    start += strncmp(start, "also ", 5) == 0 ? 5 : 4;
  }
  static const char* const starts[] = {
    "what ", "what's ", "whats ", "who ", "whose ", "where ", "when ", "why ", "how ",
    "do ", "does ", "did ", "can ", "could ", "would ", "will ", "should ", "have ", "has ",
    "is ", "are ", "are you ", "were you ", "is there ", "are there ",
    "am i ", "tell me ", "please tell me ", "guess "
  };
  for (size_t i = 0; i < sizeof(starts) / sizeof(starts[0]); i++)
    if (strncmp(start, starts[i], strlen(starts[i])) == 0) return true;
  return false;
}

static inline bool memoryV2UsableText(const char* text) {
  if (!text || strlen(text) < 8) return false;
  if (strcmp(text, "h: fact") == 0 || strcmp(text, "LOW: fact") == 0) return false;
  if (strcmp(text, "h: favorite color") == 0 || strcmp(text, "LOW: favorite color") == 0) return false;
  char lower[MEMORY_V2_FACT_LEN];
  memoryV2Normalize(text, lower, sizeof(lower));
  for (char* p = lower; *p; p++) *p = tolower((unsigned char)*p);
  // Never persist a question accidentally captured as the value of a fact.
  if (strstr(lower, "favorite color is what ") || strstr(lower, "favourite colour is what ") ||
      strstr(lower, "favorite colour is what ") || strstr(lower, "favourite color is what ") ||
      strstr(lower, "favorite color is do i ") || strstr(lower, "favourite colour is do i ") ||
      strstr(lower, "user studies what do i ") || strstr(lower, "user studies do i ") ||
      strstr(lower, "likes what ") || strstr(lower, "likes do ") || strstr(lower, "likes you ") ||
      strcmp(lower, "h: user likes now") == 0 || strcmp(lower, "user likes now") == 0 ||
      strstr(lower, "favorite color is tell me ") || strstr(lower, "favourite colour is tell me ") ||
      strcmp(lower, "h: user likes to drink") == 0 || strcmp(lower, "user likes to drink") == 0) return false;
  return true;
}

static inline int memoryV2Category(const char* text) {
  if (!text) return -1;
  char lower[MEMORY_V2_FACT_LEN];
  memoryV2Normalize(text, lower, sizeof(lower));
  for (size_t i = 0; lower[i]; i++) {
    if (lower[i] >= 'A' && lower[i] <= 'Z') lower[i] = (char)(lower[i] + ('a' - 'A'));
  }
  if (strstr(lower, "my name is") || strstr(lower, "user name is") || strstr(lower, "name is")) return 1;
  if (strstr(lower, "favorite color") || strstr(lower, " color") || strstr(lower, " colour")) return 2;
  if (strstr(lower, "favorite drink") || strstr(lower, "milkshake") || strstr(lower, "milk shake") || strstr(lower, "beverage")) return 3;
  if (strstr(lower, "study") || strstr(lower, "studying") || strstr(lower, "student")) return 4;
  if (strstr(lower, "years old") || strstr(lower, "age is")) return 5;
  if (strstr(lower, "favorite snack") || strstr(lower, "snack")) return 6;
  bool mentionsWalk = strstr(lower, "walk") || strstr(lower, "walking");
  bool mentionsStuck = strstr(lower, "stuck") || strstr(lower, "blocked");
  bool mentionsCoding = strstr(lower, "programming") || strstr(lower, "coding") ||
                        strstr(lower, "code") || strstr(lower, "debug");
  bool mentionsReset = strstr(lower, "reset") || strstr(lower, "clear my head") ||
                       strstr(lower, "fresh perspective") || strstr(lower, "step away");
  bool mentionsBreak = strstr(lower, "break");
  if (strstr(lower, "reset when stuck") || strstr(lower, "reset when programming") ||
      strstr(lower, "walk helps") || strstr(lower, "walk to reset") ||
      (mentionsWalk && (mentionsStuck || mentionsCoding || mentionsReset)) ||
      (mentionsStuck && (mentionsCoding || mentionsReset || mentionsBreak)) ||
      (mentionsCoding && (mentionsReset || mentionsBreak))) return 7;
  return -1;
}

// Repair common extractor echoes and turn preference fragments into facts
// that remain understandable when recalled without their original message.
static inline void memoryV2CanonicalizeFact(char* text, size_t capacity) {
  if (!text || capacity == 0) return;
  char* body = text;
  if (strncmp(body, "h: ", 3) == 0) body += 3;
  else if (strncmp(body, "LOW: ", 5) == 0) body += 5;
  struct EchoRule { const char* source; const char* canonical; };
  static const EchoRule repeated[] = {
    {"User studies i study ", "User studies "},
    {"User is studying i am studying ", "User is studying "},
    {"User likes i like ", "User likes "},
    {"User loves i love ", "User loves "},
    {"User prefers i prefer ", "User prefers "}
  };
  for (size_t i = 0; i < sizeof(repeated) / sizeof(repeated[0]); i++) {
    size_t n = strlen(repeated[i].source);
    if (strncasecmp(body, repeated[i].source, n) == 0) {
      char repaired[MEMORY_V2_FACT_LEN];
      snprintf(repaired, sizeof(repaired), "%s%s", repeated[i].canonical, body + n);
      strncpy(body, repaired, capacity - (size_t)(body - text) - 1);
      body[capacity - (size_t)(body - text) - 1] = '\0';
      break;
    }
  }
  // Bare fragments from the extractor lose their subject on retrieval.
  if (strncmp(text, "h: ", 3) == 0 &&
      (strstr(body, "milkshake") || strstr(body, "milkshakes")) &&
      strncasecmp(body, "User ", 5) != 0 && strncasecmp(body, "I ", 2) != 0) {
    char canonical[MEMORY_V2_FACT_LEN];
    snprintf(canonical, sizeof(canonical), "h: User likes %s", body);
    strncpy(text, canonical, capacity - 1);
    text[capacity - 1] = '\0';
  }
}

static inline int memoryV2QueryCategory(const char* query) {
  if (!query) return -1;
  char lower[MEMORY_V2_FACT_LEN];
  memoryV2Normalize(query, lower, sizeof(lower));
  for (char* p = lower; *p; p++) *p = tolower((unsigned char)*p);
  int found = -1;
  int count = 0;
  if (strstr(lower, "name") || strstr(lower, "who am i")) { found = 1; count++; }
  if (strstr(lower, "color") || strstr(lower, "colour")) { found = 2; count++; }
  if (strstr(lower, "milkshake") || strstr(lower, "milk shake") || strstr(lower, "drink") || strstr(lower, "flavor") || strstr(lower, "flavour")) { found = 3; count++; }
  if (strstr(lower, "study") || strstr(lower, "student")) { found = 4; count++; }
  if (strstr(lower, "years old") || strstr(lower, "age is") || strstr(lower, "my age") ||
      strstr(lower, "how old")) { found = 5; count++; }
  if (strstr(lower, "snack") || strstr(lower, "treat")) { found = 6; count++; }
  if (strstr(lower, "reset") || strstr(lower, "stuck on programming") ||
      strstr(lower, "stuck in programming") || strstr(lower, "when i get stuck") ||
      strstr(lower, "when i'm stuck") || strstr(lower, "what helps me")) { found = 7; count++; }
  return count == 1 ? found : -1;
}

static inline int memoryV2Find(const char* text, uint8_t kind) {
  for (uint8_t i = 0; i < memoryV2FactCount; i++) {
    if (memoryV2Facts[i].active && memoryV2Facts[i].kind == kind &&
        memoryV2SameText(memoryV2Facts[i].text, text)) return i;
  }
  return -1;
}

static inline uint16_t memoryV2Priority(uint8_t importance, uint16_t seenCount) {
  uint16_t score = (uint16_t)importance * 100;
  uint16_t repeatBonus = seenCount > 20 ? 20 : seenCount;
  return score + repeatBonus * 2;
}

static inline uint8_t memoryV2InitialImportance(const char* text, int category) {
  if (!text) return 0;
  if (strncmp(text, "LOW: ", 5) == 0) return 0;
  // Identity and stable life context should survive casual preferences.
  if (category == 1 || category == 4 || category == 5) return 3;
  if (strncmp(text, "h: ", 3) == 0) return 2;
  // Explicit likes and preferences are useful but can be replaced when corrected.
  if (category == 2 || category == 3 || category == 6) return 2;
  return 1;
}

static inline int memoryV2SelectSlot(uint8_t incomingImportance, uint16_t incomingCount) {
  // Reuse tombstoned entries first. Incrementing the high-water mark before
  // checking them made every correction consume another slot until capacity.
  for (uint8_t i = 0; i < memoryV2FactCount; i++) {
    if (!memoryV2Facts[i].active) return i;
  }
  if (memoryV2FactCount < MEMORY_V2_MAX_FACTS) return memoryV2FactCount++;

  int selected = -1;
  for (uint8_t i = 0; i < memoryV2FactCount; i++) {
    if (selected < 0 ||
        memoryV2Priority(memoryV2Facts[i].importance, memoryV2Facts[i].seenCount) <
        memoryV2Priority(memoryV2Facts[selected].importance, memoryV2Facts[selected].seenCount)) selected = i;
  }
  if (selected >= 0 && memoryV2Priority(incomingImportance, incomingCount) >
                        memoryV2Priority(memoryV2Facts[selected].importance,
                                          memoryV2Facts[selected].seenCount)) return selected;
  return selected;
}

static inline void memoryV2Observe(const char* rawText, uint8_t kind) {
  MemoryV2Guard guard;
  if (!guard.locked) { LOGW("MEM2", "Observe skipped: memory mutex timeout"); return; }
  char text[MEMORY_V2_FACT_LEN];
  memoryV2Normalize(rawText, text, sizeof(text));
  if (kind == MEMORY_V2_FACT) memoryV2CanonicalizeFact(text, sizeof(text));
  if (!memoryV2UsableText(text)) return;

  uint32_t now = millis();
  int existing = memoryV2Find(text, kind);
  if (existing >= 0) {
    MemoryV2Fact& fact = memoryV2Facts[existing];
    fact.lastSeen = now;
    if (fact.seenCount < 65535) fact.seenCount++;
    uint8_t observedImportance = memoryV2InitialImportance(text, memoryV2Category(text));
    if (observedImportance > fact.importance) fact.importance = observedImportance;
    memoryV2Revision++;
    fact.sequence = memoryV2Revision;
    memoryV2Dirty = true;
    LOGI("MEM2", "DUP kind=%u count=%u text=%.70s", kind, fact.seenCount, fact.text);
    return;
  }

  int category = memoryV2Category(text);
  uint8_t importance = memoryV2InitialImportance(text, category);
  if (category >= 0) {
    for (uint8_t i = 0; i < memoryV2FactCount; i++) {
      if (memoryV2Facts[i].active && memoryV2Facts[i].kind == kind &&
          memoryV2Category(memoryV2Facts[i].text) == category) {
        if (importance < memoryV2Facts[i].importance) {
          LOGI("MEM2", "KEEP higher-importance category=%d new=%.50s", category, text);
          return;
        }
        memoryV2Facts[i].active = false;
        memoryV2Dirty = true;
        memoryV2Revision++;
        LOGI("MEM2", "REPLACE category=%d old=%.60s", category, memoryV2Facts[i].text);
      }
    }
  }

  int slot = memoryV2SelectSlot(importance, 1);
  if (slot < 0 || (slot < memoryV2FactCount && memoryV2Facts[slot].active &&
      memoryV2Priority(importance, 1) <=
      memoryV2Priority(memoryV2Facts[slot].importance, memoryV2Facts[slot].seenCount))) {
    LOGW("MEM2", "Full: retained higher-priority memories; dropped %.60s", text);
    return;
  }
  MemoryV2Fact& fact = memoryV2Facts[slot];
  memset(&fact, 0, sizeof(fact));
  strncpy(fact.text, text, sizeof(fact.text) - 1);
  fact.firstSeen = now;
  fact.lastSeen = now;
  fact.seenCount = 1;
  fact.importance = importance;
  fact.kind = kind;
  fact.active = true;
  memoryV2Revision++;
  fact.sequence = memoryV2Revision;
  memoryV2Dirty = true;
  LOGI("MEM2", "ADD kind=%u importance=%u slot=%d text=%.70s", kind, fact.importance, slot, fact.text);
}

static inline void memoryV2ObserveSummary(const char* text) {
  memoryV2Observe(text, MEMORY_V2_SUMMARY);
}

static inline void memoryV2ObserveFact(const char* text) {
  memoryV2Observe(text, MEMORY_V2_FACT);
}

// Capture a few unambiguous first-person statements locally so key facts do
// not depend on a successful cloud extraction. Returns one normalized fact.
static inline bool memoryV2ExplicitLeadIn(const char* message, const char* position) {
  const char* p = message;
  while (p < position) {
    while (p < position && (*p == ' ' || *p == '\t' || *p == ',' || *p == ':' || *p == '-')) p++;
    if (p >= position) break;
    char word[12] = {};
    size_t length = 0;
    while (p < position && ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z'))) {
      if (length + 1 < sizeof(word)) {
        char c = *p;
        if (c >= 'A' && c <= 'Z') c = (char)(c + ('a' - 'A'));
        word[length++] = c;
      }
      p++;
    }
    if (length == 0 || !(strcmp(word, "well") == 0 || strcmp(word, "hmm") == 0 ||
                         strcmp(word, "okay") == 0 || strcmp(word, "ok") == 0 ||
                         strcmp(word, "yeah") == 0 || strcmp(word, "yes") == 0 ||
                         strcmp(word, "so") == 0 || strcmp(word, "hey") == 0 ||
                         strcmp(word, "hi") == 0 || strcmp(word, "hello") == 0 ||
                         strcmp(word, "yuki") == 0 || strcmp(word, "but") == 0 ||
                         strcmp(word, "i") == 0 || strcmp(word, "just") == 0 ||
                         strcmp(word, "told") == 0 || strcmp(word, "you") == 0 ||
                         strcmp(word, "that") == 0 || strcmp(word, "actually") == 0 ||
                         strcmp(word, "because") == 0 || strcmp(word, "and") == 0 ||
                         strcmp(word, "also") == 0)) return false;
  }
  return true;
}

static inline bool memoryV2ExtractExplicitFact(const char* message, char* target, size_t targetSize) {
  if (!message || !target || targetSize == 0) return false;
  target[0] = '\0';
  if (memoryV2LooksLikeQuestion(message)) return false;

  // Preserve the useful, repeatable coping detail that ordinary extraction
  // often misses. Keep the fact specific and self-contained for later recall.
  char lowerMessage[MEMORY_V2_FACT_LEN];
  memoryV2Normalize(message, lowerMessage, sizeof(lowerMessage));
  for (char* p = lowerMessage; *p; p++) *p = tolower((unsigned char)*p);
  bool saysStuck = strstr(lowerMessage, "stuck") || strstr(lowerMessage, "stock");
  if (saysStuck && strstr(lowerMessage, "walk") &&
      (strstr(lowerMessage, "reset") || strstr(lowerMessage, "helps me"))) {
    const char* duration = (strstr(lowerMessage, "10 minute") || strstr(lowerMessage, "ten minute"))
        ? "a ten-minute walk" : "a short walk";
    snprintf(target, targetSize, "h: User takes %s to reset when stuck programming", duration);
    return memoryV2UsableText(target);
  }

  // Accept the common reversed name phrasing used in natural conversation.
  const char* isMyName = strstr(lowerMessage, " is my name");
  if (isMyName && isMyName != lowerMessage) {
    size_t nameLen = (size_t)(isMyName - lowerMessage);
    if (nameLen > 0 && nameLen < 32) {
      snprintf(target, targetSize, "h: User's name is %.*s", (int)nameLen, message);
      return memoryV2UsableText(target);
    }
  }

  struct Rule { const char* phrase; const char* factPrefix; };
  static const Rule rules[] = {
    {"my name is ", "h: User's name is "},
    {"my favorite color is ", "h: User's favorite color is "},
    {"my favourite colour is ", "h: User's favorite color is "},
    {"my favorite colour is ", "h: User's favorite color is "},
    {"my favourite color is ", "h: User's favorite color is "},
    {"my favorite drink is ", "h: User likes to drink "},
    {"my favourite drink is ", "h: User likes to drink "},
    {"my favorite milkshake is ", "h: User likes "},
    {"my favourite milkshake is ", "h: User likes "},
    {"i am studying ", "h: User is studying "},
    {"i'm studying ", "h: User is studying "},
    {"i study ", "h: User studies "},
    {"i am a ", "h: User is a "},
    {"i'm a ", "h: User is a "},
    {"i like to drink ", "h: User likes to drink "},
    {"i like drinking ", "h: User likes to drink "},
    {"i like ", "h: User likes "},
    {"i love ", "h: User loves "},
    {"i prefer ", "h: User prefers "}
  };

  const char* match = nullptr;
  const Rule* matchedRule = nullptr;
  size_t matchedPhraseLen = 0;
  for (size_t r = 0; r < sizeof(rules) / sizeof(rules[0]); r++) {
    size_t phraseLen = strlen(rules[r].phrase);
    for (const char* p = message; *p; p++) {
      size_t i = 0;
      while (i < phraseLen && p[i]) {
        char c = p[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c + ('a' - 'A'));
        if (c != rules[r].phrase[i]) break;
        i++;
      }
      if (i == phraseLen && memoryV2ExplicitLeadIn(message, p)) {
        if (!match || p < match - matchedPhraseLen ||
            (p == match - matchedPhraseLen && phraseLen > matchedPhraseLen)) {
          match = p + phraseLen;
          matchedRule = &rules[r];
          matchedPhraseLen = phraseLen;
        }
      }
    }
  }
  if (!match || !matchedRule) return false;

  while (*match == ' ' || *match == '\t') match++;
  size_t valueLen = strcspn(match, ".!?\r\n");
  // A compound user message may contain several first-person facts. Keep
  // this fact within its clause instead of swallowing the following ones.
  static const char* const clauseStarts[] = {
    " and i ", " and my ", " i study ", " i am ", " i'm ", " i like ",
    " i love ", " i prefer ", " my favorite ", " my favourite "
  };
  for (size_t i = 0; i < valueLen; i++) {
    bool boundary = false;
    for (size_t c = 0; c < sizeof(clauseStarts) / sizeof(clauseStarts[0]); c++) {
      size_t n = strlen(clauseStarts[c]);
      if (i + n <= valueLen && strncasecmp(match + i, clauseStarts[c], n) == 0) {
        boundary = true;
        break;
      }
    }
    if (boundary) { valueLen = i; break; }
  }
  // Keep reminder instructions out of the stored preference value.
  static const char* const trailingInstructions[] = {
    " keep that in mind", " keep that mind", " don't forget", " dont forget",
    " do not forget", " remember that", " remember this", " please remember"
  };
  for (size_t i = 0; i < valueLen; i++) {
    for (size_t m = 0; m < sizeof(trailingInstructions) / sizeof(trailingInstructions[0]); m++) {
      size_t n = strlen(trailingInstructions[m]);
      if (i + n <= valueLen && strncasecmp(match + i, trailingInstructions[m], n) == 0) {
        valueLen = i;
        i = valueLen;
        break;
      }
    }
  }
  while (valueLen > 0 && (match[valueLen - 1] == ' ' || match[valueLen - 1] == '\t' || match[valueLen - 1] == ',')) valueLen--;
  if (valueLen < 2) return false;

  size_t prefixLen = strlen(matchedRule->factPrefix);
  if (prefixLen + valueLen >= targetSize) valueLen = targetSize - prefixLen - 1;
  if (valueLen < 2) return false;
  memcpy(target, matchedRule->factPrefix, prefixLen);
  memcpy(target + prefixLen, match, valueLen);
  target[prefixLen + valueLen] = '\0';
  return memoryV2UsableText(target);
}

static inline uint8_t memoryV2ExtractExplicitFacts(const char* message,
    char facts[][MEMORY_V2_FACT_LEN], uint8_t maxFacts) {
  if (!message || !facts || maxFacts == 0 || memoryV2LooksLikeQuestion(message)) return 0;
  uint8_t count = 0;
  char candidate[MEMORY_V2_FACT_LEN];
  if (memoryV2ExtractExplicitFact(message, candidate, sizeof(candidate))) {
    strncpy(facts[count++], candidate, MEMORY_V2_FACT_LEN - 1);
    facts[count - 1][MEMORY_V2_FACT_LEN - 1] = '\0';
  }
  // Re-run the deterministic parser on coordinated or adjacent first-person
  // clauses so one sentence can yield several separate memories.
  static const char* const clauseStarts[] = {
    " and i ", " and my ", " i study ", " i am ", " i'm ", " i like ",
    " i love ", " i prefer ", " my favorite ", " my favourite "
  };
  const char* scan = message;
  while (count < maxFacts && *scan) {
    const char* next = nullptr;
    for (const char* p = scan; *p; p++) {
      for (size_t c = 0; c < sizeof(clauseStarts) / sizeof(clauseStarts[0]); c++) {
        size_t n = strlen(clauseStarts[c]);
        if (strncasecmp(p, clauseStarts[c], n) == 0) {
          next = p + ((strncmp(clauseStarts[c], " and ", 5) == 0) ? 5 : 1);
          break;
        }
      }
      if (next) break;
    }
    if (!next) break;
    scan = next;
    if (memoryV2ExtractExplicitFact(scan, candidate, sizeof(candidate))) {
      bool duplicate = false;
      for (uint8_t i = 0; i < count; i++) if (memoryV2SameText(facts[i], candidate)) duplicate = true;
      if (!duplicate) {
        strncpy(facts[count++], candidate, MEMORY_V2_FACT_LEN - 1);
        facts[count - 1][MEMORY_V2_FACT_LEN - 1] = '\0';
      }
    }
  }
  return count;
}

// Turn a short answer to Yuki's immediately preceding memory question into a
// fact, e.g. "it's blue" or "strawberry".
static inline bool memoryV2ExtractFollowupFact(const char* message, uint8_t category,
                                               char* target, size_t targetSize) {
  if (!message || !target || targetSize == 0 || memoryV2LooksLikeQuestion(message)) return false;
  target[0] = '\0';
  const char* value = message;
  while (*value == ' ' || *value == '\t') value++;
  char normalizedMessage[MEMORY_V2_FACT_LEN];
  memoryV2Normalize(value, normalizedMessage, sizeof(normalizedMessage));
  for (char* p = normalizedMessage; *p; p++) *p = tolower((unsigned char)*p);
  if (strncmp(normalizedMessage, "what ", 5) == 0 || strncmp(normalizedMessage, "what's ", 7) == 0 ||
      strncmp(normalizedMessage, "what is ", 8) == 0 || strncmp(normalizedMessage, "which ", 6) == 0 ||
      strncmp(normalizedMessage, "who ", 4) == 0 || strncmp(normalizedMessage, "where ", 6) == 0 ||
      strncmp(normalizedMessage, "why ", 4) == 0 || strncmp(normalizedMessage, "how ", 4) == 0 ||
      strncmp(normalizedMessage, "do i ", 5) == 0 || strncmp(normalizedMessage, "do you ", 7) == 0 ||
      strstr(normalizedMessage, "what is my ") || strstr(normalizedMessage, "what do i ") ||
      strstr(normalizedMessage, "tell me what") || strstr(normalizedMessage, "what did i ") ||
      strstr(normalizedMessage, "do you remember")) return false;
  static const char* const leadIns[] = {"it's ", "its ", "the answer is ", "no, ", "well, ", "yeah, "};
  for (size_t i = 0; i < sizeof(leadIns) / sizeof(leadIns[0]); i++) {
    size_t n = strlen(leadIns[i]), j = 0;
    while (j < n && value[j]) {
      char c = value[j];
      if (c >= 'A' && c <= 'Z') c = (char)(c + ('a' - 'A'));
      if (c != leadIns[i][j]) break;
      j++;
    }
    if (j == n) { value += n; break; }
  }
  while (*value == ' ' || *value == '\t') value++;
  // Answers may repeat the fact instead of giving only a short value.
  static const char* const categoryLeadIns[] = {
    "my favorite color is ", "my favourite color is ", "my favorite colour is ", "my favourite colour is ",
    "my name is ", "my favorite drink is ", "my favourite drink is ",
    "i study ", "i am studying ", "i'm studying ", "i like to drink ", "i like drinking "
  };
  for (size_t i = 0; i < sizeof(categoryLeadIns) / sizeof(categoryLeadIns[0]); i++) {
    size_t n = strlen(categoryLeadIns[i]), j = 0;
    while (j < n && value[j]) {
      char c = tolower((unsigned char)value[j]);
      if (c != categoryLeadIns[i][j]) break;
      j++;
    }
    if (j == n) { value += n; break; }
  }
  // Corrective replies such as "not robbery, it's strawberry" provide the
  // corrected value after the explicit correction marker.
  if (category == 3 && strncasecmp(value, "not ", 4) == 0) {
    const char* correction = strstr(value, "it's ");
    if (!correction) correction = strstr(value, "its ");
    if (correction) value = correction + (strncmp(correction, "it's ", 5) == 0 ? 5 : 4);
  }
  while (*value == ' ' || *value == '\t') value++;
  size_t valueLen = strcspn(value, ".!?\r\n");
  while (valueLen > 0 && (value[valueLen - 1] == ' ' || value[valueLen - 1] == '\t')) valueLen--;
  if (valueLen < 1 || valueLen > 48) return false;

  const char* prefix = nullptr;
  if (category == 1) prefix = "h: User's name is ";
  else if (category == 2) prefix = "h: User's favorite color is ";
  else if (category == 3) prefix = "h: User's favorite milkshake flavor is ";
  else if (category == 4) prefix = "h: User studies ";
  else if (category == 6) prefix = "h: User's favorite snack is ";
  if (!prefix) return false;
  size_t prefixLen = strlen(prefix);
  if (prefixLen + valueLen >= targetSize) return false;
  memcpy(target, prefix, prefixLen);
  memcpy(target + prefixLen, value, valueLen);
  target[prefixLen + valueLen] = '\0';
  return memoryV2UsableText(target);
}

static inline bool memoryV2Save() {
  MemoryV2Guard guard;
  if (!guard.locked) { LOGW("MEM2", "Save skipped: memory mutex timeout"); return false; }
  if (!memoryV2Dirty) return true;
  if (!canAllocJson(4096)) {
    LOGW("MEM2", "Save skipped: low heap (%u)", ESP.getFreeHeap());
    return false;
  }

  DynamicJsonDocument doc(4096);
  doc["version"] = 2;
  doc["revision"] = memoryV2Revision;
  JsonArray facts = doc.createNestedArray("facts");
  for (uint8_t i = 0; i < memoryV2FactCount; i++) {
    if (!memoryV2Facts[i].active) continue;
    JsonObject item = facts.createNestedObject();
    item["text"] = memoryV2Facts[i].text;
    item["first"] = memoryV2Facts[i].firstSeen;
    item["last"] = memoryV2Facts[i].lastSeen;
    item["sequence"] = memoryV2Facts[i].sequence;
    item["count"] = memoryV2Facts[i].seenCount;
    item["importance"] = memoryV2Facts[i].importance;
    item["kind"] = memoryV2Facts[i].kind;
  }
  doc["crc"] = (uint32_t)0;
  static char memoryV2SaveBuffer[4096];
  size_t length = serializeJson(doc, memoryV2SaveBuffer, sizeof(memoryV2SaveBuffer));
  if (length == 0 || length >= sizeof(memoryV2SaveBuffer)) return false;
  doc["crc"] = crc32Calc((const uint8_t*)memoryV2SaveBuffer, length);
  length = serializeJson(doc, memoryV2SaveBuffer, sizeof(memoryV2SaveBuffer));
  if (length == 0 || length >= sizeof(memoryV2SaveBuffer) ||
      !atomicWriteFile("/memory_v2.json", memoryV2SaveBuffer, length)) return false;
  copyFile("/memory_v2.json", "/memory_v2.bak");
  memoryV2Dirty = false;
  LOGI("MEM2", "Saved structured memory (%u facts)", memoryV2FactCount);
  return true;
}

static inline bool memoryV2LoadFile(const char* path) {
  File file = LittleFS.open(path, "r");
  if (!file) return false;
  if (!canAllocJson(4096)) {
    file.close();
    LOGW("MEM2", "Load skipped: low heap (%u)", ESP.getFreeHeap());
    return false;
  }

  DynamicJsonDocument doc(4096);
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  if (error) {
    LOGW("MEM2", "%s parse failed: %s", path, error.c_str());
    return false;
  }

  if (doc.containsKey("crc")) {
    uint32_t stored = doc["crc"] | (uint32_t)0;
    doc["crc"] = (uint32_t)0;
    static char verify[4096];
    size_t length = serializeJson(doc, verify, sizeof(verify));
    if (length == 0 || length >= sizeof(verify)) return false;
    uint32_t computed = crc32Calc((const uint8_t*)verify, length);
    if (stored != computed) {
      LOGW("MEM2", "%s CRC mismatch", path);
      return false;
    }
  }

  memoryV2FactCount = 0;
  memoryV2Revision = doc["revision"] | (uint32_t)0;
  bool migratedFacts = false;
  JsonArray facts = doc["facts"].as<JsonArray>();
  for (JsonObject item : facts) {
    if (memoryV2FactCount >= MEMORY_V2_MAX_FACTS) break;
    const char* text = item["text"] | "";
    char canonicalText[MEMORY_V2_FACT_LEN];
    memoryV2Normalize(text, canonicalText, sizeof(canonicalText));
    uint8_t itemKind = item["kind"] | (uint8_t)MEMORY_V2_FACT;
    if (itemKind == MEMORY_V2_FACT) memoryV2CanonicalizeFact(canonicalText, sizeof(canonicalText));
    if (!memoryV2UsableText(canonicalText)) { if (text[0]) migratedFacts = true; continue; }
    if (strcmp(text, canonicalText) != 0) migratedFacts = true;
    MemoryV2Fact& fact = memoryV2Facts[memoryV2FactCount++];
    strncpy(fact.text, canonicalText, sizeof(fact.text) - 1);
    fact.text[sizeof(fact.text) - 1] = '\0';
    fact.firstSeen = item["first"] | (uint32_t)0;
    fact.lastSeen = item["last"] | (uint32_t)0;
    fact.sequence = item["sequence"] | memoryV2Revision;
    fact.seenCount = item["count"] | (uint16_t)1;
    fact.importance = item["importance"] | (uint8_t)0;
    fact.kind = itemKind;
    fact.active = true;
  }
  // Older cloud merges could leave several active values for a single
  // replaceable category. Keep the newest value and collapse exact duplicates.
  for (uint8_t i = 0; i < memoryV2FactCount; i++) {
    if (!memoryV2Facts[i].active) continue;
    int category = memoryV2Category(memoryV2Facts[i].text);
    for (uint8_t j = i + 1; j < memoryV2FactCount; j++) {
      if (!memoryV2Facts[j].active || memoryV2Facts[i].kind != memoryV2Facts[j].kind) continue;
      bool sameText = memoryV2SameText(memoryV2Facts[i].text, memoryV2Facts[j].text);
      int otherCategory = memoryV2Category(memoryV2Facts[j].text);
      if (!sameText && (category < 0 || category != otherCategory)) continue;
      uint8_t keep = i;
      uint8_t drop = j;
      if (memoryV2Facts[j].sequence > memoryV2Facts[i].sequence ||
          (memoryV2Facts[j].sequence == memoryV2Facts[i].sequence &&
           memoryV2Priority(memoryV2Facts[j].importance, memoryV2Facts[j].seenCount) >
           memoryV2Priority(memoryV2Facts[i].importance, memoryV2Facts[i].seenCount))) {
        keep = j;
        drop = i;
      }
      if (sameText) {
        if (memoryV2Facts[drop].seenCount > memoryV2Facts[keep].seenCount)
          memoryV2Facts[keep].seenCount = memoryV2Facts[drop].seenCount;
        if (memoryV2Facts[drop].importance > memoryV2Facts[keep].importance)
          memoryV2Facts[keep].importance = memoryV2Facts[drop].importance;
      }
      memoryV2Facts[drop].active = false;
      migratedFacts = true;
      if (keep == j) break;
    }
  }
  memoryV2Dirty = migratedFacts;
  LOGI("MEM2", "Loaded structured memory from %s (%u facts)", path, memoryV2FactCount);
  return true;
}

static inline void memoryV2Load() {
  if (memoryV2LoadFile("/memory_v2.json")) return;
  if (memoryV2LoadFile("/memory_v2.bak")) return;
  LOGI("MEM2", "No structured memory file; starting empty");
}

static inline void memoryV2SaveIfNeeded() {
  memoryV2Save();
}

static inline void memoryV2BuildContext(char* target, size_t targetSize) {
  if (!target || targetSize == 0) return;
  target[0] = '\0';
  MemoryV2Guard guard;
  if (!guard.locked) { LOGW("MEM2", "Context skipped: memory mutex timeout"); return; }
  size_t used = 0;
  bool included[MEMORY_V2_MAX_FACTS] = {};
  for (uint8_t pass = 0; pass < memoryV2FactCount; pass++) {
    int best = -1;
    for (uint8_t i = 0; i < memoryV2FactCount; i++) {
      if (included[i] || !memoryV2Facts[i].active || memoryV2Facts[i].kind != MEMORY_V2_FACT ||
          !memoryV2UsableText(memoryV2Facts[i].text)) continue;
      if (best < 0 || memoryV2Priority(memoryV2Facts[i].importance, memoryV2Facts[i].seenCount) >
                      memoryV2Priority(memoryV2Facts[best].importance, memoryV2Facts[best].seenCount) ||
          (memoryV2Priority(memoryV2Facts[i].importance, memoryV2Facts[i].seenCount) ==
           memoryV2Priority(memoryV2Facts[best].importance, memoryV2Facts[best].seenCount) && i > best)) best = i;
    }
    if (best < 0) break;
    included[best] = true;
    int written = snprintf(target + used, targetSize - used, "%s%s",
                           used > 0 ? " | " : "", memoryV2Facts[best].text);
    if (written < 0 || (size_t)written >= targetSize - used) continue;
    used += (size_t)written;
  }
}

static inline void memoryV2BuildCategoryContext(int category, char* target, size_t targetSize) {
  if (!target || targetSize == 0) return;
  target[0] = '\0';
  if (category < 0) return;
  MemoryV2Guard guard;
  if (!guard.locked) return;
  size_t used = 0;
  bool included[MEMORY_V2_MAX_FACTS] = {};
  for (uint8_t pass = 0; pass < memoryV2FactCount; pass++) {
    int best = -1;
    for (uint8_t i = 0; i < memoryV2FactCount; i++) {
      if (included[i] || !memoryV2Facts[i].active || memoryV2Facts[i].kind != MEMORY_V2_FACT ||
          memoryV2Category(memoryV2Facts[i].text) != category) continue;
      bool duplicate = false;
      for (uint8_t j = 0; j < memoryV2FactCount; j++) {
        if (included[j] && memoryV2SameText(memoryV2Facts[i].text, memoryV2Facts[j].text)) {
          duplicate = true;
          break;
        }
      }
      if (duplicate) continue;
      if (best < 0 || memoryV2Facts[i].sequence > memoryV2Facts[best].sequence) best = i;
    }
    if (best < 0) break;
    included[best] = true;
    int written = snprintf(target + used, targetSize - used, "%s%s",
                           used ? " | " : "", memoryV2Facts[best].text);
    if (written < 0 || (size_t)written >= targetSize - used) break;
    used += (size_t)written;
  }
}

static inline void memoryV2BuildCoreFacts(char* target, size_t targetSize) {
  if (!target || targetSize == 0) return;
  target[0] = '\0';
  size_t used = 0;
  for (int category = 1; category <= 7; category++) {
    char fact[MEMORY_V2_FACT_LEN] = {};
    memoryV2BuildCategoryContext(category, fact, sizeof(fact));
    if (!fact[0]) continue;
    const char* readable = strncmp(fact, "h: ", 3) == 0 ? fact + 3 :
                           strncmp(fact, "LOW: ", 5) == 0 ? fact + 5 : fact;
    int written = snprintf(target + used, targetSize - used, "%s%s",
                           used ? " | " : "", readable);
    if (written < 0 || (size_t)written >= targetSize - used) break;
    used += (size_t)written;
  }
}

static inline bool memoryV2GetCategoryValue(const char* fact, int category,
                                             char* target, size_t targetSize) {
  if (!fact || !target || targetSize == 0) return false;
  target[0] = '\0';
  char lower[MEMORY_V2_FACT_LEN];
  memoryV2Normalize(fact, lower, sizeof(lower));
  for (char* p = lower; *p; p++) *p = tolower((unsigned char)*p);
  const char* marker = nullptr;
  if (category == 1) marker = strstr(lower, "name is ");
  else if (category == 2) {
    marker = strstr(lower, "color is ");
    if (!marker) marker = strstr(lower, "colour is ");
  } else if (category == 3) {
    marker = strstr(lower, "favorite milkshake flavor is ");
    if (!marker) marker = strstr(lower, "likes to drink ");
    if (!marker) marker = strstr(lower, "likes ");
  } else if (category == 4) {
    marker = strstr(lower, "studies ");
    if (!marker) marker = strstr(lower, "studying ");
    if (!marker) marker = strstr(lower, "is a ");
  } else if (category == 5) {
    marker = strstr(lower, " is ");
    if (!marker) marker = strstr(lower, "im ");
  } else if (category == 6) {
    marker = strstr(lower, "snack is ");
  } else if (category == 7) {
    marker = strstr(lower, "takes ");
  }
  if (!marker) return false;
  size_t offset = (size_t)(marker - lower) + strlen(marker == strstr(lower, "color is ") ? "color is " :
      marker == strstr(lower, "colour is ") ? "colour is " :
      marker == strstr(lower, "favorite milkshake flavor is ") ? "favorite milkshake flavor is " :
      marker == strstr(lower, "likes to drink ") ? "likes to drink " :
      marker == strstr(lower, "likes ") ? "likes " :
      marker == strstr(lower, "studies ") ? "studies " :
      marker == strstr(lower, "studying ") ? "studying " :
      marker == strstr(lower, "is a ") ? "is a " :
      marker == strstr(lower, "snack is ") ? "snack is " :
      marker == strstr(lower, "takes ") ? "takes " :
      marker == strstr(lower, "im ") ? "im " :
      category == 1 ? "name is " : "is ");
  if (offset >= strlen(fact)) return false;
  const char* value = fact + offset;
  size_t length = strcspn(value, "|.!?\r\n");
  if (category == 4) {
    char* student = strstr(value, " student");
    if (student && (size_t)(student - value) < length) length = (size_t)(student - value);
    while (length && (value[length - 1] == ' ' || value[length - 1] == '\t')) length--;
  } else if (category == 5) {
    char* years = strstr(value, " years old");
    if (years && (size_t)(years - value) < length) length = (size_t)(years - value);
  }
  static const char* const trailingInstructions[] = {
    " keep that in mind", " keep that mind", " don't forget", " dont forget",
    " do not forget", " remember that", " remember this", " please remember"
  };
  for (size_t i = 0; i < length; i++) {
    for (size_t m = 0; m < sizeof(trailingInstructions) / sizeof(trailingInstructions[0]); m++) {
      size_t n = strlen(trailingInstructions[m]);
      if (i + n <= length && strncasecmp(value + i, trailingInstructions[m], n) == 0) {
        length = i;
        i = length;
        break;
      }
    }
  }
  while (length && (value[length - 1] == ' ' || value[length - 1] == '\t')) length--;
  if (!length) return false;
  if (length >= targetSize) length = targetSize - 1;
  memcpy(target, value, length);
  target[length] = '\0';
  return true;
}

static inline bool memoryV2HasFacts() {
  MemoryV2Guard guard;
  if (!guard.locked) return false;
  for (uint8_t i = 0; i < memoryV2FactCount; i++) {
    if (memoryV2Facts[i].active && memoryV2Facts[i].kind == MEMORY_V2_FACT &&
        memoryV2UsableText(memoryV2Facts[i].text)) return true;
  }
  return false;
}

static inline uint32_t memoryV2GetRevision() {
  MemoryV2Guard guard;
  if (!guard.locked) return 0;
  return memoryV2Revision;
}

static inline uint8_t memoryV2GetFactCount() {
  MemoryV2Guard guard;
  if (!guard.locked) return 0;
  return memoryV2FactCount;
}

static inline int memoryV2GetCategory(uint8_t slot) {
  MemoryV2Guard guard;
  if (!guard.locked || slot >= memoryV2FactCount || !memoryV2Facts[slot].active) return -1;
  return memoryV2Category(memoryV2Facts[slot].text);
}

static inline bool memoryV2BuildCloudFact(uint8_t slot, char* target, size_t targetSize) {
  MemoryV2Guard guard;
  if (!guard.locked) return false;
  if (!target || targetSize == 0 || slot >= memoryV2FactCount || !memoryV2Facts[slot].active ||
      !memoryV2UsableText(memoryV2Facts[slot].text)) return false;
  DynamicJsonDocument doc(256);
  doc["version"] = 2;
  doc["revision"] = memoryV2Revision;
  doc["slot"] = slot;
  doc["text"] = memoryV2Facts[slot].text;
  doc["first"] = memoryV2Facts[slot].firstSeen;
  doc["last"] = memoryV2Facts[slot].lastSeen;
  doc["sequence"] = memoryV2Facts[slot].sequence;
  doc["count"] = memoryV2Facts[slot].seenCount;
  doc["importance"] = memoryV2Facts[slot].importance;
  doc["kind"] = memoryV2Facts[slot].kind;
  return serializeJson(doc, target, targetSize) > 0;
}

static inline uint32_t memoryV2ArchiveKey(uint8_t slot) {
  MemoryV2Guard guard;
  if (!guard.locked) return 0;
  if (slot >= memoryV2FactCount || !memoryV2Facts[slot].active) return 0;
  return crc32Calc((const uint8_t*)memoryV2Facts[slot].text, strlen(memoryV2Facts[slot].text));
}

static inline bool memoryV2BuildArchiveFact(uint8_t slot, char* target, size_t targetSize,
                                           uint32_t* archiveKey = nullptr,
                                           int* archiveCategory = nullptr) {
  MemoryV2Guard guard;
  if (!guard.locked) return false;
  if (!target || targetSize == 0 || slot >= memoryV2FactCount || !memoryV2Facts[slot].active ||
      !memoryV2UsableText(memoryV2Facts[slot].text)) return false;
  if (archiveKey) *archiveKey = crc32Calc((const uint8_t*)memoryV2Facts[slot].text,
                                          strlen(memoryV2Facts[slot].text));
  if (archiveCategory) *archiveCategory = memoryV2Category(memoryV2Facts[slot].text);
  DynamicJsonDocument doc(256);
  doc["version"] = 2;
  doc["active"] = true;
  doc["text"] = memoryV2Facts[slot].text;
  doc["sequence"] = memoryV2Facts[slot].sequence;
  doc["count"] = memoryV2Facts[slot].seenCount;
  doc["importance"] = memoryV2Facts[slot].importance;
  doc["kind"] = memoryV2Facts[slot].kind;
  return serializeJson(doc, target, targetSize) > 0;
}

static inline void memoryV2CloudSearchBegin(const char* query) {
  memoryV2CloudSearchActive = true;
  memoryV2CloudHitCount = 0;
  memoryV2CloudSearchLastRx = millis();
  memset(memoryV2CloudHits, 0, sizeof(memoryV2CloudHits));
  memoryV2Normalize(query, memoryV2CloudSearchQuery, sizeof(memoryV2CloudSearchQuery));
  for (char* p = memoryV2CloudSearchQuery; *p; p++) *p = tolower((unsigned char)*p);
}

static inline bool memoryV2CloudHitOutranks(const MemoryV2CloudHit& left,
                                            const MemoryV2CloudHit& right,
                                            int queryCategory) {
  if (queryCategory >= 0 && left.sequence != right.sequence) return left.sequence > right.sequence;
  if (left.importance != right.importance) return left.importance > right.importance;
  if (left.seenCount != right.seenCount) return left.seenCount > right.seenCount;
  return left.sequence > right.sequence;
}

static inline bool memoryV2AcceptCloudArchiveFact(const char* payload) {
  if (!memoryV2CloudSearchActive || !payload) return false;
  DynamicJsonDocument doc(320);
  if (deserializeJson(doc, payload)) return false;
  if (!(doc["active"] | false)) return false;
  const char* text = doc["text"] | "";
  MemoryV2CloudHit candidate = {};
  memoryV2Normalize(text, candidate.text, sizeof(candidate.text));
  candidate.kind = doc["kind"] | (uint8_t)MEMORY_V2_FACT;
  if (candidate.kind == MEMORY_V2_FACT) memoryV2CanonicalizeFact(candidate.text, sizeof(candidate.text));
  if (!memoryV2UsableText(candidate.text)) return false;

  int queryCategory = memoryV2QueryCategory(memoryV2CloudSearchQuery);
  int category = memoryV2Category(candidate.text);
  if (queryCategory >= 0 && category != queryCategory) return false;

  candidate.sequence = doc["sequence"] | (uint32_t)0;
  candidate.seenCount = doc["count"] | (uint16_t)1;
  candidate.importance = doc["importance"] | (uint8_t)0;
  candidate.active = true;

  int existing = -1;
  for (uint8_t i = 0; i < memoryV2CloudHitCount; i++) {
    int hitCategory = memoryV2Category(memoryV2CloudHits[i].text);
    if ((category >= 0 && hitCategory == category) ||
        (category < 0 && memoryV2SameText(memoryV2CloudHits[i].text, candidate.text))) {
      existing = i;
      break;
    }
  }
  if (existing >= 0) {
    if (memoryV2CloudHitOutranks(candidate, memoryV2CloudHits[existing], queryCategory)) {
      memoryV2CloudHits[existing] = candidate;
    }
    memoryV2CloudSearchLastRx = millis();
    return true;
  }

  int slot = -1;
  if (memoryV2CloudHitCount < MEMORY_V2_CLOUD_HITS) {
    slot = memoryV2CloudHitCount++;
  } else {
    int weakest = 0;
    for (uint8_t i = 1; i < memoryV2CloudHitCount; i++) {
      if (memoryV2CloudHitOutranks(memoryV2CloudHits[weakest], memoryV2CloudHits[i], queryCategory)) weakest = i;
    }
    if (memoryV2CloudHitOutranks(candidate, memoryV2CloudHits[weakest], queryCategory)) slot = weakest;
  }
  if (slot >= 0) memoryV2CloudHits[slot] = candidate;
  memoryV2CloudSearchLastRx = millis();
  return slot >= 0;
}

static inline void memoryV2CloudSearchEnd(char* target, size_t targetSize) {
  if (!target || targetSize == 0) return;
  target[0] = '\0';
  size_t used = 0;
  int queryCategory = memoryV2QueryCategory(memoryV2CloudSearchQuery);
  bool included[MEMORY_V2_CLOUD_HITS] = {};
  for (uint8_t pass = 0; pass < memoryV2CloudHitCount; pass++) {
    int best = -1;
    for (uint8_t i = 0; i < memoryV2CloudHitCount; i++) {
      if (!memoryV2CloudHits[i].active || included[i]) continue;
      if (best < 0 || memoryV2CloudHitOutranks(memoryV2CloudHits[i], memoryV2CloudHits[best], queryCategory)) best = i;
    }
    if (best < 0) break;
    included[best] = true;
    int written = snprintf(target + used, targetSize - used, "%s%s",
                           used ? " | " : "", memoryV2CloudHits[best].text);
    if (written < 0 || (size_t)written >= targetSize - used) continue;
    used += (size_t)written;
  }
  memoryV2CloudSearchActive = false;
}

static inline bool memoryV2HasCategory(int category) {
  MemoryV2Guard guard;
  if (!guard.locked) return false;
  if (category < 0) return false;
  for (uint8_t i = 0; i < memoryV2FactCount; i++) {
    if (memoryV2Facts[i].active && memoryV2Facts[i].kind == MEMORY_V2_FACT &&
        memoryV2Category(memoryV2Facts[i].text) == category) return true;
  }
  return false;
}

static inline bool memoryV2AcceptCloudFact(const char* payload) {
  if (!payload) return false;
  DynamicJsonDocument doc(256);
  if (deserializeJson(doc, payload)) return false;
  uint32_t revision = doc["revision"] | (uint32_t)0;
  uint8_t slot = doc["slot"] | (uint8_t)MEMORY_V2_MAX_FACTS;
  const char* text = doc["text"] | "";
  if (revision == 0 || slot >= MEMORY_V2_MAX_FACTS || !memoryV2UsableText(text)) return false;
  if (memoryV2CloudRevision != 0 && revision < memoryV2CloudRevision) return false;
  if (revision != memoryV2CloudRevision) {
    memoryV2CloudRevision = revision;
    memoryV2CloudExpected = 0;
    memoryV2CloudSeenMask = 0;
    memset(memoryV2CloudFacts, 0, sizeof(memoryV2CloudFacts));
  }
  MemoryV2Fact& fact = memoryV2CloudFacts[slot];
  strncpy(fact.text, text, sizeof(fact.text) - 1);
  fact.text[sizeof(fact.text) - 1] = '\0';
  fact.firstSeen = doc["first"] | (uint32_t)0;
  fact.lastSeen = doc["last"] | (uint32_t)0;
  fact.sequence = doc["sequence"] | revision;
  fact.seenCount = doc["count"] | (uint16_t)1;
  fact.importance = doc["importance"] | (uint8_t)0;
  fact.kind = doc["kind"] | (uint8_t)MEMORY_V2_FACT;
  fact.active = true;
  memoryV2CloudSeenMask |= (uint32_t)1 << slot;
  if (memoryV2CloudCommitRevision == revision) {
    memoryV2CommitCloud(revision, memoryV2CloudCommitExpected);
  }
  return true;
}

static inline bool memoryV2CommitCloud(uint32_t revision, uint8_t expected) {
  MemoryV2Guard guard;
  if (!guard.locked) { LOGW("MEM2", "Cloud merge skipped: memory mutex timeout"); return false; }
  if (revision == 0 || revision != memoryV2CloudRevision || expected > MEMORY_V2_MAX_FACTS) return false;
  if (revision == memoryV2LastMergedCloudRevision) return false;
  uint8_t seen = 0;
  for (uint8_t i = 0; i < MEMORY_V2_MAX_FACTS; i++) {
    if (memoryV2CloudSeenMask & ((uint32_t)1 << i)) seen++;
  }
  if (seen != expected) return false;

  bool changed = false;
  for (uint8_t i = 0; i < MEMORY_V2_MAX_FACTS; i++) {
    if (!(memoryV2CloudSeenMask & ((uint32_t)1 << i))) continue;
    MemoryV2Fact& incoming = memoryV2CloudFacts[i];
    int local = memoryV2Find(incoming.text, incoming.kind);
    if (local >= 0) {
      MemoryV2Fact& current = memoryV2Facts[local];
      if (incoming.seenCount > current.seenCount) {
        current.seenCount = incoming.seenCount;
        changed = true;
      }
      if (incoming.importance > current.importance) {
        current.importance = incoming.importance;
        changed = true;
      }
      if (incoming.sequence > current.sequence) {
        current.sequence = incoming.sequence;
        changed = true;
      }
      continue;
    }

    int category = memoryV2Category(incoming.text);
    int conflictingLocal = -1;
    if (category >= 0) {
      for (uint8_t j = 0; j < memoryV2FactCount; j++) {
        if (memoryV2Facts[j].active && memoryV2Facts[j].kind == incoming.kind &&
            memoryV2Category(memoryV2Facts[j].text) == category) {
          conflictingLocal = j;
          break;
        }
      }
    }
    if (conflictingLocal >= 0 && incoming.importance <= memoryV2Facts[conflictingLocal].importance) {
      LOGI("MEM2", "Cloud conflict kept local category=%d local=%.40s cloud=%.40s", category,
           memoryV2Facts[conflictingLocal].text, incoming.text);
      continue;
    }
    if (conflictingLocal >= 0) memoryV2Facts[conflictingLocal].active = false;

    int slot = memoryV2SelectSlot(incoming.importance, incoming.seenCount);
    if (slot < 0 || (slot < memoryV2FactCount && memoryV2Facts[slot].active &&
        memoryV2Priority(incoming.importance, incoming.seenCount) <=
        memoryV2Priority(memoryV2Facts[slot].importance, memoryV2Facts[slot].seenCount))) {
      LOGI("MEM2", "Cloud fact skipped; local memory has higher priority: %.55s", incoming.text);
      continue;
    }
    memoryV2Facts[slot] = incoming;
    changed = true;
  }

  if (revision > memoryV2Revision) {
    memoryV2Revision = revision;
    changed = true;
  }
  if (changed) {
    if (revision <= memoryV2Revision) memoryV2Revision++;
    memoryV2Dirty = true;
    pendingCloudSync = true;
  }
  memoryV2LastMergedCloudRevision = revision;
  memoryV2CloudCommitRevision = 0;
  memoryV2CloudCommitExpected = 0;
  LOGI("MEM2", "Cloud revision %lu merged (%u facts, changed=%u; local facts preserved)",
       (unsigned long)revision, seen, changed ? 1 : 0);
  return changed;
}

static inline bool memoryV2NoteCloudCommit(uint32_t revision, uint8_t expected) {
  memoryV2CloudCommitRevision = revision;
  memoryV2CloudCommitExpected = expected;
  return memoryV2CommitCloud(revision, expected);
}

static inline void memoryV2Report() {
  LOGI("MEM2", "REPORT active=%u capacity=%u", memoryV2FactCount, MEMORY_V2_MAX_FACTS);
  for (uint8_t i = 0; i < memoryV2FactCount; i++) {
    if (memoryV2Facts[i].active) {
      LOGI("MEM2", "ITEM slot=%u kind=%u importance=%u count=%u text=%.70s",
           i, memoryV2Facts[i].kind, memoryV2Facts[i].importance,
           memoryV2Facts[i].seenCount, memoryV2Facts[i].text);
    }
  }
}
