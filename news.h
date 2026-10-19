// news.h — Google News RSS headline fetcher (no API key needed)
#pragma once
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include "debug.h"

// Fetch 3 top headlines from Google News RSS, return in buf as "h1 | h2 | h3"
inline bool fetchHeadlines(char* buf, size_t bufSize) {
  if (!buf || bufSize < 2) return false;
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClientSecure client;
  client.setInsecure(); // Skip SSL verification (ESP32-C3 limited certs)
  client.setTimeout(3000);
  // TLS handshakes can otherwise wait for the core's long default timeout.
  // Keep news fetches bounded when the remote endpoint is slow or unreachable.
  client.setHandshakeTimeout(5);

  // Bound both TCP connection establishment (milliseconds) and TLS handshake
  // (seconds); setTimeout alone only limits later stream reads.
  if (!client.connect("news.google.com", 443, 5000)) {
    LOGW("NEWS", "TLS connect failed or timed out");
    client.stop();
    return false;
  }

  // Request RSS feed — use top stories URL for fresher content
  client.println("GET /rss?hl=en-US&gl=US&ceid=US:en HTTP/1.1");
  client.println("Host: news.google.com");
  client.println("User-Agent: YukiBot/1.0");
  client.println("Accept: application/rss+xml, application/xml, text/xml");
  client.println("Accept-Encoding: identity");
  client.println("Connection: close");
  client.println();

  // Validate the HTTP response and note whether the body is chunked.
  unsigned long deadline = millis() + 12000;
  String statusLine = client.readStringUntil('\n');
  statusLine.trim();
  if (!statusLine.startsWith("HTTP/") || statusLine.indexOf(" 200 ") < 0) {
    if (statusLine.length()) LOGW("NEWS", "HTTP response rejected: %.64s", statusLine.c_str());
    else LOGW("NEWS", "No HTTP status line before read timeout");
    client.stop();
    return false;
  }
  bool chunked = false;
  bool headersComplete = false;
  while (client.connected() && millis() < deadline) {
    String line = client.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) { headersComplete = true; break; }
    line.toLowerCase();
    if (line.startsWith("transfer-encoding:") && line.indexOf("chunked") >= 0) chunked = true;
  }
  if (!headersComplete) {
    LOGW("NEWS", "Timed out reading HTTP headers");
    client.stop();
    return false;
  }

  // Stream-parse <item> titles. Parse complete XML tags before changing state;
  // the old byte-by-byte matcher treated the slash in </title> as headline text.
  int found = 0;
  buf[0] = '\0';
  bool inItem = false;
  bool inTitle = false;
  bool insideTag = false;
  bool inCdata = false;
  char titleBuf[128] = {0};
  int titleLen = 0;
  char tagBuf[40] = {0};
  int tagLen = 0;
  char cdataTail[3] = {0};
  uint8_t cdataTailLen = 0;
  size_t chunkRemaining = 0;
  bool chunkFinished = false;
  auto nextBodyByte = [&]() -> int {
    if (!chunked) return client.read();
    while (chunkRemaining == 0 && !chunkFinished) {
      String sizeLine = client.readStringUntil('\n');
      sizeLine.trim();
      char* end = nullptr;
      unsigned long size = strtoul(sizeLine.c_str(), &end, 16);
      if (end == sizeLine.c_str() || size == 0) { chunkFinished = true; return -2; }
      chunkRemaining = (size_t)size;
    }
    if (chunkFinished) return -2;
    int value = client.read();
    if (value >= 0 && --chunkRemaining == 0) {
      client.read();
      client.read();
    }
    return value;
  };

  while (client.connected() && millis() < deadline) {
    int next = nextBodyByte();
    if (next == -2) break;
    if (next < 0) { delay(1); continue; }
    char c = (char)next;

    if (inCdata) {
      if (cdataTailLen < 3) cdataTail[cdataTailLen++] = c;
      if (cdataTailLen == 3) {
        if (memcmp(cdataTail, "]]>", 3) == 0) {
          inCdata = false;
          cdataTailLen = 0;
        } else {
          if (inTitle && titleLen < (int)sizeof(titleBuf) - 1)
            titleBuf[titleLen++] = cdataTail[0];
          cdataTail[0] = cdataTail[1];
          cdataTail[1] = cdataTail[2];
          cdataTailLen = 2;
        }
      }
      continue;
    }

    if (!insideTag) {
      if (c == '<') {
        insideTag = true;
        tagLen = 0;
        tagBuf[tagLen++] = '<';
      } else if (inItem && inTitle && titleLen < (int)sizeof(titleBuf) - 1) {
        titleBuf[titleLen++] = c;
        titleBuf[titleLen] = '\0';
      }
      continue;
    }

    if (tagLen < (int)sizeof(tagBuf) - 1) tagBuf[tagLen++] = c;
    // CDATA's opening marker has no closing '>' of its own.
    if (tagLen == 9 && memcmp(tagBuf, "<![CDATA[", 9) == 0) {
      insideTag = false;
      inCdata = true;
      cdataTailLen = 0;
      continue;
    }
    if (c != '>') continue;
    tagBuf[tagLen] = '\0';
    insideTag = false;

    if (strcmp(tagBuf, "<item>") == 0) {
      inItem = true;
      inTitle = false;
      titleLen = 0;
      titleBuf[0] = '\0';
    } else if (strncasecmp(tagBuf, "<title", 6) == 0) {
      if (inItem) {
        inTitle = true;
        titleLen = 0;
        titleBuf[0] = '\0';
      }
    } else if (strcmp(tagBuf, "</title>") == 0) {
      inTitle = false;
    } else if (strcmp(tagBuf, "</item>") == 0) {
      if (inItem && titleLen > 0 && found < 3) {
        titleBuf[titleLen] = '\0';
        if (found > 0) strncat(buf, " | ", bufSize - strlen(buf) - 1);
        strncat(buf, titleBuf, bufSize - strlen(buf) - 1);
        found++;
      }
      inItem = false;
      inTitle = false;
      if (found >= 3) break;
    }
  }

  client.stop();

  // Clean up HTML entities in-place
  auto replaceInPlace = [](char* buf, size_t bufSize, const char* find, const char* replace) {
    size_t findLen = strlen(find);
    size_t replaceLen = strlen(replace);
    char* pos = buf;
    while ((pos = strstr(pos, find)) != NULL) {
      size_t remaining = strlen(pos + findLen);
      if (strlen(buf) - findLen + replaceLen >= bufSize) break;
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
