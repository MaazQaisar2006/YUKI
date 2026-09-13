#pragma once

struct PulseResult {
  char action[16];
  char detail[160];
  char emotionTag[16];
};

extern unsigned long microExpressionUntil;
extern Emotion microEmotion;

static const char* const pulseActionNames[] = {
  "speak", "murmur", "emote", "mood_shift", "hum", "sigh",
  "giggle", "fidget", "stretch", "stare", "react", "nothing",
  "micro_expression", "sleep", "quote", "news"
};
static const int PULSE_ACTION_COUNT = 16;

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

static PulseResult callPulseAI(const char* context) {
  PulseResult r;
  strcpy(r.action, "nothing"); r.detail[0] = '\0'; r.emotionTag[0] = '\0';

  if (WiFi.status() != WL_CONNECTED) return r;

  WiFiClientSecure client;
  client.setCACert(NULL);
  client.setInsecure();
  client.setTimeout(10000);
  HTTPClient http;
  http.begin(client, "https://api.groq.com/openai/v1/chat/completions");
  http.setTimeout(15000);
  http.setReuse(false);
  http.addHeader("Content-Type", "application/json");
  char auth[128];
  snprintf(auth, sizeof(auth), "Bearer %s", API_KEY);
  http.addHeader("Authorization", auth);

  // Reduced from 1024 to 512 to save stack space
  StaticJsonDocument<512> doc;
  JsonArray messages = doc.createNestedArray("messages");

  JsonObject sysMsg = messages.createNestedObject();
  sysMsg["role"] = "system";
  sysMsg["content"] =
    "You are Yuki's inner voice. Given your current state, pick ONE action from: "
    "speak | murmur | emote | mood_shift | hum | sigh | giggle | fidget | stretch | stare | react | nothing | micro_expression | sleep. "
    "For speak: DETAIL is what you say (natural, in-character, 4-12 words). "
    "If your recent inner lines are given, continue that thread naturally sometimes, react to your own last thought, or start fresh — vary it. "
    "For murmur: DETAIL is a 1-3 word quiet phrase like 'mm-hmm' or 'nice...'. "
    "For emote: DETAIL is the emotion (HAPPY/SAD/LOVE/SHY/WINK/SURPRISED/CONFUSED/etc). "
    "For mood_shift: DETAIL is the new mood name. "
    "For hum/sigh/giggle/fidget/stretch/stare: DETAIL can be empty. "
    "For react: DETAIL is the emotion (same as emote) — includes a brief expression sound. "
    "For nothing: DETAIL can be empty. "
    "For micro_expression: DETAIL is a 1-word emotion like HAPPY/SAD/WINK (500ms flash, no text/sound). "
    "For sleep: goes to sleep (DETAIL can be empty). Use when it feels right — tired, late, or just quiet. "
    "Reply in EXACT format:\nACTION=<action>\nDETAIL=<detail>\nEMOTION=<optional emotion tag for speak>";

  JsonObject userMsg = messages.createNestedObject();
  userMsg["role"] = "user";
  userMsg["content"] = context;

  doc["model"] = "qwen/qwen3.6-27b";
  doc["max_tokens"] = 256;
  doc["reasoning_effort"] = "none";
  doc["temperature"] = 0.9;

  size_t payloadLen = measureJson(doc);
  serializeJson(doc, workspace, sizeof(workspace));

  int httpCode = http.POST((uint8_t*)workspace, payloadLen);
  if (httpCode > 0) {
    StaticJsonDocument<256> filter;
    filter["choices"][0]["message"]["content"] = true;
    StaticJsonDocument<512> resp;
    Stream& stream = http.getStream();
    DeserializationError err = deserializeJson(resp, stream, DeserializationOption::Filter(filter));
    if (!err) {
      const char* text = resp["choices"][0]["message"]["content"];
      if (text) {
        LOGI("PULSE","AI raw: %s", text);
        const char* a = strstr(text, "ACTION=");
        const char* d = strstr(text, "DETAIL=");
        const char* e = strstr(text, "EMOTION=");
        if (a) {
          a += 7;
          int len = 0;
          while (a[len] && a[len] != '\n' && a[len] != '\r' && len < 15) len++;
          strncpy(r.action, a, len); r.action[len] = '\0';
          if (!isValidPulseAction(r.action)) {
            LOGW("PULSE","Invalid action '%s' -> nothing", r.action);
            strcpy(r.action, "nothing");
          }
        }
        if (d) {
          d += 7;
          int len = 0;
          while (d[len] && d[len] != '\n' && d[len] != '\r' && len < 159) len++;
          strncpy(r.detail, d, len); r.detail[len] = '\0';
        }
        if (e) {
          e += 8;
          int len = 0;
          while (e[len] && e[len] != '\n' && e[len] != '\r' && len < 15) len++;
          strncpy(r.emotionTag, e, len); r.emotionTag[len] = '\0';
        }
        LOGI("PULSE","Parsed: action=%s detail='%s' emotion=%s", r.action, r.detail, r.emotionTag);
      }
    } else {
      LOGW("PULSE","JSON parse error in AI response");
    }
  } else {
    LOGW("PULSE","HTTP POST failed: code=%d", httpCode);
  }
  http.end(); client.stop();
  return r;
}

