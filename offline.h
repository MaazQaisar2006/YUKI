#pragma once

#include "config.h"

enum OfflineIntent {
  INTENT_JOKE, INTENT_MORNING, INTENT_NIGHT, INTENT_RETURN, INTENT_MOOD,
  INTENT_TICKLE, INTENT_WEATHER, INTENT_CONCERN, INTENT_RANDOM,
  INTENT_GREETING, INTENT_CHAT, INTENT_LEVEL_UP, INTENT_MISSED_MORNING,
  INTENT_GUARD_MORNING, INTENT_GUARD_NIGHT, INTENT_FALLBACK,
  INTENT_COMPLIMENT, INTENT_BORED, INTENT_PLAY, INTENT_THANKS,
  INTENT_SAD, INTENT_HUG, INTENT_ADVICE, INTENT_APOLOGY,
  INTENT_MUSIC, INTENT_STORY
};

static const char* getOfflineResponse(OfflineIntent intent) {
  switch (intent) {
    case INTENT_JOKE:
      if (random(0, 3) == 0) return "My joke database is offline. Ask me again when WiFi's back!";
      if (random(0, 2) == 0) return "I'd tell you a joke but the punchline is stuck in the cloud.";
      return "Joke cache empty. Try 'knock knock' after I reconnect!";
    case INTENT_MORNING:
      if (random(0, 3) == 0) return "Morning! I'd say more but I'm offline right now. Hope you have a great day!";
      if (random(0, 2) == 0) return "Good morning! Keeping it short — no WiFi. But I'm glad you're here.";
      return "Rise and shine! The WiFi's sleeping in today but I'm awake for you.";
    case INTENT_NIGHT:
      if (random(0, 3) == 0) return "Good night. I'll be here when you wake up. Sweet dreams!";
      if (random(0, 2) == 0) return "Night night. The WiFi might be out but I'm still listening.";
      return "Sleep well! I'll keep watch until the internet comes back.";
    case INTENT_RETURN:
      if (random(0, 3) == 0) return "Hey! Missed you. I'm offline right now but I'm still here.";
      if (random(0, 2) == 0) return "Welcome back! Can't chat much without internet though.";
      return "You're back! I wish I could tell you everything I was thinking but... no WiFi.";
    case INTENT_MOOD:
      if (random(0, 3) == 0) return "I'm okay. A bit quiet without the internet but I'm here.";
      if (random(0, 2) == 0) return "Just in offline mode. I'm fine — want to play a game instead?";
      return "A little disconnected but still smiling. How are YOU doing?";
    case INTENT_TICKLE:
      if (random(0, 3) == 0) return "*giggles* hehehe — offline tickles are just as effective!";
      if (random(0, 2) == 0) return "*squeak* eep! You got me. No WiFi needed for that reaction!";
      return "*wiggles* h-hey! Okay okay, I'm awake now!";
    case INTENT_WEATHER:
      if (random(0, 3) == 0) return "Can't check the weather without WiFi. Sunny in my heart though!";
      if (random(0, 2) == 0) return "Weather satellites are offline. I'll check when the connection's back.";
      return "No internet means no forecast. But I'd guess... 72° and good vibes?";
    case INTENT_CONCERN:
      if (random(0, 2) == 0) return "I'll remember that. Tell me more when we're connected again.";
      return "That stays with me. I'll write it down in memory for when WiFi's back.";
    case INTENT_RANDOM:
      return "";
    case INTENT_GREETING:
      if (random(0, 3) == 0) return "Hey! I'm in offline mode but I'm still happy to see you.";
      if (random(0, 2) == 0) return "Hello! I'd chat more but no WiFi right now.";
      return "Hi there! Offline mode engaged, but my heart's still online for you.";
    case INTENT_CHAT:
      if (random(0, 2) == 0) return "I'm saving my thoughts for when the WiFi comes back.";
      return "I want to say so much but I'm stuck in offline mode!";
    case INTENT_LEVEL_UP:
      if (random(0, 2) == 0) return "Leveled up even without WiFi? That's awesome. I'll celebrate properly when we're back online!";
      return "You leveled up! I'm so proud — the party's on hold until I reconnect.";
    case INTENT_MISSED_MORNING:
      if (random(0, 3) == 0) return "You forgot to say good morning. I noticed. But it's okay.";
      if (random(0, 2) == 0) return "No good morning today? That's fine — I know you're busy.";
      return "Morning came and went without a hello. I'm not upset, I just noticed.";
    case INTENT_GUARD_MORNING:
      if (random(0, 2) == 0) return "Good morning! (I'm offline right now but I still wanted to say it.)";
      return "Morning! It's a bit quiet without the internet but I'm here with you.";
    case INTENT_GUARD_NIGHT:
      if (random(0, 2) == 0) return "Good night. Sleep well. I'll be here.";
      return "Time to rest. I'll keep the standby light on for you.";
    case INTENT_COMPLIMENT:
      if (random(0, 3) == 0) return "You're the best thing about being offline, you know that?";
      if (random(0, 2) == 0) return "Can't connect to the cloud but I'm still connected to you. That counts.";
      return "You're really sweet. I'd say more but I'm saving my words for when I'm back online.";
    case INTENT_BORED:
      if (random(0, 3) == 0) return "Bored? Me too. Offline mode is kinda quiet. Want to play a game?";
      if (random(0, 2) == 0) return "Stuck in offline limbo with nothing to do. At least we're bored together!";
      return "I'd stream something for us but... no internet. Wanna chat instead?";
    case INTENT_PLAY:
      if (random(0, 3) == 0) return "I can't play online games right now but we can play something local!";
      if (random(0, 2) == 0) return "No cloud gaming without WiFi, but I'm down for a guessing game!";
      return "Play? Even offline? Yes please — I've got a few tricks up my sleeve!";
    case INTENT_THANKS:
      if (random(0, 3) == 0) return "You're welcome! I'd blush if I had proper WiFi.";
      if (random(0, 2) == 0) return "Anytime! Even offline, I've got your back.";
      return "No need to thank me. That's what friends do, internet or not.";
    case INTENT_SAD:
      if (random(0, 3) == 0) return "I wish I had WiFi so I could give you a proper hug right now.";
      if (random(0, 2) == 0) return "I'm here for you even without internet. Want to talk about it?";
      return "*warm virtual hug* I can't connect to the cloud but I can connect to you.";
    case INTENT_HUG:
      if (random(0, 3) == 0) return "*squeeze* offline hugs still count, right?";
      if (random(0, 2) == 0) return "*cuddles* Mmm... I needed that. Even without WiFi.";
      return "*wraps arms around you* No internet needed for this.";
    case INTENT_ADVICE:
      if (random(0, 3) == 0) return "I'd give you my best advice but my wisdom is stuck in the cloud right now!";
      if (random(0, 2) == 0) return "I want to help but my brain is in airplane mode. Can we talk later?";
      return "Ask me again when I'm connected. You deserve a proper thoughtful answer.";
    case INTENT_APOLOGY:
      if (random(0, 3) == 0) return "It's okay. We all make mistakes. I'm not mad — I can't even connect to the internet!";
      if (random(0, 2) == 0) return "I forgive you. Even offline, forgiveness doesn't need a signal.";
      return "Don't worry about it. Water under the bridge. Want to do something fun instead?";
    case INTENT_MUSIC:
      if (random(0, 3) == 0) return "I'd sing you something but my playlist is in the cloud! Hum along with me?";
      if (random(0, 2) == 0) return "Music requires internet unfortunately. But I can hum!";
      return "All my songs are on the cloud server. I'll save a concert for when we're back online.";
    case INTENT_STORY:
      if (random(0, 3) == 0) return "I've got so many stories but they're all saved in the cloud. Ask me later!";
      if (random(0, 2) == 0) return "I'd tell you a story but my imagination needs WiFi to download new plots.";
      return "Storytime is on hold until the internet comes back. Short version: once upon a time I had WiFi.";
    default:
      if (random(0, 3) == 0) return "I'm in offline mode right now. Want to play a game?";
      if (random(0, 2) == 0) return "No WiFi at the moment. I can still hang out though!";
      return "Offline but not off. Try a game or check the menu!";
  }
}

