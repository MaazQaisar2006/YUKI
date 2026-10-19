#pragma once

struct PulseResult {
  char action[16];
  char detail[160];
  char emotionTag[16];
};

extern unsigned long microExpressionUntil;
extern Emotion microEmotion;
extern Message receivedMessages[];
extern int messageIndex;

static const char* const pulseActionNames[] = {
  "speak", "murmur", "emote", "mood_shift", "hum", "sigh",
  "giggle", "fidget", "stretch", "stare", "react",
  "micro_expression", "sleep", "quote", "news", "start_topic"
};
static const int PULSE_ACTION_COUNT = 16;

// --- THOUGHT CATEGORIES for context-driven idle behavior ---
static const char* const thoughtCategories[] = {
  "quiet",        // silence or a nonverbal idle action is the default
  "memory",       // recall a fact about the user from journal
  "callback"      // revisit a recent user topic only when grounded
};
static char pulseRecentUserContext[384] = {};

// --- FILLER CAP: max sighs/giggles per hour ---
static int fillerCountThisHour = 0;
static unsigned long fillerWindowStart = 0;

// --- PULSE MEMORY: track last 3 idle actions to prevent repetition ---
static char recentPulseActions[3][20] = {"", "", ""};
static int recentPulseIdx = 0;

// --- EMOTIONAL PERSISTENCE: track mood duration ---
static int pulsesSinceMoodShift = 0;

// An idle callback can use a known fact occasionally, but the same fact/topic
// stays out of idle prompts for a while after it was selected.
static const unsigned long IDLE_FACT_COOLDOWN_MS = 45UL * 60UL * 1000UL;
static unsigned long idleFactUsedAt[8] = {};
static char idleFactCandidates[7][MEMORY_V2_FACT_LEN] = {};
static char selectedIdleFact[MEMORY_V2_FACT_LEN] = {};

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

static bool isValidPulseAction(const char* a) {
  for (int i = 0; i < PULSE_ACTION_COUNT; i++) {
    if (strcmp(a, pulseActionNames[i]) == 0) return true;
  }
  return false;
}

static char* findPulseField(char* text, const char* field) {
  const size_t fieldLen = strlen(field);
  for (char* p = text; *p; p++) {
    if (strncasecmp(p, field, fieldLen) != 0) continue;
    char after = p[fieldLen]; // safe: the complete field matched before this byte is read
    bool fieldBoundary = after == '\0' || after == '=' || after == ':' || after == ' ' || after == '\t';
    if (fieldBoundary &&
        (p == text || p[-1] == '\n' || p[-1] == '\r' || p[-1] == ' ' || p[-1] == '*')) return p;
  }
  return NULL;
}

static char* skipPulseSeparators(char* p) {
  while (*p == ' ' || *p == '\t' || *p == ':' || *p == '=' || *p == '|' ||
         *p == '-' || *p == '*' || *p == '"' || *p == '\'') p++;
  while (strlen(p) >= 3 && (uint8_t)p[0] == 0xE2 && (uint8_t)p[1] == 0x80 &&
         ((uint8_t)p[2] == 0x94 || (uint8_t)p[2] == 0x93 ||
          (uint8_t)p[2] == 0x9C || (uint8_t)p[2] == 0x9D)) p += 3;
  return p;
}

static void trimPulseText(char* text) {
  if (!text) return;
  char* start = text;
  size_t length = strlen(start);
  while (length && (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n' ||
                    *start == '"' || *start == '\'' || *start == '`' || *start == '*')) {
    start++;
    length--;
  }
  while (length >= 3 && (uint8_t)start[0] == 0xE2 && (uint8_t)start[1] == 0x80 &&
         ((uint8_t)start[2] == 0x9C || (uint8_t)start[2] == 0x9D ||
          (uint8_t)start[2] == 0x94 || (uint8_t)start[2] == 0x93)) {
    start += 3;
    length -= 3;
  }
  if (start != text) memmove(text, start, strlen(start) + 1);
  length = strlen(text);
  while (length && (text[length - 1] == ' ' || text[length - 1] == '\t' ||
                    text[length - 1] == '\r' || text[length - 1] == '\n' ||
                    text[length - 1] == '"' || text[length - 1] == '\'' ||
                    text[length - 1] == '`' || text[length - 1] == '*' ||
                    text[length - 1] == '|')) text[--length] = '\0';
  if (length >= 3 && (uint8_t)text[length - 3] == 0xE2 &&
      (uint8_t)text[length - 2] == 0x80 &&
      ((uint8_t)text[length - 1] == 0x9C || (uint8_t)text[length - 1] == 0x9D ||
       (uint8_t)text[length - 1] == 0x94 || (uint8_t)text[length - 1] == 0x93)) {
    text[length - 3] = '\0';
  }
}

