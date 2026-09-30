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
  "observation",  // notice something in the environment or mood
  "memory",       // recall a fact about the user from journal
  "question",     // ask the user something
  "musing",       // philosophical/random thought
  "weather",      // comment on the weather
  "self",         // self-reflection about feelings or state
  "callback",     // follow up on something from recent conversation
  "greeting"      // morning/night greetings based on time
};
static const int THOUGHT_CATEGORY_COUNT = 8;
static int lastThoughtCategory = -1;

// --- FILLER CAP: max sighs/giggles per hour ---
static int fillerCountThisHour = 0;
static unsigned long fillerWindowStart = 0;

// --- PULSE MEMORY: track last 3 idle actions to prevent repetition ---
static char recentPulseActions[3][20] = {"", "", ""};
static int recentPulseIdx = 0;

// --- TOPIC FOLLOW-UP: track unanswered questions ---
static char lastPulseQuestion[160] = "";
static unsigned long lastPulseQuestionTime = 0;

// --- EMOTIONAL PERSISTENCE: track mood duration ---
static int pulsesSinceMoodShift = 0;

// --- TOPIC ROTATION: inject variety every few pulses ---
static int topicRotationCounter = 0;
static const char* const topicSeeds[] = {
  "Think about something different: music, a sound, a texture, or a tiny mystery.",
  "Notice something physical: the weight of your body, the air on your skin, the light in the room.",
  "Think about a small comfort: a soft blanket, a favorite tune, or a quiet moment.",
  "Consider something abstract: time, silence, or what makes a place feel like home.",
  "Wonder about something the user actually mentioned recently, if it feels natural.",
  "Notice the contrast: what's different from yesterday? What stayed the same?",
  "Think about something you learned recently or something that surprised you.",
  "Consider the future: what might happen tomorrow? What are you curious about?"
};
static const int TOPIC_SEED_COUNT = 8;

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
  while ((uint8_t)p[0] == 0xE2 && (uint8_t)p[1] == 0x80 &&
         ((uint8_t)p[2] == 0x94 || (uint8_t)p[2] == 0x93)) p += 3; // UTF-8 em/en dash
  return p;
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
}