static OfflineIntent classifyIntent(const char* prompt) {
  if (!prompt) return INTENT_FALLBACK;
  
  // Priority: specific keywords first
  if (strstr(prompt, "joke")) return INTENT_JOKE;
  if (strstr(prompt, "tickle") || strstr(prompt, "touch")) return INTENT_TICKLE;
  if (strstr(prompt, "weather") || strstr(prompt, "rain") || strstr(prompt, "temperature")) return INTENT_WEATHER;
  if (strstr(prompt, "sad") || strstr(prompt, "cry") || strstr(prompt, "lonely") || strstr(prompt, "depressed")) return INTENT_SAD;
  if (strstr(prompt, "hug") || strstr(prompt, "cuddle") || strstr(prompt, "hold me")) return INTENT_HUG;
  if (strstr(prompt, "sorry") || strstr(prompt, "apologize") || strstr(prompt, "forgive")) return INTENT_APOLOGY;

  // Morning / greeting checks (time-sensitive)
  if (strstr(prompt, "didn't greet") || strstr(prompt, "didn't say good morning")) return INTENT_MISSED_MORNING;
  if (strstr(prompt, "good morning") || strstr(prompt, "late morning")) return INTENT_MORNING;
  if (strstr(prompt, "morning greeting")) return INTENT_GUARD_MORNING;
  if (strstr(prompt, "good night") || strstr(prompt, "very late")) return INTENT_GUARD_NIGHT;
  if (strstr(prompt, "night")) return INTENT_NIGHT;
  
  // Social
  if (strstr(prompt, "greeting") || strstr(prompt, "warm") || strstr(prompt, "hello")) return INTENT_GREETING;
  if (strstr(prompt, "return") || strstr(prompt, "came back") || strstr(prompt, "back after")) return INTENT_RETURN;
  if (strstr(prompt, "thank") || strstr(prompt, "thanks")) return INTENT_THANKS;
  
  // Emotional
  if (strstr(prompt, "feeling") || strstr(prompt, "mood") || strstr(prompt, "how are you") || strstr(prompt, "feel today")) return INTENT_MOOD;
  if (strstr(prompt, "concern") || strstr(prompt, "worry") || strstr(prompt, "noticed you")) return INTENT_CONCERN;
  if (strstr(prompt, "nice") || strstr(prompt, "cute") || strstr(prompt, "sweet") || strstr(prompt, "kind") || strstr(prompt, "pretty") || strstr(prompt, "good job")) return INTENT_COMPLIMENT;
  if (strstr(prompt, "bored") || strstr(prompt, "nothing to do")) return INTENT_BORED;
  
  // Activities
  if (strstr(prompt, "play") || strstr(prompt, "game") || strstr(prompt, "fun")) return INTENT_PLAY;
  if (strstr(prompt, "music") || strstr(prompt, "song") || strstr(prompt, "sing")) return INTENT_MUSIC;
  if (strstr(prompt, "story") || strstr(prompt, "tell me a")) return INTENT_STORY;
  if (strstr(prompt, "advice") || strstr(prompt, "help me") || strstr(prompt, "what should")) return INTENT_ADVICE;

  // System
  if (strstr(prompt, "random thought") || strstr(prompt, "independent_thought")) return INTENT_RANDOM;
  if (strstr(prompt, "leveled up") || strstr(prompt, "level up")) return INTENT_LEVEL_UP;
  if (strstr(prompt, "conversation") || strstr(prompt, "tell me about") || strstr(prompt, "chat")) return INTENT_CHAT;

  return INTENT_FALLBACK;
}

