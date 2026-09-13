// tts.h — Edge TTS voice playback via gptimer ISR + hardware LEDC PWM (switchable)
// Streams 16kHz mono PCM (8- or 16-bit WAV) from the TTS relay straight into the
// sample ring — no full-body buffer (the C3 String class caps at 64KB).
// All sound (SFX + voice) shares the same transistor-amp speaker on SPK_PIN.
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "driver/gptimer.h"
#include "driver/ledc.h"
#include "esp_rom_sys.h"
#include "config.h"
#include "debug.h"

// ===========================================================================
// Driver selection. 0 = hardware LEDC (default): the 16kHz ISR latches a new
// duty via the IDF driver calls in ttsWriteDuty. Probe-tested (ledc_probe
// v6/v7): on this C3 only the driver path latches reliably — every raw-register
// store form (full-word, 3-store, RMW, with/without wait) freezes after the
// first latch. 1 = software PWM in the ISR — the exact drive the audible SFX
// beeps use (active-low pulse, idle HIGH, pulse width carries the volume).
// Flipping this flag alone reverts to the SW path; nothing else is affected.
// ===========================================================================
#define TTS_USE_SW_PWM 0
#define TTS_SOFT_START 1   // 10ms fade-in on the first samples (no start click)

// TTS runs in its own FreeRTOS task so the main loop (buttons, display, MQTT)
// never blocks for the fetch+playback duration. 0 = real path.
#define TTS_TASK_PRIO 1     // same as the Arduino loop task — interleaves, never starves
#define TTS_TASK_STACK 4096 // verified via uxTaskGetStackHighWaterMark after first job

// C3 GPIO20 output registers (DR_REG_GPIO_BASE = 0x60004000): W1TS set / W1TC clear
#define TTS_GPIO_OUT_W1TS (*(volatile uint32_t *)(0x60004000UL + 0x0008))
#define TTS_GPIO_OUT_W1TC (*(volatile uint32_t *)(0x60004000UL + 0x000C))
#define TTS_SPK_MASK (1UL << SPK_PIN)

extern Config sys;
extern bool lowVoltageMode;

#ifndef TTS_HOST
#define TTS_HOST "192.168.100.27"
#endif
#ifndef TTS_PORT
#define TTS_PORT 8000
#endif

#define TTS_RING_SIZE 8192
#define TTS_MAX_TEXT 300

// --- LEDC output (hardware PWM on SPK_PIN, 78.125kHz carrier, 8-bit) ---
// The ISR latches each new duty via the IDF driver calls in ttsWriteDuty. The
// driver calls live in flash — accepted ISR cache-miss jitter (probe verified
// the ISR still holds 16000 samples/s). Duty readback used only for startup
// latch verification. Low-speed channel 0, per ledcAttach below.
#define TTS_LEDC_DUTY_R_REG (*(volatile uint32_t *)(0x60019000UL + 0x0010)) // LSCH0 active duty (>> 4)

static inline void IRAM_ATTR ttsWriteDuty(int duty) {
  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, (uint32_t)duty);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

volatile bool ttsPlaying = false;
char ttsPendingText[TTS_MAX_TEXT];
bool ttsPendingSpeak = false;

static volatile uint8_t ttsRing[TTS_RING_SIZE];
static volatile uint16_t ttsHead = 0, ttsTail = 0;
static volatile uint8_t ttsVol = 50;
static gptimer_handle_t ttsTimer = NULL;
static volatile uint32_t ttsIsrTicks = 0;
static volatile uint8_t ttsPlayMode = 0;   // 0 = LEDC (default), 1 = software PWM
static TaskHandle_t ttsTask = NULL;        // worker task (fetch+stream+playback)
static char ttsJobText[TTS_MAX_TEXT];      // job handed to the worker by speakText
static volatile bool ttsJobPending = false;

