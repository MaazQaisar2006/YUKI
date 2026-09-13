// quotes.h — Dynamic quote system with weekly refresh from Quotable API
#pragma once
#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "config.h"
#include "debug.h"

extern char workspace[3200];
extern Config sys;
extern bool canAllocJson(size_t sz);
extern NTPClient timeClient;

// --- FALLBACK QUOTES (PROGMEM, always available) ---
static const char* const fallbackQuotes[] PROGMEM = {
  "The only way to do great work is to love what you do. — Steve Jobs",
  "Innovation distinguishes between a leader and a follower. — Steve Jobs",
  "Stay hungry, stay foolish. — Stewart Brand",
  "Life is what happens when you're busy making other plans. — John Lennon",
  "In the middle of difficulty lies opportunity. — Albert Einstein",
  "Be yourself; everyone else is already taken. — Oscar Wilde",
  "You only live once, but if you do it right, once is enough. — Mae West",
  "Be the change you wish to see in the world. — Mahatma Gandhi",
  "Not all those who wander are lost. — J.R.R. Tolkien",
  "If you tell the truth, you don't have to remember anything. — Mark Twain",
  "Kindness is a language which the deaf can hear and the blind can see. — Mark Twain",
  "The purpose of our lives is to be happy. — Dalai Lama",
  "In three words I can sum up everything I've learned about life: it goes on. — Robert Frost",
  "Freedom lies in being bold. — Robert Frost",
  "The best fighter is never angry. — Lao Tzu",
  "Great acts are made up of small deeds. — Lao Tzu",
  "Happiness can be found even in the darkest of times if one only remembers to turn on the light. — Albus Dumbledore",
  "It is impossible for a man to learn what he thinks he already knows. — Epictetus",
  "Walk towards the sunshine and the shadows will fall behind you. — Mary Engelbreit",
  "Success is an iceberg. — Unknown",
  "Don't spend major time on minor things. — Jim Rohn",
  "Words are the clothes thoughts wear. — Samuel Beckett",
  "All I can do is be me, whoever that is. — Bob Dylan",
  "Just do the best you can. No one can do more than that. — John Wooden",
  "Curiosity is the most powerful thing you own. — James Cameron",
  "Sorrow is how we learn to love. — Rita Mae Brown",
  "Patience is not the ability to wait but the ability to keep a good attitude while waiting. — Joyce Meyer",
  "The finish line is just the beginning of a whole new race. — Unknown",
  "Life is not meant to be easy but take courage: it can be delightful. — George Bernard Shaw",
  "Experience is a comb which nature gives us when we are bald. — Chinese Proverb"
};
static const int FALLBACK_QUOTE_COUNT = sizeof(fallbackQuotes) / sizeof(fallbackQuotes[0]);

// --- QUOTE FILE MANAGEMENT ---
#define QUOTE_FILE "/quotes.json"
#define MAX_QUOTES 50

// Load a random quote from LittleFS, or fallback if unavailable
inline bool loadRandomQuote(char* buf, size_t bufSize) {
  // Try LittleFS first
  if (LittleFS.exists(QUOTE_FILE)) {
    File f = LittleFS.open(QUOTE_FILE, "r");
    if (f) {
      if (!canAllocJson(2048)) { f.close(); } // Low heap, skip
      else {
        DynamicJsonDocument doc(2048);
        DeserializationError err = deserializeJson(doc, f);
        f.close();
        if (!err) {
          JsonArray arr = doc.as<JsonArray>();
          if (arr.size() > 0) {
            int idx = random(0, arr.size());
            const char* quote = arr[idx]["content"] | "";
            const char* author = arr[idx]["author"] | "";
            if (strlen(quote) > 0) {
              if (strlen(author) > 0)
                snprintf(buf, bufSize, "%s — %s", quote, author);
              else
                strncpy(buf, quote, bufSize - 1);
              buf[bufSize - 1] = '\0';
              // Remove used quote from array (no repeats all week)
              arr.remove(idx);
              // Rewrite file
              File fw = LittleFS.open(QUOTE_FILE, "w");
              if (fw) {
                serializeJson(doc, fw);
                fw.close();
              }
              LOGI("QUOTE","Loaded from file: %s", buf);
              return true;
            }
          }
          // File empty or corrupt — delete it
          LOGW("QUOTE","Quote file empty, removing");
          LittleFS.remove(QUOTE_FILE);
        } else {
          LOGW("QUOTE","Parse error, removing file");
          LittleFS.remove(QUOTE_FILE);
        }
      }
    }
  }
  // Fallback to PROGMEM
  int idx = random(0, FALLBACK_QUOTE_COUNT);
  strncpy(buf, fallbackQuotes[idx], bufSize - 1);
  buf[bufSize - 1] = '\0';
  LOGI("QUOTE","Loaded fallback: %s", buf);
  return false;
}