// Providers occasionally ignore the requested ACTION/DETAIL/EMOTION format.
// Recover common plain-text variants rather than converting every answer into
// an empty "murmur".
static bool parsePulseResponse(const char* source, PulseResult& result) {
  if (!source || !source[0]) return false;
  static char text[256];
  strncpy(text, source, sizeof(text) - 1);
  text[sizeof(text) - 1] = '\0';
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
            after != '=' && after != '|' && after != '*' && after != '"' && after != '\'') continue;
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
  } else {
    // Treat an unlabelled natural-language response as a spoken idle thought.
    strncpy(actionToken, "speak", sizeof(actionToken) - 1);
    strncpy(result.detail, text, sizeof(result.detail) - 1);
    result.detail[sizeof(result.detail) - 1] = '\0';
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
  static char pulseSystemPrompt[1800];
  snprintf(pulseSystemPrompt, sizeof(pulseSystemPrompt),
    "You are %s, an AI companion character. %s\n"
    "Given your state and the THOUGHT_CATEGORY, pick ONE action.\n\n"
    "VARIETY RULE (MOST IMPORTANT): You MUST pick a DIFFERENT action type than your last 2 actions. "
    "The context shows 'Recent idle' — never repeat the same action. Cycle through: speak, murmur, hum, sigh, giggle, fidget, stretch, stare, emote, mood_shift, start_topic, micro_expression. "
    "Do NOT always pick speak. You have a body — use it.\n\n"
    "Available actions:\n"
    "speak — say a thought aloud (4-15 words, natural)\n"
    "murmur — a quiet 1-3 word phrase like 'mm-hmm' or 'nice...'\n"
    "hum — just a sound, no words (DETAIL empty)\n"
    "sigh — a tired/relieved sigh, no words (DETAIL empty)\n"
    "giggle — a soft laugh, no words (DETAIL empty)\n"
    "fidget — restless movement, no words (DETAIL empty)\n"
    "stretch — stretching, no words (DETAIL empty)\n"
    "stare — staring into space, thinking (DETAIL empty)\n"
    "emote — shift expression. DETAIL = one emotion word (HAPPY/SAD/LOVE/SHY/WINK/SURPRISED/CONFUSED/THINKING_FACE/CALM/EXCITED)\n"
    "mood_shift — deeper mood change. DETAIL = one emotion word\n"
    "micro_expression — 500ms flash of emotion. DETAIL = one emotion word\n"
    "start_topic — initiate conversation. DETAIL = a natural opener (4-12 words)\n\n"
    "EMOTION RULES:\n"
    "- Do NOT stay NEUTRAL for more than 2 consecutive pulses. Pick a subtle emotion that fits.\n"
    "- Shift gradually: NEUTRAL→CALM→THINKING_CONTENT or NEUTRAL→HAPPY→LOVE.\n"
    "- Never jump randomly between unrelated emotions.\n\n"
    "CREATIVITY:\n"
    "- Follow THOUGHT_CATEGORY as a creative nudge.\n"
    "- Be specific when the moment gives you something real to say. A personal fact is optional; never force one into an idle thought.\n"
    "- Don't repeat the same thought. Each pulse should be different.\n"
    "- Do NOT dwell on the same emotional theme for more than 2 pulses. If you just thought about something sad or heavy, shift to something lighter or neutral. VARY your emotional tone across pulses.\n\n"
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
  int hr = timeClient.getHours();
  const char* tod = "night";
  if (hr >= 5 && hr < 12) tod = "morning";
  else if (hr >= 12 && hr < 17) tod = "afternoon";
  else if (hr >= 17 && hr < 21) tod = "evening";

  const char* todHint = "late at night — time to wind down";
  if (hr >= 5 && hr < 9) todHint = "early morning — perfect for waking up and gentle greetings";
  else if (hr >= 9 && hr < 12) todHint = "late morning — energy is good, natural time to check in";
  else if (hr >= 12 && hr < 14) todHint = "midday — bright and active";
  else if (hr >= 14 && hr < 17) todHint = "afternoon — calm and present";
  else if (hr >= 17 && hr < 19) todHint = "early evening — warm and reflective";
  else if (hr >= 19 && hr < 22) todHint = "evening — cozy and relaxing";
  else if (hr >= 22) todHint = "late night — getting sleepy, thoughts are quiet";

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
  const char* interactionDesc = "just talked with them";
  if (minsSinceInteraction > 2) interactionDesc = "been quiet for a while";
  if (minsSinceInteraction > 10) interactionDesc = "been away/quiet for ages";

  const char* presence = userIsHome ? "home" : "away";
  const char* weather = (weatherCode != -1) ? yukiWeatherDesc : "unknown";
  int temp = (int)currentTemp;

  const char* energyDesc = "medium";
  if (yukiEnergy > 0.75f) energyDesc = "high";
  else if (yukiEnergy < 0.35f) energyDesc = "low";

  // --- SMART CATEGORY SELECTION (Change #1) ---
  int nextCat = -1;
  if (minsSinceInteraction > 10 && lastThoughtCategory != 6 && random(4) == 0) {
    nextCat = 6; // callback — follow up on silence
  } else if (hr >= 5 && hr < 9 && !saidGoodMorning) {
    nextCat = 7; // greeting
  } else if (hr >= 22 && lastThoughtCategory != 5 && random(3) == 0) {
    nextCat = 5; // self — winding down
  } else if (weatherCode >= 500 && weatherCode < 600 && random(3) == 0) {
    nextCat = 4; // weather — rainy
  } else if (weatherCode == 800 && random(4) == 0) {
    nextCat = 4; // weather — sunny
  } else {
    // Weighted random from remaining categories
    int weights[] = {30, 8, 25, 18, 5, 10, 5, 0}; // obs, mem, q, mus, wea, self, cb, greet
    int total = 0;
    for (int i = 0; i < THOUGHT_CATEGORY_COUNT; i++) total += weights[i];
    int pick = random(0, total);
    int acc = 0;
    for (int i = 0; i < THOUGHT_CATEGORY_COUNT; i++) {
      acc += weights[i];
      if (pick < acc) { nextCat = i; break; }
    }
  }
  if (nextCat < 0 || nextCat >= THOUGHT_CATEGORY_COUNT) nextCat = 0;

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
      nextCat = 0; // No fresh fact: make an ordinary observation instead.
    }
  }
  lastThoughtCategory = nextCat;
  LOGI("PULSE", "Idle thought category=%s personal_fact=%s",
       thoughtCategories[nextCat], selectedIdleFact[0] ? "offered" : "none");

  int used = snprintf(buf, size,
    "Time: %d:00 %s (%s). Energy: %s (%.2f). Mood: %s (%d pulses). Affinity: %d. Level: %d. "
    "User: %s (%lu min quiet). Feel: %s. Weather: %s, %dC. "
    "Greetings: morning_said=%s night_said=%s. "
    "Battery: %.1fV. Mode: FACE=%s. "
    "THOUGHT_CATEGORY=%s. LAST_ACTION=%s",
    hr, tod, todHint, energyDesc, yukiEnergy, emoStr, pulsesSinceMoodShift, sys.affinity, sys.level,
    presence, minsSinceInteraction, interactionDesc, weather, temp,
    saidGoodMorning ? "yes" : "no", saidGoodNight ? "yes" : "no",
    vcc,
    (currentMode == FACE) ? "yes" : "no",
    thoughtCategories[nextCat],
    strlen(recentPulseActions[0]) > 0 ? recentPulseActions[0] : "none");

  if (selectedIdleFact[0]) {
    safeAppend(buf, size, &used,
      "\nOptional personal callback (use only if it genuinely fits; otherwise ignore it and choose another thought): %.150s",
      selectedIdleFact);
  }

  // --- TOPIC FOLLOW-UP: track questions (Change #8) ---
  // Clear follow-up if user recently interacted (they probably answered)
  if (minsSinceInteraction < 2) {
    lastPulseQuestionTime = 0;
  } else if (lastPulseQuestionTime > 0 && millis() - lastPulseQuestionTime > 300000) {
    safeAppend(buf, size, &used, "\nFOLLOW-UP: You asked \"%s\" a while ago. They haven't answered. Gently remind them or move on.", lastPulseQuestion);
    lastPulseQuestionTime = 0; // one-shot
  }

  // Inject last 2 conversation lines from message history
  int found = 0;
  for (int i = MAX_MESSAGES - 1; i >= 0 && found < 2; i--) {
    int idx = (messageIndex - 1 - i + MAX_MESSAGES) % MAX_MESSAGES;
    const char* entry = receivedMessages[idx].content;
    if (strlen(entry) == 0) continue;
    if (strncmp(entry, "User: ", 6) == 0 || strncmp(entry, "Yuki: ", 6) == 0) {
      safeAppend(buf, size, &used, "\nRecent: %.120s", entry);
      found++;
    }
  }

  if (pulseMemoryCount > 0) {
    // Only show the most recent inner line to avoid topic fixation
    int lastIdx = pulseMemoryCount - 1;
    safeAppend(buf, size, &used, "\nLast thought: %.100s", pulseMemory[lastIdx]);
    safeAppend(buf, size, &used, "\n IMPORTANT: Pick a COMPLETELY DIFFERENT topic. Don't repeat the same theme.");
  }

  // Inject topic seed every 3 pulses for variety
  topicRotationCounter++;
  if (topicRotationCounter % 3 == 0) {
    safeAppend(buf, size, &used, "\nTOPIC SEED: %s", topicSeeds[random(0, TOPIC_SEED_COUNT)]);
  }

  // Inject pulse memory — last 3 idle actions (Change #7)
  bool hasPulseMem = false;
  for (int i = 0; i < 3; i++) {
    if (strlen(recentPulseActions[i]) > 0) { hasPulseMem = true; break; }
  }
  if (hasPulseMem) {
    safeAppend(buf, size, &used, "\n Recent idle (DO NOT REPEAT these actions): %s%s%s%s%s%s",
      strlen(recentPulseActions[0]) > 0 ? recentPulseActions[0] : "",
      strlen(recentPulseActions[0]) > 0 && strlen(recentPulseActions[1]) > 0 ? " | " : "",
      strlen(recentPulseActions[1]) > 0 ? recentPulseActions[1] : "",
      strlen(recentPulseActions[1]) > 0 && strlen(recentPulseActions[2]) > 0 ? " | " : "",
      strlen(recentPulseActions[2]) > 0 ? recentPulseActions[2] : "");
    safeAppend(buf, size, &used, "\n Pick a DIFFERENT action type. VARY your thoughts.");
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
    LOGI("PULSE","Filler cap reached (%d this hour), forcing speak", fillerCountThisHour);
    strcpy(r.action, "speak");
    const char* fallbacks[] = {
      "Hmm, nothing comes to mind right now.",
      "It's quiet... I like that though.",
      "Just thinking about things.",
      "Wonder what they're up to.",
      "Kind of nice, just existing.",
      "The room feels calm right now.",
      "I like these quiet moments.",
      "Sometimes silence says enough.",
      "Just breathing, existing.",
      "This is peaceful.",
      "Nothing special happening, and that's fine.",
      "I wonder what Maaz is thinking about."
    };
    strncpy(r.detail, fallbacks[random(0, 12)], sizeof(r.detail) - 1);
    r.detail[sizeof(r.detail) - 1] = '\0';
    r.emotionTag[0] = '\0';
  }
  // Only count actual fillers (not forced speak from cap)
  if (strcmp(r.action, "sigh") == 0 || strcmp(r.action, "giggle") == 0
      || strcmp(r.action, "fidget") == 0 || strcmp(r.action, "stretch") == 0) {
    fillerCountThisHour++;
  }

  // Rotate away from recent actions to keep variety natural
  static char recentActions[3][16] = {};
  static int recentIdx = 0;
  bool isRepeat = false;
  for (int i = 0; i < 3; i++) {
    if (strlen(recentActions[i]) > 0 && strcmp(r.action, recentActions[i]) == 0) {
      isRepeat = true; break;
    }
  }
  if (isRepeat) {
    char original[24];
    strncpy(original, r.action, sizeof(original) - 1);
    original[sizeof(original) - 1] = '\0';
    for (int i = 0; i < PULSE_ACTION_COUNT; i++) {
      if (strcmp(r.action, pulseActionNames[i]) == 0) {
        int next = -1;
        // If the line was a spoken line, prefer rotating into another text-carrying action so it isn't lost
        bool textDetail = (strcmp(r.action, "speak") == 0 || strcmp(r.action, "murmur") == 0) && strlen(r.detail) > 0;
        if (textDetail) {
          if (strcmp("murmur", recentActions[0]) != 0 && strcmp("murmur", recentActions[1]) != 0 && strcmp("murmur", recentActions[2]) != 0) next = 1;
          else if (strcmp("speak", recentActions[0]) != 0 && strcmp("speak", recentActions[1]) != 0 && strcmp("speak", recentActions[2]) != 0) next = 0;
        }
        if (next == -1) next = (i + 1) % PULSE_ACTION_COUNT;
        // Skip past other recent actions
        for (int attempt = 0; attempt < PULSE_ACTION_COUNT; attempt++) {
          bool skip = false;
          for (int j = 0; j < 3; j++) {
            if (strcmp(pulseActionNames[next], recentActions[j]) == 0) { skip = true; break; }
          }
          if (!skip) break;
          next = (next + 1) % PULSE_ACTION_COUNT;
        }
        strcpy(r.action, pulseActionNames[next]);
        // When rotating to emote/mood_shift, use the emotion tag the model provided
        if (strcmp(r.action, "emote") == 0 || strcmp(r.action, "mood_shift") == 0) {
          if (strlen(r.emotionTag) > 0 && strcmp(r.emotionTag, "NEUTRAL") != 0) {
            strncpy(r.detail, r.emotionTag, sizeof(r.detail) - 1);
            r.detail[sizeof(r.detail) - 1] = '\0';
          } else {
            const char* randomEmotions[] = {"HAPPY","CALM","THINKING_FACE","SHY","CONFUSED","SAD","LOVE","SASSY","WINK","EXCITED"};
            strncpy(r.detail, randomEmotions[random(0, 10)], sizeof(r.detail) - 1);
            r.detail[sizeof(r.detail) - 1] = '\0';
          }
        } else if (strcmp(r.action, "speak") != 0 && strcmp(r.action, "murmur") != 0) {
          r.detail[0] = '\0';
        }
        LOGI("PULSE","Rotated '%s' -> '%s'", original, r.action);
        break;
      }
    }
  }
  strcpy(recentActions[recentIdx % 3], r.action);
  recentIdx++;

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
      // --- TOPIC FOLLOW-UP: track questions (Change #8) ---
      if (strstr(r.detail, "?") != NULL || strcmp(r.action, "start_topic") == 0) {
        strncpy(lastPulseQuestion, r.detail, sizeof(lastPulseQuestion) - 1);
        lastPulseQuestion[sizeof(lastPulseQuestion) - 1] = '\0';
        lastPulseQuestionTime = millis();
      }
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