// Selectable voices: seeded with the curated list, replaced by the relay's
// /voices/selectable response (region-filtered, probe-verified) on first fetch.
static char ttsVoiceList[10][24] = {
  "en-US-AriaNeural", "en-US-EmmaNeural", "en-US-JennyNeural", "en-US-GuyNeural",
  "en-GB-SoniaNeural", "en-GB-LibbyNeural", "en-AU-NatashaNeural", "en-CA-ClaraNeural", "", ""
};
static volatile int ttsVoiceCount = 8;
static volatile bool ttsVoiceRefresh = false;  // menu entry -> worker refreshes the list

// --- Test-bench knobs (RAM-only: menu writes, ISR reads once per tick) ---
volatile uint32_t ttsKnobPulseUs = 55;   // 55/45/35/25 — max pulse width in µs
volatile int     ttsKnobPolarity = 0;    // 0 NORM (active-low, idle HIGH) / 1 INV (active-high, idle LOW)
volatile int     ttsKnobDeadband = 0;    // 0/4/8/16/32 — silence gate around sample 128
volatile int     ttsKnobSpread = 0;      // 0 lin / 1 boost 1.5x / 2 boost 2x / 3 clip
volatile int     ttsKnobVolGain = 100;   // 50/75/100/125/150 — playback volume multiplier %
volatile bool    ttsMonitor = false;     // live got/ticks heartbeat during playback
char ttsLastText[TTS_MAX_TEXT] = "";     // last spoken sentence (menu Replay)

// Copy the persisted voice config (Config struct) into the live ISR knobs.
// Called once per playback start; the Voice Config menu writes sys + saveSys.
inline void ttsSyncFromSys() {
  ttsKnobPulseUs = (uint32_t)sys.voicePulse;
  ttsKnobPolarity = sys.voicePolarity ? 1 : 0;
  ttsKnobDeadband = sys.voiceDeadband;
  ttsKnobSpread = sys.voiceSpread;
  ttsKnobVolGain = sys.voiceGain;
}

// IRAM-safe: driver calls only — no raw register stores (probe: raw stores of
// any form freeze after the first latch; only the driver path latches).
bool IRAM_ATTR ttsSampleTick(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx) {
  if (!ttsPlaying) return false;
  ttsIsrTicks++;
  if (ttsHead == ttsTail) return false;   // underrun: hold current output
  uint8_t s = ttsRing[ttsTail];
  ttsTail = (ttsTail + 1) & (TTS_RING_SIZE - 1);
#if TTS_SOFT_START
  // 10ms soft-start (160 samples @16kHz): ramp in so speech doesn't click on start
  if (ttsIsrTicks <= 160) {
    int sv = (int)s - 128;
    s = (uint8_t)(128 + (sv * (int)ttsIsrTicks) / 160);
  }
#endif
  int dv = (int)s - 128;
  int db = ttsKnobDeadband;
  if (db > 0 && dv >= -db && dv <= db) {   // silence gate: hold idle (SW) or 0% duty (LEDC)
#if TTS_USE_SW_PWM
    if (ttsKnobPolarity == 0) TTS_GPIO_OUT_W1TS = TTS_SPK_MASK;
    else TTS_GPIO_OUT_W1TC = TTS_SPK_MASK;
#else
    ttsWriteDuty(0);
#endif
    return false;
  }
  if (ttsKnobSpread == 1) {                // boost 1.5x: expand quiet range (clamped)
    dv = (dv * 3) / 2;
    if (dv > 100) dv = 100; else if (dv < -100) dv = -100;
  } else if (ttsKnobSpread == 2) {         // boost 2x: more of the same
    dv = dv * 2;
    if (dv > 100) dv = 100; else if (dv < -100) dv = -100;
  } else if (ttsKnobSpread == 3) {         // clip: cap deviation, keep amp headroom
    if (dv > 102) dv = 102; else if (dv < -102) dv = -102;
  }
  int duty = 128 + (dv * ttsVol) / 100;
  if (duty > 255) duty = 255; else if (duty < 0) duty = 0;
#if TTS_USE_SW_PWM
  if (ttsPlayMode) {
    // Software PWM: one pulse per tick, idle between pulses — the exact drive
    // the (audible) SFX beeps use, pulse width carrying the volume. Pure GPIO
    // register writes + a ROM busy-wait: IRAM-safe and tick-exact (verified by
    // `done ... isr ticks` staying equal to the byte count). Max pulse is the
    // test knob (default 55µs of the 62.5µs tick; shorter = more headroom for
    // higher-priority WiFi ISRs). The Polarity knob flips active-low (idle
    // HIGH — heavy DC bias through the coil, current default) vs active-high
    // (idle LOW — near-zero bias, amp linear region test). (The cycle-timed
    // variant overran the tick on this chip and screeched — do not reintroduce.)
    uint32_t us = ((uint32_t)duty * ttsKnobPulseUs) / 255;
    if (ttsKnobPolarity == 0) {            // NORM: active-low pulse, idle HIGH
      if (us) {
        TTS_GPIO_OUT_W1TC = TTS_SPK_MASK;
        esp_rom_delay_us(us);
      }
      TTS_GPIO_OUT_W1TS = TTS_SPK_MASK;
    } else {                               // INV: active-high pulse, idle LOW
      if (us) {
        TTS_GPIO_OUT_W1TS = TTS_SPK_MASK;
        esp_rom_delay_us(us);
      }
      TTS_GPIO_OUT_W1TC = TTS_SPK_MASK;
    }
    return false;
  }
#endif
  ttsWriteDuty(duty);
  return false;
}

