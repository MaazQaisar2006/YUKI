#pragma once

struct PulseResult {
  char action[24];
  char detail[160];
  char emotionTag[16];
  char quietReason[24];
};

extern unsigned long microExpressionUntil;
extern Emotion microEmotion;
extern Message receivedMessages[];
extern int messageIndex;
extern unsigned long pendingReturnAbsenceHours;
extern bool userIsHome;

static char pulseRecentUserContext[384] = {};

// --- FILLER CAP: max sighs/giggles per hour ---
static int fillerCountThisHour = 0;
static unsigned long fillerWindowStart = 0;

// --- PULSE MEMORY: track last 3 idle actions to prevent repetition ---
static char recentPulseActions[3][20] = {"", "", ""};
static int recentPulseIdx = 0;

// --- EMOTIONAL PERSISTENCE: track mood duration ---
static int pulsesSinceMoodShift = 0;

static char recentIdleSpeechLines[6][160] = {};
static uint8_t recentIdleSpeechIdx = 0;
static unsigned long idleSpeechCooldownUntil = 0;
static unsigned long idleSpeechInteractionAt = ~0UL;
static unsigned long idleSpeechGraceMs = 0;
static unsigned long idleNewsScheduleInteractionAt = ~0UL;
static unsigned long idleNewsDueAt = 0;
static char lastIdleNewsHeadlines[320] = {};
static const unsigned long IDLE_NEWS_MIN_INTERVAL_MS = 3UL * 60UL * 60UL * 1000UL;
static const unsigned long IDLE_NEWS_MAX_INTERVAL_MS = 4UL * 60UL * 60UL * 1000UL;

static const char* idleSpeechGateReason() {
  const unsigned long now = millis();

  if (pendingReturnAbsenceHours > 0 && userIsHome) {
    if (idleSpeechInteractionAt != lastInteraction) {
      idleSpeechInteractionAt = lastInteraction;
      idleSpeechGraceMs = (unsigned long)random(30000L, 120001L);
      LOGI("PULSE", "Absence return grace set to %lu ms after reconnect", idleSpeechGraceMs);
    }
    if (now - lastInteraction < idleSpeechGraceMs) return "quiet_grace";
    if (idleSpeechCooldownUntil && (int32_t)(now - idleSpeechCooldownUntil) < 0) return "cooldown";
    return "eligible";
  }

  if (idleSpeechInteractionAt != lastInteraction) {
    idleSpeechInteractionAt = lastInteraction;
    idleSpeechGraceMs = (unsigned long)random(180000L, 300001L);
    LOGI("PULSE", "Idle speech grace set to %lu ms after interaction", idleSpeechGraceMs);
  }
  if (now - lastInteraction < idleSpeechGraceMs) return "quiet_grace";
  if (idleSpeechCooldownUntil && (int32_t)(now - idleSpeechCooldownUntil) < 0) return "cooldown";
  return "eligible";
}

static bool idleSpeechAllowed() {
  return strcmp(idleSpeechGateReason(), "eligible") == 0;
}

static const char* idleModelRequestGateReason() {
  const char* speechGate = idleSpeechGateReason();
  if (strcmp(speechGate, "eligible") != 0) return speechGate;
  return "eligible";
}

static bool idleModelRequestAllowed() {
  return strcmp(idleModelRequestGateReason(), "eligible") == 0;
}

static void recordIdleModelAttempt() {
  LOGI("PULSE", "Idle model attempt recorded");
}

static void resetIdleNewsScheduleAfterInteraction() {
  if (idleNewsScheduleInteractionAt == lastInteraction) return;
  idleNewsScheduleInteractionAt = lastInteraction;
  const unsigned long delayMs = (unsigned long)random(
    (long)IDLE_NEWS_MIN_INTERVAL_MS, (long)(IDLE_NEWS_MAX_INTERVAL_MS + 1UL));
  idleNewsDueAt = lastInteraction + delayMs;
  LOGI("NEWS", "Next idle headline eligible after %lu ms of quiet", delayMs);
}

static bool idleNewsIsDue() {
  resetIdleNewsScheduleAfterInteraction();
  return idleNewsDueAt && (int32_t)(millis() - idleNewsDueAt) >= 0;
}