static void buildPulseContext(char* buf, size_t size) {
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

  snprintf(buf, size,
    "Time: %d:00 %s (%s). Energy: %s (%.2f). Mood: %s. Affinity: %d. Level: %d. "
    "User: %s (%lu min quiet). Feel: %s. Weather: %s, %dC. "
    "Greetings: morning_said=%s night_said=%s. "
    "Battery: %.1fV. Mode: FACE=%s.",
    hr, tod, todHint, energyDesc, yukiEnergy, emoStr, sys.affinity, sys.level,
    presence, minsSinceInteraction, interactionDesc, weather, temp,
    saidGoodMorning ? "yes" : "no", saidGoodNight ? "yes" : "no",
    vcc,
    (currentMode == FACE) ? "yes" : "no");

  if (pulseMemoryCount > 0) {
    int used = strlen(buf);
    snprintf(buf + used, size - used, " Recent inner lines: %s%s%s%s%s%s",
      pulseMemoryCount > 0 ? "1) " : "", pulseMemoryCount > 0 ? pulseMemory[0] : "",
      pulseMemoryCount > 1 ? " 2) " : "", pulseMemoryCount > 1 ? pulseMemory[1] : "",
      pulseMemoryCount > 2 ? " 3) " : "", pulseMemoryCount > 2 ? pulseMemory[2] : "");
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
  // Rotate away from recent actions to keep variety natural
  static char recentActions[3][16] = {};
  static int recentIdx = 0;
  bool isRepeat = false;
  for (int i = 0; i < 3; i++) {
    if (strlen(recentActions[i]) > 0 && strcmp(r.action, recentActions[i]) == 0) {
      isRepeat = true; break;
    }
  }
  if (isRepeat && strcmp(r.action, "nothing") != 0) {
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
        if (strcmp(r.action, "speak") != 0 && strcmp(r.action, "murmur") != 0) r.detail[0] = '\0';
        LOGI("PULSE","Rotated '%s' -> '%s'", original, r.action);
        break;
      }
    }
  }
  strcpy(recentActions[recentIdx % 3], r.action);
  recentIdx++;

  lastIdleAction = millis();
  lastActivity = millis();
  LOGI("PULSE","Exec: %s detail='%s' emo=%s", r.action, r.detail, r.emotionTag);

  if (strcmp(r.action, "speak") == 0) {
    if (strlen(r.detail) > 0) {
      if (strlen(r.emotionTag) > 0) {
        Emotion e = parseEmotionTag(r.emotionTag);
        if (e != NEUTRAL || strcmp(r.emotionTag, "NEUTRAL") == 0) {
          currentEmotion = e; emotionSetTime = millis();
        }
      }
      strncpy(aiMsg, r.detail, sizeof(aiMsg) - 1);
      aiMsg[sizeof(aiMsg) - 1] = '\0';
      scrollOffset = 0;
      logLocalChat("Yuki", aiMsg);
      sound_notify();
      ttsQueue(aiMsg);   // companion voice: she says her own lines aloud
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
    if (sys.soundOn && sys.level >= 3) sound_hum();
  } else if (strcmp(r.action, "emote") == 0) {
    if (strlen(r.detail) > 0) {
      Emotion e = parseEmotionTag(r.detail);
      currentEmotion = e; emotionSetTime = millis();
    }
  } else if (strcmp(r.action, "mood_shift") == 0) {
    if (strlen(r.detail) > 0) {
      Emotion e = parseEmotionTag(r.detail);
      currentEmotion = e; emotionSetTime = millis();
      updateEnergyLevel();
    }
  } else if (strcmp(r.action, "hum") == 0) {
    if (sys.soundOn && sys.level >= 3) sound_hum();
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