// Queue a reply to be spoken by the main loop (never blocks the AI path)
inline void ttsQueue(const char* text) {
  if (!text || !sys.soundOn || !sys.voiceOn || ttsPlaying) return;
  strncpy(ttsPendingText, text, sizeof(ttsPendingText) - 1);
  ttsPendingText[sizeof(ttsPendingText) - 1] = '\0';
  ttsPendingSpeak = true;
  LOGI("TTS", "queued: %s", ttsPendingText);
}

// Relay host: use ttsRelayUrl if configured, else phone IP, else PC fallback.
static inline const char* ttsRelayHost() {
  if (strlen(sys.ttsRelayUrl) > 0) return sys.ttsRelayUrl;
  if (strlen(sys.phoneIP) > 0) return sys.phoneIP;
  return TTS_HOST;
}

// Fetch /voices/selectable from the relay and repopulate ttsVoiceList.
// Runs inside the worker task (network + flash-free); the menu only sets the
// refresh flag. Writes the array first and the count last so the menu's
// count-gated reads never see a torn list. A failed/empty fetch keeps the
// current list.
static void ttsFetchVoiceList() {
  if (!sys.voiceOn || !sys.soundOn) return;   // muted: no backend traffic
  WiFiClient c;
  c.setTimeout(8000);
  if (!c.connect(ttsRelayHost(), TTS_PORT)) { LOGW("TTS", "voices: connect failed"); return; }
  c.printf("GET /voices/selectable HTTP/1.1\r\nHost: %s:%d\r\nConnection: close\r\n\r\n",
           ttsRelayHost(), TTS_PORT);
  char buf[768];
  int n = 0;
  unsigned long t0 = millis();
  while (n < (int)sizeof(buf) - 1 && millis() - t0 < 9000) {
    if (c.available()) {
      int r = c.read((uint8_t*)buf + n, sizeof(buf) - 1 - n);
      if (r > 0) n += r; else if (r < 0) break;
    } else if (!c.connected()) break;
    else delay(1);
  }
  buf[n] = '\0';
  char* body = strstr(buf, "\r\n\r\n");
  if (!body) { LOGW("TTS", "voices: bad response"); return; }
  body += 4;
  int vc = 0, i = 0;
  int len = (int)strlen(body);
  while (vc < 10 && i < len) {          // flat JSON array: ["a","b",...]
    if (body[i] == '"') {
      int j = i + 1, k = 0;
      while (j < len && body[j] != '"' && k < 23) ttsVoiceList[vc][k++] = body[j++];
      ttsVoiceList[vc][k] = '\0';
      if (k > 0) vc++;
      i = j;
    }
    i++;
  }
  if (vc > 0) {
    ttsVoiceCount = vc;
    LOGI("TTS", "voices: %d selectable (first: %s)", vc, ttsVoiceList[0]);
  } else {
    LOGW("TTS", "voices: empty list, keeping cached");
  }
}