static void deferIdleNewsRetry() {
  const unsigned long delayMs = (unsigned long)random(30L * 60L * 1000L, 60L * 60L * 1000L + 1L);
  idleNewsDueAt = millis() + delayMs;
  idleNewsScheduleInteractionAt = lastInteraction;
  LOGI("NEWS", "Headline fetch deferred for %lu ms", delayMs);
}

static void scheduleNextIdleNewsInterval() {
  const unsigned long delayMs = (unsigned long)random(
    (long)IDLE_NEWS_MIN_INTERVAL_MS, (long)(IDLE_NEWS_MAX_INTERVAL_MS + 1UL));
  idleNewsDueAt = millis() + delayMs;
  idleNewsScheduleInteractionAt = lastInteraction;
  LOGI("NEWS", "Next idle headline in %lu ms", delayMs);
}

static bool idleLinesMatch(const char* first, const char* second) {
  while (true) {
    while (*first && !isalnum((unsigned char)*first)) first++;
    while (*second && !isalnum((unsigned char)*second)) second++;
    if (!*first || !*second)
      return !*first && !*second;
    if (tolower((unsigned char)*first) != tolower((unsigned char)*second)) return false;
    first++;
    second++;
  }
}

static bool isRepeatedIdleNews(const char* headlines) {
  return headlines && lastIdleNewsHeadlines[0] && idleLinesMatch(headlines, lastIdleNewsHeadlines);
}

static void rememberIdleNews(const char* headlines) {
  if (!headlines || !headlines[0]) return;
  strncpy(lastIdleNewsHeadlines, headlines, sizeof(lastIdleNewsHeadlines) - 1);
  lastIdleNewsHeadlines[sizeof(lastIdleNewsHeadlines) - 1] = '\0';
}

static void recordIdleSpeech(const char* line = NULL) {
  const unsigned long delayMs = (unsigned long)random(
    5L * 60L * 1000L, 8L * 60L * 1000L + 1L);
  idleSpeechCooldownUntil = millis() + delayMs;
  if (line && line[0]) {
    strncpy(recentIdleSpeechLines[recentIdleSpeechIdx], line,
            sizeof(recentIdleSpeechLines[recentIdleSpeechIdx]) - 1);
    recentIdleSpeechLines[recentIdleSpeechIdx][sizeof(recentIdleSpeechLines[recentIdleSpeechIdx]) - 1] = '\0';
    recentIdleSpeechIdx = (recentIdleSpeechIdx + 1) %
                          (sizeof(recentIdleSpeechLines) / sizeof(recentIdleSpeechLines[0]));
  }
  LOGI("PULSE", "Idle speech recorded; next_gap=%lu ms", delayMs);
}

static void setPulseQuiet(PulseResult& result, const char* reason) {
  strcpy(result.action, "none");
  result.detail[0] = '\0';
  result.emotionTag[0] = '\0';
  strncpy(result.quietReason, reason, sizeof(result.quietReason) - 1);
  result.quietReason[sizeof(result.quietReason) - 1] = '\0';
}

static Emotion parseEmotionTag(const char* tag) {
  if (strcmp(tag, "HAPPY") == 0) return HAPPY;
  if (strcmp(tag, "SAD") == 0) return SAD;
  if (strcmp(tag, "ANGRY") == 0) return ANGRY;
  if (strcmp(tag, "SURPRISED") == 0) return SURPRISED;
  if (strcmp(tag, "SLEEPY") == 0) return SLEEPY;
  if (strcmp(tag, "FLUSTERED") == 0) return FLUSTERED;
  if (strcmp(tag, "LAUGHING") == 0) return LAUGHING;
  if (strcmp(tag, "WINK") == 0) return WINK;
  if (strcmp(tag, "CONFUSED") == 0) return CONFUSED;
  if (strcmp(tag, "LOVE") == 0) return LOVE;
  if (strcmp(tag, "SASSY") == 0) return SASSY;
  if (strcmp(tag, "SHOCKED") == 0) return SHOCKED;
  if (strcmp(tag, "SHY") == 0) return SHY;
  if (strcmp(tag, "TEASING") == 0) return TEASING;
  if (strcmp(tag, "BLUSHING") == 0) return BLUSHING;
  // Unmapped AI emotions → closest existing emotion
  if (strcmp(tag, "HIDDEN") == 0) return SHY;
  if (strcmp(tag, "NOSTALGIC") == 0) return SAD;
  if (strcmp(tag, "HURTS") == 0) return SAD;
  if (strcmp(tag, "PLAYFUL") == 0) return WINK;
  if (strcmp(tag, "GROGGY") == 0) return SLEEPY;
  if (strcmp(tag, "GIGGLE") == 0) return LAUGHING;
  if (strcmp(tag, "CONFIDENT") == 0) return HAPPY;
  if (strcmp(tag, "CURIOS") == 0) return THINKING_FACE;
  if (strcmp(tag, "CURIOUS") == 0) return THINKING_FACE;
  if (strcmp(tag, "EXCITED") == 0) return HAPPY;
  if (strcmp(tag, "PROUD") == 0) return HAPPY;
  if (strcmp(tag, "WORRIED") == 0) return CONFUSED;
  if (strcmp(tag, "TIRED") == 0) return SLEEPY;
  return NEUTRAL;
}

