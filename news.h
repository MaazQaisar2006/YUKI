// news.h — Google News RSS headline fetcher (no API key needed)
#pragma once
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include "debug.h"

// Fetch 3 top headlines from Google News RSS, return in buf as "h1 | h2 | h3"
inline bool fetchHeadlines(char* buf, size_t bufSize) {
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClientSecure client;
  client.setInsecure(); // Skip SSL verification (ESP32-C3 limited certs)
  client.setTimeout(15000);

  if (!client.connect("news.google.com", 443)) {
    LOGW("NEWS","Connect failed");
    return false;
  }

  // Request RSS feed
  client.println("GET /rss?hl=en-US&gl=US&ceid=US:en HTTP/1.1");
  client.println("Host: news.google.com");
  client.println("User-Agent: YukiBot/1.0");
  client.println("Connection: close");
  client.println();

  // Wait for response headers
  unsigned long timeout = millis() + 10000;
  while (client.connected() && millis() < timeout) {
    String line = client.readStringUntil('\n');
    if (line == "\r" || line == "\n") break; // Headers done
  }

  // Stream-parse <title> tags — no XML library needed
  int found = 0;
  buf[0] = '\0';
  bool inTitle = false;
  unsigned long readTimeout = millis() + 10000;

  while (client.connected() && millis() < readTimeout) {
    char c = client.read();
    if (c < 0) { delay(1); continue; }

    // Simple state machine: find <title> ... </title>
    static char tagBuf[8] = {0};
    static int tagLen = 0;

    if (!inTitle) {
      // Accumulate tag characters
      if (c == '<') {
        tagLen = 0;
        tagBuf[0] = '\0';
      }
      if (tagLen < 7) {
        tagBuf[tagLen++] = c;
        tagBuf[tagLen] = '\0';
      }
      // Check if we hit <title> (opening tag, skip <![CDATA[)
      if (strstr(tagBuf, "<title>")) {
        inTitle = true;
        tagLen = 0;
        // Skip CDATA if present
        delay(50);
        while (client.available()) {
          char peek = client.peek();
          if (peek == '<') break; // Hit </title>
          if (peek == '[' || peek == '!') { client.read(); continue; }
          break;
        }
      }
    } else {
      // Accumulate title content
      if (c == '<') {
        // End of title
        inTitle = false;
        if (strlen(buf) > 0 && found < 2) {
          strncat(buf, " | ", bufSize - strlen(buf) - 1);
        }
        found++;
        tagLen = 0;
        tagBuf[0] = '\0';
        if (found >= 3) break;
      } else {
        // Append character to buffer
        size_t len = strlen(buf);
        if (len < bufSize - 2) {
          buf[len] = c;
          buf[len + 1] = '\0';
        }
      }
    }
  }

  client.stop();

  // Clean up HTML entities in-place (no heap allocation)
  // Simple replace: find pattern, shift remainder, insert replacement
  auto replaceInPlace = [](char* buf, size_t bufSize, const char* find, const char* replace) {
    size_t findLen = strlen(find);
    size_t replaceLen = strlen(replace);
    char* pos = buf;
    while ((pos = strstr(pos, find)) != NULL) {
      size_t remaining = strlen(pos + findLen);
      if (strlen(buf) - findLen + replaceLen >= bufSize) break; // Would overflow
      memmove(pos + replaceLen, pos + findLen, remaining + 1);
      memcpy(pos, replace, replaceLen);
      pos += replaceLen;
    }
  };
  
  replaceInPlace(buf, bufSize, "&amp;", "&");
  replaceInPlace(buf, bufSize, "&quot;", "\"");
  replaceInPlace(buf, bufSize, "&#39;", "'");
  replaceInPlace(buf, bufSize, "&lt;", "<");
  replaceInPlace(buf, bufSize, "&gt;", ">");
  
  // Trim whitespace
  char* start = buf;
  while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r') start++;
  if (start != buf) {
    size_t len = strlen(start);
    memmove(buf, start, len + 1);
  }
  size_t len = strlen(buf);
  while (len > 0 && (buf[len-1] == ' ' || buf[len-1] == '\t' || buf[len-1] == '\n' || buf[len-1] == '\r')) {
    buf[--len] = '\0';
  }

  if (found > 0) {
    LOGI("NEWS","Fetched %d headlines", found);
    return true;
  }
  LOGW("NEWS","No headlines found");
  return false;
}