// Blocking: POST text to the relay, then stream the WAV straight from the socket
// into the sample ring — no full-body String buffer. (The C3 Arduino core caps
// String at 65535 bytes without PSRAM, which silently truncated every WAV over
// 64KB.) The relay caches repeated texts, so retries after a mid-stream failure
// return instantly from cache. Call from the main loop.
static bool ttsStreamPlay(const char* body, int bodyLen) {
  WiFiClient client;
  client.setTimeout(15);   // per-read cap; the head loop below has its own 25s guard
  // Relay host: the phone's IP (set via MQTT, same IP presence pings) when
  // known, else the PC fallback.
  const char* host = ttsRelayHost();
  if (!client.connect(host, TTS_PORT)) { LOGW("TTS", "connect %s:%d failed", host, TTS_PORT); return false; }
  LOGI("TTS", "relay: %s:%d", host, TTS_PORT);
  client.printf("POST /tts HTTP/1.1\r\nHost: %s:%d\r\nContent-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",
                host, TTS_PORT, bodyLen);
  client.write((const uint8_t*)body, bodyLen);

  // HTTP head, ONE byte at a time — a while(client.available()) drain here
  // over-reads into the body.
  String head;
  unsigned long t0 = millis();
  while (head.length() < 4 || head.substring(head.length() - 4) != "\r\n\r\n") {
    if (millis() - t0 > 25000) { LOGW("TTS", "head timeout"); return false; }
    if (!client.available()) { delay(1); continue; }
    head += (char)client.read();
  }
  int code = 0;
  sscanf(head.c_str(), "HTTP/1.%*d %d", &code);
  if (code != 200) { LOGW("TTS", "http %d", code); return false; }
  long clen = -1;
  int ci = head.indexOf("Content-Length:");
  if (ci >= 0) clen = atol(head.c_str() + ci + 15);
  LOGI("TTS", "status 200, clen: %ld", clen);

  // WAV header: fixed 44 bytes for the relay's RIFF/fmt(16)/data layout.
  // Chunk-walk it like the old in-RAM parser, but the declared data size IS the
  // real PCM length (relay builds the header from actual bytes — no clamping).
  uint8_t hdr[44];
  size_t hgot = 0;
  t0 = millis();
  while (hgot < sizeof(hdr)) {
    if (client.available()) {
      int n = client.read(hdr + hgot, sizeof(hdr) - hgot);
      if (n > 0) { hgot += (size_t)n; t0 = millis(); }
      else if (n < 0) break;
    } else if (!client.connected()) break;
    else if (millis() - t0 > 15000) { LOGW("TTS", "header idle timeout"); return false; }
  }
  if (hgot < sizeof(hdr) || memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) {
    LOGW("TTS", "bad wav header: %d bytes", (int)hgot);
    return false;
  }
  uint16_t bits = 16;
  uint32_t dataLen = 0;
  size_t pos = 12;
  while (pos + 8 <= sizeof(hdr)) {
    uint32_t sz = (uint32_t)hdr[pos + 4] | ((uint32_t)hdr[pos + 5] << 8) |
                  ((uint32_t)hdr[pos + 6] << 16) | ((uint32_t)hdr[pos + 7] << 24);
    if (memcmp(hdr + pos, "fmt ", 4) == 0 && sz >= 16 && pos + 24 <= sizeof(hdr)) {
      bits = (uint16_t)hdr[pos + 8 + 14] | ((uint16_t)hdr[pos + 8 + 15] << 8);
    }
    if (memcmp(hdr + pos, "data", 4) == 0) { dataLen = sz; break; }
    if (sz > sizeof(hdr) - pos - 8) break;   // end of header (data chunk size is large)
    pos += 8 + sz;
  }
  if (dataLen == 0 || dataLen > 2000000) {
    LOGW("TTS", "bad data len: %lu", (unsigned long)dataLen);
    return false;
  }
  LOGI("TTS", "wav: %lu bytes, %u-bit", (unsigned long)dataLen, (unsigned)bits);

  // --- Start playback: gptimer ISR 16kHz drives SPK_PIN (software PWM or LEDC) ---
  ttsSyncFromSys();   // load persisted voice config into the live knobs
  ttsVol = (uint8_t)constrain(((uint32_t)sys.soundVolume * (uint32_t)ttsKnobVolGain) / 100, 0, 200);
  if (ttsVol == 0) return false;
#if TTS_USE_SW_PWM
  // Voice drives the pin directly (software PWM) — plain GPIO, idle per polarity.
  pinMode(SPK_PIN, OUTPUT);
  digitalWrite(SPK_PIN, ttsKnobPolarity ? LOW : HIGH);
  ttsPlayMode = 1;
#else
  // Hardware LEDC, 78.125kHz carrier / 8-bit (probe-validated timer config).
  bool attached = ledcAttach(SPK_PIN, 78125, 8);
  ttsWriteDuty(128);
  // Latch check: active-duty readback should settle to 128 (128<<4 = 0x800).
  LOGI("TTS", "attach: %d vol: %u duty_rd: %u", (int)attached, (unsigned)ttsVol,
       (unsigned)(TTS_LEDC_DUTY_R_REG >> 4));
  ttsPlayMode = 0;
#endif
  ttsHead = ttsTail = 0;
  ttsIsrTicks = 0;
  ttsPlaying = true;
  if (!ttsTimer) {
    // Created directly via the ESP-IDF driver (NOT Arduino timerBegin). The
    // gptimer driver accepts priorities 1-3 (ESP_INTR_FLAG_LOWMED) — fixed at
    // 3, the highest allowed, to minimize preemption by shared interrupts.
    // 16MHz resolution, alarm at count 1000 = every 62.5µs, same period and
    // same level-compare fire-on-every-tick behaviour as the old 16kHz alarm 1.
    gptimer_config_t gcfg = {};
    gcfg.clk_src = GPTIMER_CLK_SRC_DEFAULT;
    gcfg.direction = GPTIMER_COUNT_UP;
    gcfg.resolution_hz = 16000000;
    gcfg.intr_priority = 3;
    gcfg.flags.intr_shared = true;
    if (gptimer_new_timer(&gcfg, &ttsTimer) != ESP_OK) {
      LOGW("TTS", "gptimer_new_timer failed");
      ttsPlaying = false;
#if !TTS_USE_SW_PWM
      ledcDetach(SPK_PIN);
#endif
      return false;
    }
    gptimer_event_callbacks_t gcbs = {};
    gcbs.on_alarm = ttsSampleTick;
    if (gptimer_register_event_callbacks(ttsTimer, &gcbs, NULL) != ESP_OK) {
      LOGW("TTS", "gptimer callbacks failed");
      ttsPlaying = false;
      return false;
    }
    static gptimer_alarm_config_t galrm;
    galrm.alarm_count = 1000;
    galrm.reload_count = 0;
    galrm.flags.auto_reload_on_alarm = true;
    if (gptimer_set_alarm_action(ttsTimer, &galrm) != ESP_OK) {
      LOGW("TTS", "gptimer alarm failed");
      ttsPlaying = false;
      return false;
    }
    gptimer_enable(ttsTimer);
    LOGI("TTS", "gptimer ready (16kHz, prio 3)");
  }
  gptimer_set_raw_count(ttsTimer, 0);
  gptimer_start(ttsTimer);

  LOGI("TTS", "params: pulse=%u pol=%d dead=%d spread=%d gain=%d mon=%d vol=%u",
       (unsigned)ttsKnobPulseUs, ttsKnobPolarity, ttsKnobDeadband, ttsKnobSpread,
       ttsKnobVolGain, (int)ttsMonitor, (unsigned)ttsVol);

ttsVol = (uint8_t)constrain(((uint32_t)sys.soundVolume * (uint32_t)ttsKnobVolGain) / 100, 0, 200);
  ttsIsrTicks = 0;

  // --- Stream PCM from the socket into the ring (stall-guarded, never hangs) ---
  uint8_t sbuf[512];
  size_t got = 0;
  bool carry = false;
  uint8_t carryByte = 0;
  bool aborted = false;
  unsigned long lastPush = millis();
  unsigned long lastMon = millis();
  while (got < dataLen) {
    if (ttsMonitor && millis() - lastMon >= 250) {
      lastMon = millis();
      LOGI("TTS", "mid: got=%u ticks=%u", (unsigned)got, (unsigned)ttsIsrTicks);
    }
    if (client.available()) {
      int n = client.read(sbuf, sizeof(sbuf));
      if (n <= 0) {
        LOGW("TTS", "stream read error, got %u of %lu", (unsigned)got, (unsigned long)dataLen);
        aborted = true;
        break;
      }
      t0 = millis();
      for (int i = 0; i < n && !aborted; i++) {
        uint8_t u8;
        if (bits == 16) {
          if (carry) {
            int16_t s = (int16_t)(carryByte | ((uint16_t)sbuf[i] << 8));
            carry = false;
            u8 = (uint8_t)((s >> 8) + 128);
          } else {
            carryByte = sbuf[i];
            carry = true;
            continue;
          }
        } else {
          u8 = sbuf[i];
        }
        while (((uint16_t)(ttsHead - ttsTail)) >= TTS_RING_SIZE - 1) {
          if (millis() - lastPush > 10000) {   // ISR stalled — must not hang the loop
            LOGW("TTS", "ring stall, aborting");
            aborted = true;
            break;
          }
          yield();
          delay(1);
        }
        if (aborted) break;
        ttsRing[ttsHead] = u8;
        ttsHead = (ttsHead + 1) & (TTS_RING_SIZE - 1);
        lastPush = millis();
      }
      got += (size_t)n;
    } else if (!client.connected()) {
      if (got < dataLen) {
        LOGW("TTS", "stream closed early, got %u of %lu", (unsigned)got, (unsigned long)dataLen);
        aborted = true;
      }
      break;
    } else if (millis() - t0 > 20000) {
      LOGW("TTS", "stream idle timeout, got %u of %lu", (unsigned)got, (unsigned long)dataLen);
      aborted = true;
      break;
    }
  }

  // Drain remaining ring (bounded — a dead ISR must not freeze the loop)
  unsigned long drainStart = millis();
  while (((uint16_t)(ttsHead - ttsTail)) > 0) {
    if (millis() - drainStart > 5000) { LOGW("TTS", "drain timeout"); break; }
    yield();
    delay(1);
  }
  ttsPlaying = false;
  gptimer_stop(ttsTimer);   // restarted on next playback; no idle ISR preempting WiFi
  // timer left stopped — the ISR is a no-op when !ttsPlaying, and restarting it
  // on later playbacks would need extra bookkeeping for no benefit
  ledcDetach(SPK_PIN);   // safe even if never attached (SW-mode fallback)
  pinMode(SPK_PIN, OUTPUT);
  digitalWrite(SPK_PIN, LOW);
  LOGI("TTS", "%s (%u of %lu bytes, isr ticks: %u, mode: %u)", aborted ? "FAILED" : "done",
       (unsigned)got, (unsigned long)dataLen, (unsigned)ttsIsrTicks, (unsigned)ttsPlayMode);
  return !aborted;
}