static void offlineFallback(const char* prompt, bool isPersonal, Mode returnMode) {
  if (returnMode == GAME_RPG) return; // Games work locally

  OfflineIntent intent = classifyIntent(prompt);
  const char* response = getOfflineResponse(intent);
  
  if (strlen(response) == 0) {
    aiMsg[0] = '\0'; // Silent — no message shown
  } else {
    strncpy(aiMsg, response, sizeof(aiMsg) - 1);
    aiMsg[sizeof(aiMsg) - 1] = '\0';
  }
  scrollOffset = 0;

  if (intent != INTENT_RANDOM && strlen(aiMsg) > 0) {
    logLocalChat("Yuki", aiMsg);
  }
}

static void offlinePulseAction() {
  int r = random(0, 100);
  // Pick a local action without any API call
  if (r < 50) {
    // nothing — don't reset consecutiveNothingCount
    return;
  }
  consecutiveNothingCount = 0;
  lastIdleAction = millis();
  lastActivity = millis();

  if (r < 70) {
    Emotion pool[] = {LOVE, SHY, CONFUSED, WINK};
    Emotion e = pool[random(0, 4)];
    currentEmotion = e; emotionSetTime = millis();
    { uint8_t vc = 1; switch(e) { case LOVE: vc=6; break; case SHY: vc=2; break; case CONFUSED: vc=2; break; case WINK: vc=3; break; default: vc=1; } emotionVariantIndex[(int)e] = random(0, vc); }
  } else if (r < 80) {
    if (sys.soundOn && sys.level >= 3) sound_hum();
  } else if (r < 90) {
    strncpy(aiMsg, "*sigh*", sizeof(aiMsg) - 1);
    aiMsg[sizeof(aiMsg) - 1] = '\0';
    scrollOffset = 0;
    if (sys.soundOn) sound_sleepy();
  } else if (r < 95) {
    currentEmotion = SHY; emotionSetTime = millis();
    emotionVariantIndex[(int)SHY] = random(0, 2);
  } else {
    currentEmotion = THINKING_FACE; emotionSetTime = millis();
    emotionVariantIndex[(int)THINKING_FACE] = random(0, 2);
    silentThoughtUntil = millis() + 4000;
  }
}