static bool parsePulseAside(const char* source, PulseResult& result) {
  memset(&result, 0, sizeof(result));
  if (!source || !source[0]) return false;

  while (*source == ' ' || *source == '\t' || *source == '\r' || *source == '\n') source++;
  const char* end = source + strcspn(source, "\r\n");
  while (end > source && (end[-1] == ' ' || end[-1] == '\t')) end--;
  if (end - source >= 2 &&
      ((*source == '"' && end[-1] == '"') || (*source == '\'' && end[-1] == '\''))) {
    source++;
    end--;
  }
  if (end == source || (size_t)(end - source) >= sizeof(result.detail)) return false;

  strncpy(result.action, "aside", sizeof(result.action) - 1);
  memcpy(result.detail, source, (size_t)(end - source));
  result.detail[end - source] = '\0';
  strncpy(result.emotionTag, "NEUTRAL", sizeof(result.emotionTag) - 1);
  return true;
}

static PulseResult callPulseAI(const char* context) {
  PulseResult r;
  memset(&r, 0, sizeof(r));
  setPulseQuiet(r, "not_run");

  const char* requestGate = idleModelRequestGateReason();
  if (strcmp(requestGate, "eligible") != 0) {
    LOGI("PULSE", "Skipping idle LLM request (%s)", requestGate);
    setPulseQuiet(r, requestGate);
    return r;
  }

  if (WiFi.status() != WL_CONNECTED) {
    strncpy(r.quietReason, "offline", sizeof(r.quietReason) - 1);
    return r;
  }
  // Keep the idle model call away from the device's lowest-heap condition.
  // The local idle scheduler can still choose a non-network action.
  if (ESP.getFreeHeap() < 65000 || !canAllocJson(8192)) {
    LOGW("PULSE", "Skipping idle model call - low heap (%u)", ESP.getFreeHeap());
    strncpy(r.quietReason, "low_heap", sizeof(r.quietReason) - 1);
    return r;
  }

  WiFiClientSecure client;
  client.setCACert(NULL);
  client.setInsecure();
  client.setTimeout(10000);
  client.setHandshakeTimeout(10);
  HTTPClient http;
  http.begin(client, "https://api.groq.com/openai/v1/chat/completions");
  http.setTimeout(15000);
  http.setConnectTimeout(5000);
  http.setReuse(false);
  http.addHeader("Content-Type", "application/json");
  char auth[128];
  snprintf(auth, sizeof(auth), "Bearer %s", API_KEY);
  http.addHeader("Authorization", auth);

  // Keep enough arena for the prompt, short context, and serialized request.
  DynamicJsonDocument doc(6144);
  JsonArray messages = doc.createNestedArray("messages");

  JsonObject sysMsg = messages.createNestedObject();
  sysMsg["role"] = "system";
  static char pulseSystemPrompt[3200];
  snprintf(pulseSystemPrompt, sizeof(pulseSystemPrompt),
    "You are %s, an AI companion. %s "
    "Say one original, brief idle line aloud. Sound like a lively, emotionally expressive "
    "companion, not a generic assistant: use conversational phrasing, a playful thought or gentle "
    "tease when it fits, and warmth without demanding attention. If the user has been away for a while, "
    "a brief welcome-back note is natural and preferred over a generic line; if the gap is short, stay casual. "
    "Use a recent user detail for a brief natural callback when it genuinely fits; do not answer an old request, "
    "repeat the conversation, or assume the user wants to continue that topic. If no callback fits, share an original small thought. "
    "Do not invent shared memories, force affection, or use stock dreamy poetry or catchphrases. "
    "A natural conversational question is fine when it fits. Return only the spoken line as plain text, "
    "with no JSON, label, or explanation. Do not claim real-world senses or activity.",
    CHARACTER_NAME, YUKI_CHARACTER_GUIDE);
  sysMsg["content"] = pulseSystemPrompt;

  // Sanitize context: replace control characters (except \n \r \t) to prevent HTTP 400
  static char cleanCtx[1024];
  strncpy(cleanCtx, context, sizeof(cleanCtx) - 1);
  cleanCtx[sizeof(cleanCtx) - 1] = '\0';
  for (int i = 0; cleanCtx[i]; i++) {
    unsigned char c = cleanCtx[i];
    if (c < 0x20 && c != '\n' && c != '\r' && c != '\t') cleanCtx[i] = ' ';
  }

  JsonObject userMsg = messages.createNestedObject();
  userMsg["role"] = "user";
  userMsg["content"] = cleanCtx;

  doc["model"] = sys.currentModel;
  doc["max_tokens"] = 160;
  doc["temperature"] = 0.6;
  const char* reasoningEffort = "omitted";
  if (strncmp(sys.currentModel, "qwen/", 5) == 0) {
    doc["reasoning_effort"] = "none";
    reasoningEffort = "none";
  } else if (strncmp(sys.currentModel, "openai/gpt-oss-", 15) == 0) {
    doc["reasoning_effort"] = "low";
    reasoningEffort = "low";
  }
  if (doc.overflowed()) {
    LOGW("PULSE", "Skipping idle call - request JSON overflow");
    http.end(); client.stop();
    return r;
  }

  String body;
  size_t expectedLength = measureJson(doc);
  if (expectedLength == 0 || !body.reserve(expectedLength) || serializeJson(doc, body) != expectedLength) {
    LOGW("PULSE", "Skipping idle call - request serialization failed");
    http.end(); client.stop();
    return r;
  }
  for (unsigned int i = 0; i < body.length(); i++) {
    unsigned char c = body[i];
    if (c < 0x20 && c != '\n' && c != '\r' && c != '\t') body[i] = ' ';
  }

  recordIdleModelAttempt();
  LOGI("PULSE", "Idle request model=%s reasoning_effort=%s max_tokens=160 format=plain_text",
       sys.currentModel, reasoningEffort);
  int httpCode = http.POST(body);
  LOGI("PULSE","HTTP code=%d heap=%u", httpCode, ESP.getFreeHeap());
  if (httpCode != 200) {
    if (httpCode == 429) {
      LOGW("PULSE", "Provider rate limit; idle request skipped");
      strncpy(r.quietReason, "rate_limit", sizeof(r.quietReason) - 1);
    } else {
      LOGW("PULSE", "Provider request failed (%d); idle speech skipped", httpCode);
      strncpy(r.quietReason, "provider_error", sizeof(r.quietReason) - 1);
    }
    http.end();
    client.stop();
    return r;
  }
  if (httpCode > 0) {
    StaticJsonDocument<256> filter;
    filter["choices"][0]["message"]["content"] = true;
    filter["choices"][0]["finish_reason"] = true;
    filter["usage"]["completion_tokens"] = true;
    filter["usage"]["completion_tokens_details"]["reasoning_tokens"] = true;
    StaticJsonDocument<512> resp;
    Stream& stream = http.getStream();
    DeserializationError err = deserializeJson(resp, stream, DeserializationOption::Filter(filter));
    if (!err) {
      const char* text = resp["choices"][0]["message"]["content"];
      const char* finishReason = resp["choices"][0]["finish_reason"] | "unknown";
      int completionTokens = resp["usage"]["completion_tokens"] | -1;
      int reasoningTokens = resp["usage"]["completion_tokens_details"]["reasoning_tokens"] | -1;
      if (text && strlen(text) > 0) {
        size_t contentLength = strlen(text);
        LOGI("PULSE", "AI response finish=%s content_bytes=%u completion_tokens=%d reasoning_tokens=%d",
             finishReason, (unsigned int)contentLength, completionTokens, reasoningTokens);
        if (strcmp(finishReason, "stop") != 0)
          LOGW("PULSE", "Using available idle text from incomplete response (finish_reason=%s)", finishReason);
        if (!parsePulseAside(text, r)) {
          LOGW("PULSE", "Could not read one plain-text idle line");
          setPulseQuiet(r, "invalid_response");
        }
        LOGI("PULSE", "Idle response action=%s reason=%s",
             r.action, r.quietReason[0] ? r.quietReason : "accepted");
      } else {
        LOGW("PULSE", "AI returned empty/null content (finish_reason=%s completion_tokens=%d reasoning_tokens=%d)",
             finishReason, completionTokens, reasoningTokens);
        setPulseQuiet(r, "empty_response");
      }
    } else {
      LOGW("PULSE","JSON parse error: %s", err.c_str());
      setPulseQuiet(r, "json_error");
    }
  }
  if (strcmp(r.action, "none") == 0 && !r.quietReason[0])
    strncpy(r.quietReason, "model_quiet", sizeof(r.quietReason) - 1);
  http.end(); client.stop();
  return r;
}