// TTS worker task: waits for a job notification, then does the network fetch,
// streaming and playback (the blocking body of the old inline speakText) inside
// this task. The main loop only hands over the text and returns immediately —
// buttons, display and MQTT keep running while Yuki talks. ttsPlaying is the
// shared guard (set at job start, cleared at the end) exactly as before.
static void ttsWorker(void *arg) {
  (void)arg;
  for (;;) {
    // Poll: a job arrives as a task notification, but we also wake periodically
    // to honor a pending voice-list refresh. Jobs are handled first (the notify
    // is only given with ttsJobPending=true, so nothing is lost).
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(200));
    if (ttsJobPending) {
      ttsJobPending = false;
      ttsPlaying = true;   // lock out new jobs (queue/dispatch guards) for the whole job
      // JSON-escape the job text into the request body
      char body[512];
      int bi = 0;
      const char* pre = "{\"text\":\"";
      for (int i = 0; pre[i] && bi < (int)sizeof(body) - 4; i++) body[bi++] = pre[i];
      for (int i = 0; ttsJobText[i] && bi < (int)sizeof(body) - 4; i++) {
        char c = ttsJobText[i];
        if (c == '"' || c == '\\') { body[bi++] = '\\'; body[bi++] = c; }
        else if (c == '\n' || c == '\r' || c == '\t') body[bi++] = ' ';
        else body[bi++] = c;
      }
      body[bi++] = '"';
      if (sys.voiceName[0]) {        // "voice":"en-US-..." (relay falls back if unknown)
        const char* vk = ",\"voice\":\"";
        for (int i = 0; vk[i] && bi < (int)sizeof(body) - 2; i++) body[bi++] = vk[i];
        for (int i = 0; sys.voiceName[i] && bi < (int)sizeof(body) - 2; i++) body[bi++] = sys.voiceName[i];
        body[bi++] = '"';
      }
      {                              // "rate":"+10%" / "-5%"
        const char* rk = ",\"rate\":\"";
        for (int i = 0; rk[i] && bi < (int)sizeof(body) - 8; i++) body[bi++] = rk[i];
        body[bi++] = sys.voiceRate >= 0 ? '+' : '-';
        int ar = abs(sys.voiceRate);
        if (ar >= 10) body[bi++] = (char)('0' + ar / 10);
        body[bi++] = (char)('0' + ar % 10);
        body[bi++] = '%'; body[bi++] = '"';
      }
      {                              // "pitch":"+0Hz" / "-20Hz"
        const char* pk = ",\"pitch\":\"";
        for (int i = 0; pk[i] && bi < (int)sizeof(body) - 10; i++) body[bi++] = pk[i];
        body[bi++] = sys.voicePitch >= 0 ? '+' : '-';
        int ap = abs(sys.voicePitch);
        if (ap >= 100) body[bi++] = (char)('0' + ap / 100);
        if (ap >= 10) body[bi++] = (char)('0' + (ap / 10) % 10);
        body[bi++] = (char)('0' + ap % 10);
        body[bi++] = 'H'; body[bi++] = 'z'; body[bi++] = '"';
      }
      body[bi++] = '}'; body[bi] = '\0';

      for (int attempt = 0; attempt < 3; attempt++) {
        if (attempt > 0) delay(400);   // brief gap before the fresh retry connection
        if (ESP.getFreeHeap() < 40000) { LOGW("TTS", "low heap, skipping"); break; }
        if (ttsStreamPlay(body, bi)) break;
        LOGW("TTS", "stream attempt %d failed", attempt);
      }
      ttsPlaying = false;
      static bool stackLogged = false;
      if (!stackLogged) {
        stackLogged = true;
        LOGI("TTS", "worker stack high water: %u", (unsigned)uxTaskGetStackHighWaterMark(NULL));
      }
    } else if (ttsVoiceRefresh) {    // idle: refresh the selectable voice list
      ttsVoiceRefresh = false;
      ttsFetchVoiceList();
    }
  }
}