// Fetch quotes from ZenQuotes API and save to LittleFS
inline bool refreshQuotes() {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (!canAllocJson(6144)) { LOGW("QUOTE","Low heap, skipping refresh"); return false; }

  HTTPClient http;
  http.setConnectTimeout(10000);
  http.begin("https://zenquotes.io/api/quotes");
  http.setTimeout(15000);
  int httpCode = http.GET();

  if (httpCode != 200) {
    LOGW("QUOTE","API failed: %d", httpCode);
    http.end();
    return false;
  }

  String resp = http.getString();
  http.end();

  DynamicJsonDocument* doc = new DynamicJsonDocument(6144);
  if (!doc) { LOGW("QUOTE","JSON alloc failed"); return false; }
  DeserializationError err = deserializeJson(*doc, resp);
  if (err) {
    LOGW("QUOTE","JSON parse error: %s", err.c_str());
    delete doc;
    return false;
  }

  JsonArray incoming = doc->as<JsonArray>();
  if (incoming.size() == 0) {
    LOGW("QUOTE","Empty response from API");
    delete doc;
    return false;
  }

  // Build new quote array
  DynamicJsonDocument* newDoc = new DynamicJsonDocument(4096);
  if (!newDoc) { LOGW("QUOTE","New doc alloc failed"); delete doc; return false; }
  JsonArray newArr = newDoc->to<JsonArray>();
  int count = 0;
  for (JsonObject item : incoming) {
    if (count >= MAX_QUOTES) break;
    const char* content = item["q"] | "";
    const char* author = item["a"] | "";
    if (strlen(content) > 0) {
      JsonObject q = newArr.createNestedObject();
      q["content"] = content;
      q["author"] = author;
      count++;
    }
  }

  if (count == 0) {
    LOGW("QUOTE","No valid quotes in response");
    delete doc;
    delete newDoc;
    return false;
  }

  // Save to LittleFS
  File f = LittleFS.open(QUOTE_FILE, "w");
  if (!f) {
    LOGW("QUOTE","Failed to open file for writing");
    delete doc;
    delete newDoc;
    return false;
  }
  serializeJson(*newDoc, f);
  f.close();

  delete doc;
  delete newDoc;

  LOGI("QUOTE","Refreshed %d quotes from API", count);
  return true;
}

// Check if quotes need weekly refresh, do it if needed
inline void checkQuoteRefresh() {
  if (timeClient.isTimeSet() && sys.lastQuoteRefresh > 0) {
    unsigned long now = timeClient.getEpochTime();
    unsigned long elapsed = now - sys.lastQuoteRefresh;
    if (elapsed < 604800UL) return; // 7 days not passed
  }
  // Time to refresh (or first boot with no timestamp)
  LOGI("QUOTE","Weekly refresh triggered");
  if (refreshQuotes()) {
    sys.lastQuoteRefresh = timeClient.getEpochTime();
    // Save will happen in next saveSys() cycle
  }
}