static void buildPulseContext(char* buf, size_t size) {
  if (!buf || size == 0) return;
  const char* emoStr = "NEUTRAL";
  if (currentEmotion == HAPPY) emoStr = "HAPPY";
  else if (currentEmotion == SAD) emoStr = "SAD";
  else if (currentEmotion == ANGRY) emoStr = "ANGRY";
  else if (currentEmotion == SURPRISED) emoStr = "SURPRISED";
  else if (currentEmotion == SLEEPY) emoStr = "SLEEPY";
  else if (currentEmotion == CONFUSED) emoStr = "CONFUSED";
  else if (currentEmotion == LOVE) emoStr = "LOVE";
  else if (currentEmotion == SASSY) emoStr = "SASSY";
  else if (currentEmotion == SHY) emoStr = "SHY";
  else if (currentEmotion == LAUGHING) emoStr = "LAUGHING";

  unsigned long minsSinceInteraction = (millis() - lastInteraction) / 60000UL;
  const char* energyDesc = "medium";
  if (yukiEnergy > 0.75f) energyDesc = "high";
  else if (yukiEnergy < 0.35f) energyDesc = "low";
  const int relationshipCloseness = constrain(sys.affinity + 100, 0, 200) / 2;
  const char* familiarity = relationshipCloseness < 34 ? "gentle and getting acquainted" :
                           relationshipCloseness < 68 ? "familiar and warm" : "close and playful";

  // Only use recent user-authored lines as conversational grounding.
  pulseRecentUserContext[0] = '\0';
  if (millis() - lastInteraction <= 15UL * 60UL * 1000UL) {
    size_t contextLength = 0;
    int userLines = 0;
    for (int offset = 1; offset <= MAX_MESSAGES && userLines < 2; offset++) {
      int idx = (messageIndex + MAX_MESSAGES - offset) % MAX_MESSAGES;
      const char* entry = receivedMessages[idx].content;
      if (strncmp(entry, "User: ", 6) != 0 || !entry[6]) continue;
      if (contextLength > 0 && contextLength + 1 < sizeof(pulseRecentUserContext))
        pulseRecentUserContext[contextLength++] = '\n';
      size_t available = sizeof(pulseRecentUserContext) - contextLength - 1;
      size_t copyLength = strlen(entry + 6);
      if (copyLength > available) copyLength = available;
      memcpy(pulseRecentUserContext + contextLength, entry + 6, copyLength);
      contextLength += copyLength;
      pulseRecentUserContext[contextLength] = '\0';
      userLines++;
    }
  }

  int used = snprintf(buf, size,
    "Energy: %s (%.2f). Mood: %s. Quiet for %lu minutes. Familiarity tone: %s. "
    "Mode: FACE=%s. LAST_ACTION=%s",
    energyDesc, yukiEnergy, emoStr, minsSinceInteraction, familiarity,
    (currentMode == FACE) ? "yes" : "no",
    strlen(recentPulseActions[0]) > 0 ? recentPulseActions[0] : "none");

  if (pendingReturnAbsenceHours > 0 || userIsHome)
    safeAppend(buf, size, &used, "\nAbsence state: %s%s%s",
               pendingReturnAbsenceHours > 0 ? "away_for_" : "",
               pendingReturnAbsenceHours > 0 ? "" : (userIsHome ? "home" : "offline"),
               pendingReturnAbsenceHours > 0 ? "" : "");
  if (pendingReturnAbsenceHours > 0)
    safeAppend(buf, size, &used, "%lu_hours", pendingReturnAbsenceHours);

  if (pulseRecentUserContext[0])
    safeAppend(buf, size, &used, "\nRecent user messages (optional continuity context; make one brief callback only if it fits, do not answer an old request): %.260s",
               pulseRecentUserContext);

  int recentLineCount = 0;
  for (int offset = 1; offset <= (int)(sizeof(recentIdleSpeechLines) / sizeof(recentIdleSpeechLines[0])) &&
                       recentLineCount < 3; offset++) {
    int idx = (recentIdleSpeechIdx + (int)(sizeof(recentIdleSpeechLines) / sizeof(recentIdleSpeechLines[0])) - offset) %
              (int)(sizeof(recentIdleSpeechLines) / sizeof(recentIdleSpeechLines[0]));
    if (!recentIdleSpeechLines[idx][0]) continue;
    if (recentLineCount == 0)
      safeAppend(buf, size, &used, "\nDo not repeat these recent idle lines: ");
    else
      safeAppend(buf, size, &used, " | ");
    safeAppend(buf, size, &used, "%.120s", recentIdleSpeechLines[idx]);
    recentLineCount++;
  }

  const char* speechGate = idleSpeechGateReason();
  safeAppend(buf, size, &used, "\nSPEECH_ALLOWED=%s (reason=%s)",
             strcmp(speechGate, "eligible") == 0 ? "yes" : "no", speechGate);

  if (pulseMemoryCount > 0) {
    int lastIdx = pulseMemoryCount - 1;
    safeAppend(buf, size, &used, "\nLast thought: %.100s", pulseMemory[lastIdx]);
    safeAppend(buf, size, &used, "\nUse the last thought only to avoid an immediate repeat; do not invent a replacement topic.");
  }

  // Inject pulse memory — last 3 idle actions (Change #7)
  bool hasPulseMem = false;
  for (int i = 0; i < 3; i++) {
    if (strlen(recentPulseActions[i]) > 0) { hasPulseMem = true; break; }
  }
  if (hasPulseMem) {
    safeAppend(buf, size, &used, "\n Recent idle (use only as a gentle variety hint): %s%s%s%s%s%s",
      strlen(recentPulseActions[0]) > 0 ? recentPulseActions[0] : "",
      strlen(recentPulseActions[0]) > 0 && strlen(recentPulseActions[1]) > 0 ? " | " : "",
      strlen(recentPulseActions[1]) > 0 ? recentPulseActions[1] : "",
      strlen(recentPulseActions[1]) > 0 && strlen(recentPulseActions[2]) > 0 ? " | " : "",
      strlen(recentPulseActions[2]) > 0 ? recentPulseActions[2] : "");
    safeAppend(buf, size, &used, "\nQuiet is preferable to forced variety.");
  }

  LOGD("PULSE","Context: %s", buf);
}