// Non-blocking: clean the text, hand the job to the worker task, return. The
// actual fetch + playback happen in the task; callers never wait on it.
bool speakText(const char* text) {
  if (!text || !sys.soundOn || !sys.voiceOn || ttsPlaying) return false;
  if (WiFi.status() != WL_CONNECTED || lowVoltageMode) return false;

  // Cap speech length at a sentence boundary near 180 chars
  char clean[TTS_MAX_TEXT];
  strncpy(clean, text, sizeof(clean) - 1);
  clean[sizeof(clean) - 1] = '\0';
  if (strlen(clean) > 180) {
    char* cut = clean + 180;
    char* p = cut;
    while (p > clean && *p != '.' && *p != '!' && *p != '?' && *p != ',') p--;
    if (p > clean + 60) *p = '\0'; else clean[180] = '\0';
  }
  strncpy(ttsLastText, clean, sizeof(ttsLastText) - 1);
  ttsLastText[sizeof(ttsLastText) - 1] = '\0';   // keep for menu Replay

  if (!ttsTask) {   // created lazily on first speak; lives for the session
    if (xTaskCreate(ttsWorker, "ttsWorker", TTS_TASK_STACK, NULL, TTS_TASK_PRIO, &ttsTask) != pdPASS) {
      LOGW("TTS", "worker create failed");
      return false;
    }
  }
  strncpy(ttsJobText, clean, sizeof(ttsJobText) - 1);
  ttsJobText[sizeof(ttsJobText) - 1] = '\0';
  ttsJobPending = true;
  LOGI("TTS", "speak: '%s' (rssi: %d)", clean, (int)WiFi.RSSI());
  xTaskNotifyGive(ttsTask);
  return true;
}