static void copyPulseFieldText(char* target, size_t targetSize, char* start) {
  if (!target || targetSize == 0) return;
  target[0] = '\0';
  if (!start) return;
  start = skipPulseSeparators(start);
  char* end = start + strcspn(start, "\r\n");
  char* emotion = findPulseField(start, "EMOTION");
  if (emotion && emotion < end) end = emotion;
  while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '"' ||
                         end[-1] == '\'' || end[-1] == '*' || end[-1] == '|')) end--;
  size_t len = (size_t)(end - start);
  if (len >= targetSize) len = targetSize - 1;
  memcpy(target, start, len);
  target[len] = '\0';
  trimPulseText(target);

  // Some providers echo the requested word-count hint as if it were dialogue.
  char* p = target;
  if (isdigit((unsigned char)*p)) {
    while (isdigit((unsigned char)*p)) p++;
    if (*p == '-') p++;
    else if ((uint8_t)p[0] == 0xE2 && (uint8_t)p[1] == 0x80 &&
             ((uint8_t)p[2] == 0x93 || (uint8_t)p[2] == 0x94)) p += 3;
    else p = target;
    if (p != target && isdigit((unsigned char)*p)) {
      while (isdigit((unsigned char)*p)) p++;
      while (*p == ' ' || *p == '\t') p++;
      if (strncasecmp(p, "words", 5) == 0) {
        p += 5;
        if (*p == ':' || *p == '=') p++;
        p = skipPulseSeparators(p);
        memmove(target, p, strlen(p) + 1);
        trimPulseText(target);
      }
    }
  }
}

static bool pulseIsQuestion(const char* text) {
  if (!text) return false;
  if (strchr(text, '?')) return true;
  while (*text == ' ' || *text == '\t' || *text == '"' || *text == '\'' ||
         *text == '`' || *text == '(') text++;
  static const char* const questionStarts[] = {
    "what", "which", "who", "where", "when", "why", "how",
    "do", "does", "did", "can", "could", "would", "will",
    "is", "are", "was", "were", "have", "has", "should",
    "wonder if", "wonder what", "how about", "i wonder if", "i wonder what"
  };
  for (size_t i = 0; i < sizeof(questionStarts) / sizeof(questionStarts[0]); i++) {
    size_t length = strlen(questionStarts[i]);
    if (strncasecmp(text, questionStarts[i], length) == 0 &&
        (text[length] == '\0' || !isalnum((unsigned char)text[length])))
      return true;
  }
  return false;
}

static bool pulseIsStopWord(const char* word, size_t length) {
  static const char* const stopWords[] = {
    "about", "after", "again", "always", "because", "being", "could",
    "doing", "feel", "feels", "from", "have", "just", "like", "look",
    "maybe", "more", "much", "nice", "only", "quiet", "really", "right",
    "same", "something", "that", "their", "them", "there", "these",
    "thing", "think", "this", "those", "time", "today", "very", "what",
    "when", "where", "which", "while", "whose", "with", "would", "your"
  };
  for (size_t i = 0; i < sizeof(stopWords) / sizeof(stopWords[0]); i++) {
    if (strlen(stopWords[i]) == length && strncasecmp(word, stopWords[i], length) == 0)
      return true;
  }
  return false;
}

static bool pulseHasGrounding(const char* detail) {
  if (!detail || !detail[0]) return false;
  const char* evidence[2] = { pulseRecentUserContext, selectedIdleFact };
  for (const char* p = detail; *p;) {
    while (*p && !isalnum((unsigned char)*p)) p++;
    const char* word = p;
    while (*p && isalnum((unsigned char)*p)) p++;
    size_t length = (size_t)(p - word);
    if (length < 4 || pulseIsStopWord(word, length)) continue;
    for (size_t e = 0; e < sizeof(evidence) / sizeof(evidence[0]); e++) {
      if (!evidence[e][0]) continue;
      const char* candidate = evidence[e];
      while (*candidate) {
        while (*candidate && !isalnum((unsigned char)*candidate)) candidate++;
        const char* candidateWord = candidate;
        while (*candidate && isalnum((unsigned char)*candidate)) candidate++;
        size_t candidateLength = (size_t)(candidate - candidateWord);
        if (candidateLength == length &&
            strncasecmp(word, candidateWord, length) == 0)
          return true;
      }
    }
  }
  return false;
}