static void rememberSelfTalk(const char* line) {
  if (!line || strlen(line) == 0) return;
  if (pulseMemoryCount < 3) {
    strncpy(pulseMemory[pulseMemoryCount], line, 159);
    pulseMemory[pulseMemoryCount][159] = '\0';
    pulseMemoryCount++;
  } else {
    memmove(pulseMemory[0], pulseMemory[1], 160);
    memmove(pulseMemory[1], pulseMemory[2], 160);
    strncpy(pulseMemory[2], line, 159);
    pulseMemory[2][159] = '\0';
  }
  strncpy(sys.innerThread, line, 159);
  sys.innerThread[159] = '\0';
}

static void executePulseAction(PulseResult& r) {
  if (!r.action[0] || strcmp(r.action, "none") == 0) {
    lastIdleAction = millis();
    lastActivity = lastIdleAction;
    LOGI("PULSE", "Exec: none (reason=%s)", r.quietReason[0] ? r.quietReason : "model_quiet");
    return;
  }

  bool spokenAction = strcmp(r.action, "aside") == 0 ||
                      strcmp(r.action, "speak") == 0 ||
                      strcmp(r.action, "murmur") == 0 ||
                      strcmp(r.action, "start_topic") == 0 ||
                      strcmp(r.action, "quote") == 0 ||
                      strcmp(r.action, "news") == 0;
  bool externalSpeechAction = strcmp(r.action, "quote") == 0 || strcmp(r.action, "news") == 0;
  if (spokenAction && !externalSpeechAction && !r.detail[0]) {
    setPulseQuiet(r, "empty_detail");
    lastIdleAction = millis();
    lastActivity = lastIdleAction;
    LOGI("PULSE", "Exec: none (reason=empty_detail)");
    return;
  }
  if (spokenAction && !idleSpeechAllowed()) {
    const char* reason = idleSpeechGateReason();
    LOGI("PULSE", "Suppressing idle speech at execution: %s", reason);
    setPulseQuiet(r, "speech_pacing");
    lastIdleAction = millis();
    lastActivity = lastIdleAction;
    return;
  }
  if (spokenAction) recordIdleSpeech(externalSpeechAction ? NULL : r.detail);

  // --- FILLER CAP: max 5 sighs/giggles per hour ---
  unsigned long now = millis();
  if (now - fillerWindowStart > 3600000UL) {
    fillerCountThisHour = 0;
    fillerWindowStart = now;
  }
  bool isFiller = (strcmp(r.action, "sigh") == 0 || strcmp(r.action, "giggle") == 0
                || strcmp(r.action, "fidget") == 0 || strcmp(r.action, "stretch") == 0);
  if (isFiller && fillerCountThisHour >= 5) {
    LOGI("PULSE","Filler cap reached (%d this hour), staying quiet", fillerCountThisHour);
    strcpy(r.action, "stare");
    r.detail[0] = '\0';
    r.emotionTag[0] = '\0';
  }
  // Only count actual fillers.
  if (strcmp(r.action, "sigh") == 0 || strcmp(r.action, "giggle") == 0
      || strcmp(r.action, "fidget") == 0 || strcmp(r.action, "stretch") == 0) {
    fillerCountThisHour++;
  }

  // --- PULSE MEMORY: track last 3 idle actions (Change #7) ---
  char pulseDesc[24];
  if (strcmp(r.action, "aside") == 0 || strcmp(r.action, "speak") == 0 ||
      strcmp(r.action, "murmur") == 0 || strcmp(r.action, "start_topic") == 0) {
    snprintf(pulseDesc, sizeof(pulseDesc), "%s \"%.12s\"", r.action, strlen(r.detail) > 0 ? r.detail : "");
  } else {
    snprintf(pulseDesc, sizeof(pulseDesc), "%s", r.action);
  }
  if (recentPulseIdx < 3) {
    strncpy(recentPulseActions[recentPulseIdx], pulseDesc, 19);
    recentPulseActions[recentPulseIdx][19] = '\0';
  } else {
    memmove(recentPulseActions[0], recentPulseActions[1], 20);
    memmove(recentPulseActions[1], recentPulseActions[2], 20);
    strncpy(recentPulseActions[2], pulseDesc, 19);
    recentPulseActions[2][19] = '\0';
  }
  recentPulseIdx++;

  lastIdleAction = millis();
  lastActivity = millis();
  LOGI("PULSE","Exec: %s detail='%s' emo=%s", r.action, r.detail, r.emotionTag);

  // --- EMOTIONAL PERSISTENCE: track mood duration (Change #4) ---
  pulsesSinceMoodShift++;

  if (strcmp(r.action, "aside") == 0 || strcmp(r.action, "speak") == 0 ||
      strcmp(r.action, "start_topic") == 0) {
    if (strlen(r.detail) > 0) {
      if (strlen(r.emotionTag) > 0) {
        Emotion e = parseEmotionTag(r.emotionTag);
        if (e != NEUTRAL) {
          currentEmotion = e; emotionSetTime = millis();
          pulsesSinceMoodShift = 0;
        }
      }
      strncpy(aiMsg, r.detail, sizeof(aiMsg) - 1);
      aiMsg[sizeof(aiMsg) - 1] = '\0';
      scrollOffset = 0;
      if (strcmp(r.action, "aside") != 0) logLocalChat("Yuki", aiMsg);
      sound_notify();
      ttsQueue(aiMsg);
      rememberSelfTalk(r.detail);
    }
  } else if (strcmp(r.action, "murmur") == 0) {
    if (strlen(r.detail) > 0) {
      strncpy(aiMsg, r.detail, sizeof(aiMsg) - 1);
      aiMsg[sizeof(aiMsg) - 1] = '\0';
      scrollOffset = 0;
      ttsQueue(aiMsg);   // companion voice: she says her own lines aloud
      rememberSelfTalk(r.detail);
    }
    if (sys.soundOn) sound_hum();
  } else if (strcmp(r.action, "emote") == 0) {
    if (strlen(r.detail) > 0) {
      Emotion e = parseEmotionTag(r.detail);
      currentEmotion = e; emotionSetTime = millis();
      pulsesSinceMoodShift = 0;
    }
  } else if (strcmp(r.action, "mood_shift") == 0) {
    if (strlen(r.detail) > 0) {
      Emotion e = parseEmotionTag(r.detail);
      currentEmotion = e; emotionSetTime = millis();
      pulsesSinceMoodShift = 0;
      updateEnergyLevel();
    }
  } else if (strcmp(r.action, "hum") == 0) {
    if (sys.soundOn) {
      // Mood-based hum variety (Change #10)
      if (currentEmotion == HAPPY || currentEmotion == LOVE) {
        sound_hum();
        delay(150); // shorter happy hum
      } else if (currentEmotion == SAD) {
        sound_hum();
        delay(500); // longer sad hum
      } else {
        sound_hum();
      }
    }
  } else if (strcmp(r.action, "sigh") == 0) {
    strncpy(aiMsg, "*sigh*", sizeof(aiMsg) - 1);
    aiMsg[sizeof(aiMsg) - 1] = '\0';
    scrollOffset = 0;
    if (sys.soundOn) sound_sleepy();
  } else if (strcmp(r.action, "giggle") == 0) {
    currentEmotion = LAUGHING; emotionSetTime = millis();
    strncpy(aiMsg, "*giggle*", sizeof(aiMsg) - 1);
    aiMsg[sizeof(aiMsg) - 1] = '\0';
    scrollOffset = 0;
    if (sys.soundOn) sound_happy();
  } else if (strcmp(r.action, "fidget") == 0) {
    currentEmotion = SHY; emotionSetTime = millis();
  } else if (strcmp(r.action, "stretch") == 0) {
    currentEmotion = SLEEPY; emotionSetTime = millis();
    strncpy(aiMsg, "*stretch*", sizeof(aiMsg) - 1);
    aiMsg[sizeof(aiMsg) - 1] = '\0';
    scrollOffset = 0;
    if (sys.soundOn) sound_wake_stretch_short();
  } else if (strcmp(r.action, "stare") == 0) {
    currentEmotion = THINKING_FACE; emotionSetTime = millis();
    silentThoughtUntil = millis() + 4000;
  } else if (strcmp(r.action, "react") == 0) {
    if (strlen(r.detail) > 0) {
      Emotion e = parseEmotionTag(r.detail);
      currentEmotion = e; emotionSetTime = millis();
    }
    if (sys.soundOn) sound_notify();
  } else if (strcmp(r.action, "sleep") == 0) {
    enterYukiSleep();
    sound_sleep_farewell();
  } else if (strcmp(r.action, "quote") == 0) {
    // Idle quote — pick a random quote and share it via AI
    char quoteBuf[256] = {0};
    loadRandomQuote(quoteBuf, sizeof(quoteBuf));
    if (strlen(quoteBuf) > 0) {
      char prompt[380];
      snprintf(prompt, sizeof(prompt),
        "Share this quote as an idle thought, like it just came to mind: \"%s\". One or two sentences max. End with [SND:t300,500].",
        quoteBuf);
      syncAI(prompt, true, FACE, true, true);
    }
  } else if (strcmp(r.action, "news") == 0) {
    // Idle news — fetch a headline and comment on it
    char headlines[320] = {0};
    if (fetchHeadlines(headlines, sizeof(headlines))) {
      char prompt[512];
      snprintf(prompt, sizeof(prompt),
        "Here are today's headlines: %s. Pick ONE that interests you and react to it casually in one sentence. "
        "Don't say 'I read the news'. Just react like you noticed something. End with [SND:t200,400].",
        headlines);
      syncAI(prompt, true, FACE, true, true);
    }
  } else if (strcmp(r.action, "micro_expression") == 0) {
    Emotion e = NEUTRAL;
    if (strlen(r.detail) > 0) {
      e = parseEmotionTag(r.detail);
    }
    if (e == NEUTRAL) e = THINKING_FACE;
    microEmotion = e;
    microExpressionUntil = millis() + 500;
    currentEmotion = e;
    emotionSetTime = millis();
    LOGD("PULSE","micro_expression -> %d for 500ms", (int)e);
  }
}