// --- Test-bench accessors (called from the TTS Test menu) ---
inline uint32_t ttsGetPulse()   { return ttsKnobPulseUs; }
inline void     ttsSetPulse(uint32_t v)   { ttsKnobPulseUs = v; }
inline int      ttsGetPolarity()          { return ttsKnobPolarity; }
inline void     ttsSetPolarity(int v)     { ttsKnobPolarity = v; }
inline int      ttsGetDeadband()          { return ttsKnobDeadband; }
inline void     ttsSetDeadband(int v)     { ttsKnobDeadband = v; }
inline int      ttsGetSpread()            { return ttsKnobSpread; }
inline void     ttsSetSpread(int v)       { ttsKnobSpread = v; }
inline int      ttsGetVolGain()           { return ttsKnobVolGain; }
inline void     ttsSetVolGain(int v)      { ttsKnobVolGain = v; }
inline bool     ttsGetMonitor()           { return ttsMonitor; }
inline void     ttsSetMonitor(bool v)     { ttsMonitor = v; }
inline int         ttsGetVoiceCount()     { return ttsVoiceCount; }
inline const char* ttsGetVoiceName(int i) { return (i >= 0 && i < ttsVoiceCount) ? ttsVoiceList[i] : ""; }
inline const char* ttsGetVoice()          { return sys.voiceName; }
inline void        ttsSetVoice(const char* n) {
  strncpy(sys.voiceName, n, sizeof(sys.voiceName) - 1);
  sys.voiceName[sizeof(sys.voiceName) - 1] = '\0';
}
inline void        ttsRequestVoiceRefresh() { ttsVoiceRefresh = true; }
inline int         ttsGetRate()            { return sys.voiceRate; }
inline void        ttsSetRate(int v)       { sys.voiceRate = v; }
inline int         ttsGetPitch()           { return sys.voicePitch; }
inline void        ttsSetPitch(int v)      { sys.voicePitch = v; }

// Re-speak the last sentence with the current knob set (identical audio A/B)
inline void ttsReplaySpeak() {
  if (ttsLastText[0] && sys.voiceOn && !ttsPlaying) speakText(ttsLastText);
}