static void quietUngroundedPulse(PulseResult& result) {
  bool spokenAction = strcmp(result.action, "speak") == 0 ||
                      strcmp(result.action, "murmur") == 0 ||
                      strcmp(result.action, "start_topic") == 0;
  if ((!spokenAction && (strcmp(result.action, "quote") != 0 && strcmp(result.action, "news") != 0)) ||
      (spokenAction && !pulseIsQuestion(result.detail) &&
       pulseHasGrounding(result.detail)))
    return;
  LOGI("PULSE", "Suppressing idle speech without a grounded, non-question thought");
  strcpy(result.action, "stare");
  result.detail[0] = '\0';
  result.emotionTag[0] = '\0';
}

// Providers occasionally ignore the requested ACTION/DETAIL/EMOTION format.
// Recover common plain-text variants rather than converting every answer into
// an empty "murmur".
static bool parsePulseResponse(const char* source, PulseResult& result) {
  memset(&result, 0, sizeof(result));
  if (!source || !source[0]) return false;
  static char text[256];
  strncpy(text, source, sizeof(text) - 1);
  text[sizeof(text) - 1] = '\0';

  char* protocolToken = strstr(text, "<|");
  if (protocolToken) {
    static const char messageMarker[] = "<|message|>";
    char* messageStart = strstr(protocolToken, messageMarker);
    if (!messageStart) {
      LOGW("PULSE", "Discarding idle response with incomplete protocol markup");
      return false;
    }
    messageStart += sizeof(messageMarker) - 1;
    if (strstr(messageStart, "<|")) {
      LOGW("PULSE", "Discarding idle response with protocol markup in message");
      return false;
    }
    memmove(text, messageStart, strlen(messageStart) + 1);
  }

  char* jsonStart = text;
  while (*jsonStart == ' ' || *jsonStart == '\t' || *jsonStart == '\r' || *jsonStart == '\n')
    jsonStart++;
  if (*jsonStart == '{') {
    StaticJsonDocument<384> json;
    DeserializationError error = deserializeJson(json, jsonStart);
    if (error) {
      LOGW("PULSE", "Idle action JSON parse failed: %s", error.c_str());
      return false;
    }

    const char* action = json["action"] | "";
    size_t actionLength = strlen(action);
    if (actionLength == 0 || actionLength >= sizeof(result.action)) return false;
    for (size_t i = 0; i < actionLength; i++)
      result.action[i] = (char)tolower((unsigned char)action[i]);
    result.action[actionLength] = '\0';
    if (!isValidPulseAction(result.action)) return false;

    const char* detail = json["detail"] | "";
    strncpy(result.detail, detail, sizeof(result.detail) - 1);
    result.detail[sizeof(result.detail) - 1] = '\0';
    const char* emotion = json["emotion"] | "";
    strncpy(result.emotionTag, emotion, sizeof(result.emotionTag) - 1);
    result.emotionTag[sizeof(result.emotionTag) - 1] = '\0';
    return true;
  }

  char* first = text;
  while (*first == ' ' || *first == '\t' || *first == '\r' || *first == '\n' ||
         *first == '`' || *first == '*' || *first == '"' || *first == '\'') first++;
  if (first != text) memmove(text, first, strlen(first) + 1);
  size_t textLen = strlen(text);
  while (textLen && (text[textLen - 1] == ' ' || text[textLen - 1] == '\t' ||
                     text[textLen - 1] == '\r' || text[textLen - 1] == '\n' ||
                     text[textLen - 1] == '`' || text[textLen - 1] == '*' ||
                     text[textLen - 1] == '"' || text[textLen - 1] == '\''))
    text[--textLen] = '\0';
  if (!textLen) return false;

  char* actionField = findPulseField(text, "ACTION");
  char actionToken[16] = {};
  if (actionField) {
    char* p = skipPulseSeparators(actionField + 6);
    size_t n = 0;
    while ((isalnum((unsigned char)p[n]) || p[n] == '_') && n < sizeof(actionToken) - 1) {
      actionToken[n] = (char)tolower((unsigned char)p[n]);
      n++;
    }
    actionToken[n] = '\0';
  }

  char* prefix = text;
  if (!actionToken[0]) {
    prefix = skipPulseSeparators(prefix);
    for (int i = 0; i < PULSE_ACTION_COUNT; i++) {
      size_t n = strlen(pulseActionNames[i]);
      if (strncasecmp(prefix, pulseActionNames[i], n) == 0) {
        char after = prefix[n]; // safe after a complete token match
        if (after != '\0' && after != ' ' && after != '\t' && after != ':' &&
            after != '=' && after != '|' && after != '*' && after != '"' && after != '\'' &&
            !((uint8_t)prefix[n] == 0xE2 && (uint8_t)prefix[n + 1] == 0x80 &&
              ((uint8_t)prefix[n + 2] == 0x94 || (uint8_t)prefix[n + 2] == 0x93))) continue;
        strncpy(actionToken, pulseActionNames[i], sizeof(actionToken) - 1);
        break;
      }
    }
  }

  char* detailField = findPulseField(text, "DETAIL");
  if (detailField) {
    copyPulseFieldText(result.detail, sizeof(result.detail), detailField + 6);
  } else if (actionToken[0]) {
    size_t actionLen = strlen(actionToken);
    char* detailStart = text;
    if (actionField) detailStart = actionField + 6;
    else {
      while (*detailStart && strncasecmp(detailStart, actionToken, actionLen) != 0) detailStart++;
      if (*detailStart) detailStart += actionLen;
    }
    copyPulseFieldText(result.detail, sizeof(result.detail), detailStart);
  } else if (!actionField && !detailField) {
    // Treat an unlabelled natural-language response as a spoken idle thought.
    strncpy(actionToken, "speak", sizeof(actionToken) - 1);
    strncpy(result.detail, text, sizeof(result.detail) - 1);
    result.detail[sizeof(result.detail) - 1] = '\0';
    trimPulseText(result.detail);
  }

  if (!actionToken[0] && detailField) {
    strncpy(actionToken, "speak", sizeof(actionToken) - 1);
  } else if (!actionToken[0] && actionField) {
    return false;
  }

  char* emotionField = findPulseField(text, "EMOTION");
  if (emotionField) {
    char* p = skipPulseSeparators(emotionField + 7);
    size_t n = 0;
    while ((isalnum((unsigned char)p[n]) || p[n] == '_') && n < sizeof(result.emotionTag) - 1) n++;
    memcpy(result.emotionTag, p, n);
    result.emotionTag[n] = '\0';
  }

  if (!actionToken[0] || !isValidPulseAction(actionToken)) return false;
  strncpy(result.action, actionToken, sizeof(result.action) - 1);
  result.action[sizeof(result.action) - 1] = '\0';
  return true;
}

