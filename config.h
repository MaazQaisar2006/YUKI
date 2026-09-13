// config.h
#pragma once

#include "secrets.h"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// --- HARDWARE PINS ---
// ESP32-C3 Super Mini pin assignments
// I2C: SDA=GPIO21, SCL=GPIO1
const int BTN_UP = 9, BTN_DN = 7, BTN_SEL = 10, BTN_BACK = 6, SPK_PIN = 20, BTN_MODIFIER = 2;

// --- SYSTEM STATES ---
enum Mode { 
  FACE, MENU, SCAN_LIST, KEYBOARD, SYNCING, CONNECTING, CUSTOM_PROMPT, 
  VIEW_SCANS, VIEW_MSGS, SELECT_RECIPIENT, CUSTOM_MESSAGE, SYSTEM_INFO, 
  THINKING, CONFIRM_WIPE_SCANS, CONFIRM_WIPE_MSGS, GAME_GUESS_NUMBER, 
  GAME_RPS, GAME_SELECT, GAME_RPG, CONFIRM_SAVE_RPG, TERMINAL, 
  TERMINAL_INPUT, TERMINAL_OUTPUT, TERMINAL_FILE_LIST, VIEW_EMOTIONS, 
  EMOTION_DISPLAY, TIMEZONE_SELECT, QUICK_RESPONSE, MEMORY_REBOOT, SELECTED_WIPE, SOUND_MENU, TTS_TEST_MENU, WIFI_CONFIG, MQTT_CONFIG, SHUTDOWN_CONFIRM
};
enum Emotion { NEUTRAL, HAPPY, SURPRISED, SAD, ANGRY, THINKING_FACE, SLEEPY, FLUSTERED, LAUGHING, WINK, CONFUSED, LOVE, SASSY, SHOCKED, SAD_EMBARRASSED, SHY, TEASING, BLUSHING };
enum PersonalityMode { PM_CHATTY, PM_QUIET, PM_SASSY, PM_REFLECTIVE };

struct Config {
  char ssid[33] = ""; char pass[64] = "";
  bool soundOn = true; int xp = 0, level = 1;
  bool autoThink = true; 
  float latitude = 33.68; float longitude = 73.04; // Default location
  int scrollSpeed = 1; // 0=Slow, 1=Normal, 2=Fast
  long timeZoneOffset = 18000; // Default UTC+5
  int rpgHP = 100; // Player Health
  int rpgGold = 0; // Player Gold
  int affinity = 0; // Hidden relationship score (-100 to 100)
  int guessHighScore = 999; // Best (fewest) tries for Guess Number
  char phoneIP[16] = ""; // Last set phone IP (persisted)
  uint8_t soundVolume = 50; // 0=mute 100=max
  bool ambientSounds = true;
  bool bootSound = true;
  bool soundScents = true;
  // --- Voice config (TTS test-bench settings, now persistent) ---
  bool voiceOn = true;        // voice mute (independent of master soundOn)
  bool beepsOn = true;        // SFX/emotion/ambient beep mute (independent of master soundOn)
  bool screenSaver = true;    // OLED blanks after 5 min idle (SOUND CFG menu toggle)
  char innerThread[160] = ""; // her last spoken self-talk line (survives reboot)
  uint8_t voicePulse = 55;    // max pulse width in µs (55/45/35/25)
  int voicePolarity = 0;      // 0 NORM active-low idle HIGH / 1 INV active-high idle LOW
  int voiceDeadband = 0;      // silence gate around sample 128 (0/4/8/16/32)
  int voiceSpread = 1;        // 0 lin / 1 boost 1.5x / 2 boost 2x / 3 clip
  uint8_t voiceGain = 125;    // playback volume multiplier % (50..150)
  char voiceName[32] = "en-US-AriaNeural"; // TTS voice (relay selectable list)
  int voiceRate = 10;         // Edge TTS rate % (range +/-50)
  int voicePitch = 0;         // Edge TTS pitch Hz (range +/-50)
  // --- MQTT config (overrides secrets.h if non-empty) ---
  char mqttServer[64] = "";   // MQTT broker hostname
  int mqttPort = 0;           // MQTT broker port (0 = use secrets.h)
  char deviceId[32] = "";     // Device ID (MQTT client ID)
  char characterName[32] = ""; // Character name
  char phoneContactId[32] = ""; // Phone contact ID for DMs
  char phoneIP_mqtt[16] = ""; // Phone IP for presence (overrides phoneIP if set)
  char ttsRelayUrl[64] = "";  // TTS relay URL
  int txPower = 20;            // WiFi TX power in dBm (8-22, default 20)
  unsigned long lastConversationTime = 0; // Epoch of last meaningful interaction (multi-day awareness)
  unsigned long lastQuoteRefresh = 0;    // Epoch of last quote fetch from API
};

struct Message {
  char content[128]; // Max message length
};

// --- MQTT CONFIG ---
const char* CHARACTER_NAME = "Yuki"; // Character name
const char* PHONE_CONTACT_ID = "maaz-phone"; // The permanent contact ID for your phone

const int MAX_CONTACTS = 1;
const int MAX_USERS = 1;
const int MAX_MESSAGES = 8;

// --- CONSTANTS ---
static const char chars[] PROGMEM = " abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%^&*(){}[].,<>?/~:\";'|+-=_;"; // Already PROGMEM
const unsigned long emotionDuration = 10000;
const unsigned long sleepTimeout = 90 * 1000;
const unsigned long autoWakeupDuration = 3 * 60 * 1000;
const unsigned long debounceDelay = 200;
const unsigned long SOUND_BOOT_DELAY = 8000; // 8s before sounds work after boot

// Runtime diagnostics
#define ENABLE_HEAP_LOG 1
#define HEAP_WARN_THRESHOLD 40000
#define HEAP_LOG_INTERVAL 60000

bool canAllocJson(size_t sz);

// --- PRESENCE DETECTION ---
#define PING_INTERVAL 120000      // Ping phone every 2 minutes
#define PRESENCE_TIMEOUT 360000   // 3 missed pings = away (360s)

// --- TTS RELAY ---
#define TTS_RELAY_URL ""          // Default TTS relay URL (set in MQTT config menu)