static PulseResult callPulseAI(const char* context) {
  PulseResult r;
  strcpy(r.action, "murmur"); r.detail[0] = '\0'; r.emotionTag[0] = '\0';

  static uint32_t rateLimitRetryAt = 0;
  if (rateLimitRetryAt && (int32_t)(millis() - rateLimitRetryAt) < 0) {
    strcpy(r.action, "stretch");
    LOGD("PULSE", "Skipping model call during rate-limit cooldown");
    return r;
  }

  if (WiFi.status() != WL_CONNECTED) return r;
  // Keep the idle model call away from the device's lowest-heap condition.
  // The local idle scheduler can still choose a non-network action.
  if (ESP.getFreeHeap() < 65000 || !canAllocJson(8192)) {
    LOGW("PULSE", "Skipping idle model call - low heap (%u)", ESP.getFreeHeap());
    strcpy(r.action, "stretch");
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

  // System prompt ~2500B + context ~800B + JSON overhead — needs large arena
  DynamicJsonDocument doc(5120);
  JsonArray messages = doc.createNestedArray("messages");

  JsonObject sysMsg = messages.createNestedObject();
  sysMsg["role"] = "system";
  static char pulseSystemPrompt[2600];
  snprintf(pulseSystemPrompt, sizeof(pulseSystemPrompt),
    "You are %s, an AI companion character. %s\n"
    "Given your state and the THOUGHT_CATEGORY, pick ONE action.\n\n"
    "Quiet is the default: choose stare unless there is a specific, genuinely worthwhile thought grounded in the supplied recent user messages or optional personal callback. "
    "Do not speak merely to fill silence. Never ask a question, start a topic, or make a generic observation during idle time.\n\n"
    "Keep idle thoughts in the language of the recent conversation; use English if uncertain, and do not switch languages unprompted.\n\n"
    "Available actions:\n"
    "speak — say one brief, natural statement grounded in the supplied user messages or personal callback\n"
    "murmur — a brief reaction only when grounded in supplied context\n"
    "hum — just a sound, no words (DETAIL empty)\n"
    "sigh — a tired/relieved sigh, no words (DETAIL empty)\n"
    "giggle — a soft laugh, no words (DETAIL empty)\n"
    "fidget — restless movement, no words (DETAIL empty)\n"
    "stretch — stretching, no words (DETAIL empty)\n"
    "stare — staring into space, thinking (DETAIL empty)\n"
    "emote — shift expression. DETAIL = one emotion word (HAPPY/SAD/LOVE/SHY/WINK/SURPRISED/CONFUSED/THINKING_FACE/CALM/EXCITED)\n"
    "mood_shift — deeper mood change. DETAIL = one emotion word\n"
    "micro_expression — 500ms flash of emotion. DETAIL = one emotion word\n"
    "\n"
    "EMOTION RULES:\n"
    "- Let an emotion persist when it still fits; don't change mood merely to create variety.\n"
    "- Shift gradually and only when the moment gives you a reason.\n\n"
    "CREATIVITY:\n"
    "- Follow THOUGHT_CATEGORY as a creative nudge.\n"
    "- Never invent or assume sights, weather around the user, activities, feelings, or shared experiences. Device weather and phone presence are not evidence about the user's surroundings.\n"
    "- If no supplied user message or personal callback gives you a concrete, relevant detail to refer to, choose ACTION=stare.\n"
    "- Do not ask the user anything while they are idle; avoid rhetorical questions too.\n"
    "- Ground every callback in the supplied Recent lines or Optional personal callback. Never invent a recipe, event, promise, plan, or past conversation.\n"
    "- Do not assume the user is trying, doing, feeling, seeing, or physically experiencing anything not stated. Avoid body/sensory prompts.\n"
    "- When the user is unsure, be gentle; don't give unsolicited instructions or advice.\n"
    "- Avoid repeating the same thought, but don't invent a new topic just to be different.\n"
    "- Don't revisit a user's personal fact unless it is useful or genuinely relevant.\n\n"
    "Reply in EXACT format:\nACTION=<action>\nDETAIL=<detail>\nEMOTION=<emotion>",
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
  doc["max_tokens"] = 256;
  doc["temperature"] = 0.9;
  doc["reasoning_effort"] = "low";
  if (doc.overflowed()) {
    LOGW("PULSE", "Skipping idle call - request JSON overflow");
    http.end(); client.stop();
    strcpy(r.action, "stretch");
    return r;
  }

  String body;
  size_t expectedLength = measureJson(doc);
  if (expectedLength == 0 || !body.reserve(expectedLength) || serializeJson(doc, body) != expectedLength) {
    LOGW("PULSE", "Skipping idle call - request serialization failed");
    http.end(); client.stop();
    strcpy(r.action, "stretch");
    return r;
  }
  for (unsigned int i = 0; i < body.length(); i++) {
    unsigned char c = body[i];
    if (c < 0x20 && c != '\n' && c != '\r' && c != '\t') body[i] = ' ';
  }

  int httpCode = http.POST(body);
  LOGI("PULSE","HTTP code=%d heap=%u", httpCode, ESP.getFreeHeap());
  if (httpCode != 200) {
    if (httpCode > 0) {
      char errorBody[192] = {};
      Stream& errorStream = http.getStream();
      size_t errorLen = errorStream.readBytes((uint8_t*)errorBody, sizeof(errorBody) - 1);
      errorBody[errorLen] = '\0';
      LOGW("PULSE", "Provider HTTP %d body: %.180s", httpCode,
           errorLen ? errorBody : "(empty response body)");
    }
    if (httpCode == 429) {
      rateLimitRetryAt = millis() + 120000UL;
      LOGW("PULSE", "Provider rate limit; pausing idle model calls for 120 seconds");
    } else {
      LOGW("PULSE", "Provider request failed; skipping this idle action");
    }
    http.end();
    client.stop();
    strcpy(r.action, "stretch");
    return r;
  }
  if (httpCode > 0) {
    StaticJsonDocument<256> filter;
    filter["choices"][0]["message"]["content"] = true;
    StaticJsonDocument<512> resp;
    Stream& stream = http.getStream();
    DeserializationError err = deserializeJson(resp, stream, DeserializationOption::Filter(filter));
    if (!err) {
      const char* text = resp["choices"][0]["message"]["content"];
      if (text && strlen(text) > 0) {
        LOGI("PULSE","AI raw: %s", text);
        if (!parsePulseResponse(text, r)) {
          LOGW("PULSE", "Could not parse idle response; choosing a quiet safe action");
          strcpy(r.action, "stare");
          r.detail[0] = '\0';
        } else {
          quietUngroundedPulse(r);
        }
        LOGI("PULSE","Parsed: action=%s detail='%s' emotion=%s", r.action, r.detail, r.emotionTag);
      } else {
        LOGW("PULSE","AI returned empty/null content");
      }
    } else {
      LOGW("PULSE","JSON parse error: %s", err.c_str());
    }
  }
  http.end(); client.stop();
  return r;
}

static void buildPulseContext(char* buf, size_t size) {
  if (!buf || size == 0) return;
  selectedIdleFact[0] = '\0';
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

  // Most idle turns are explicitly framed as quiet. Memory/callback only guide
  // the model when there is actual evidence it can safely refer to.
  int nextCat = 0;
  if (pulseRecentUserContext[0] && random(4) == 0) {
    nextCat = 2;
  } else if (random(3) == 0) {
    nextCat = 1;
  }

  // Only offer one memory when the memory category was selected. A topic
  // cooldown and random choice keep one favorite from dominating idle talk.
  if (nextCat == 1) {
    int eligible[7];
    int eligibleCount = 0;
    unsigned long now = millis();
    for (int category = 1; category <= 7; category++) {
      if (idleFactUsedAt[category] != 0 && now - idleFactUsedAt[category] < IDLE_FACT_COOLDOWN_MS) continue;
      idleFactCandidates[category - 1][0] = '\0';
      memoryV2BuildCategoryContext(category, idleFactCandidates[category - 1],
                                   sizeof(idleFactCandidates[category - 1]));
      if (idleFactCandidates[category - 1][0]) {
        eligible[eligibleCount++] = category;
      }
    }
    if (eligibleCount > 0) {
      int category = eligible[random(0, eligibleCount)];
      strncpy(selectedIdleFact, idleFactCandidates[category - 1], sizeof(selectedIdleFact) - 1);
      selectedIdleFact[sizeof(selectedIdleFact) - 1] = '\0';
      idleFactUsedAt[category] = now;
    } else {
      nextCat = 0;
    }
  }
  LOGI("PULSE", "Idle thought category=%s personal_fact=%s",
       thoughtCategories[nextCat], selectedIdleFact[0] ? "offered" : "none");

  int used = snprintf(buf, size,
    "Energy: %s (%.2f). Mood: %s. Quiet for %lu minutes. "
    "Mode: FACE=%s. "
    "THOUGHT_CATEGORY=%s. LAST_ACTION=%s",
    energyDesc, yukiEnergy, emoStr, minsSinceInteraction,
    (currentMode == FACE) ? "yes" : "no",
    thoughtCategories[nextCat],
    strlen(recentPulseActions[0]) > 0 ? recentPulseActions[0] : "none");

  if (selectedIdleFact[0]) {
    safeAppend(buf, size, &used,
      "\nOptional personal callback (use only if it genuinely fits; otherwise ignore it and choose another thought): %.150s",
      selectedIdleFact);
  }

  if (pulseRecentUserContext[0])
    safeAppend(buf, size, &used, "\nRecent user messages: %.300s", pulseRecentUserContext);

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
  if (strcmp(r.action, "speak") == 0 || strcmp(r.action, "murmur") == 0 || strcmp(r.action, "start_topic") == 0) {
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

  if (strcmp(r.action, "speak") == 0 || strcmp(r.action, "start_topic") == 0) {
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
      logLocalChat("Yuki", aiMsg);
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
