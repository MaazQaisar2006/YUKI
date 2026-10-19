#!/usr/bin/env python3
"""
Yuki TTS relay — Edge TTS voice for the ESP32-C3.
POST text -> WAV 16kHz mono 16-bit streamed back.

Usage:
    pip install edge-tts flask
    (install ffmpeg and add to PATH)
    python3 tts_relay.py

Endpoints:
    POST /tts   {"text": "..."}  -> audio/wav
    GET  /health                 -> "ok" (includes chosen voice)
    GET  /voices                 -> list of available en voices
    GET  /voices/selectable      -> voices proven synthesizable (ESP menu list)
"""
import asyncio
import io
import json
import logging
import struct
import subprocess

import edge_tts
from flask import Flask, Response, request

# Preference order — the first voice that Edge actually offers wins.
# Aria is the most stable classic voice; Emma is a newer "multilingual" voice.
VOICE_PREFERENCES = [
    "en-US-AriaNeural",
    "en-US-EmmaNeural",
    "en-US-JennyNeural",
    "en-US-GuyNeural",
    "en-GB-SoniaNeural",
    "en-GB-LibbyNeural",
    "en-AU-NatashaNeural",
    "en-CA-ClaraNeural",
    "en-IE-EmilyNeural",
]
RATE = "+10%"      # slightly faster = livelier
PITCH = "+0Hz"
MAX_TEXT_LEN = 500
PROBE_TEXT = "Voice check, hello from Yuki."

VOICE = None
SELECTABLE_VOICES = []   # voices proven synthesizable (menu list for the ESP)

app = Flask(__name__)
logging.basicConfig(level=logging.INFO)

# Simple cache: repeated texts skip synthesis + conversion
_cache = {}
CACHE_MAX = 200


async def _synthesize(text: str, voice: str, rate: str = RATE, pitch: str = PITCH) -> bytes:
    tts = edge_tts.Communicate(text, voice, rate=rate, pitch=pitch)
    buf = io.BytesIO()
    async for chunk in tts.stream():
        if chunk["type"] == "audio":
            buf.write(chunk["data"])
    return buf.getvalue()


async def _pick_voice() -> str:
    available = {v["ShortName"] for v in await edge_tts.list_voices()}
    candidates = [v for v in VOICE_PREFERENCES if v in available]
    if not candidates:  # nothing preferred exists — take any English voice
        candidates = sorted(
            v["ShortName"]
            for v in await edge_tts.list_voices()
            if v["Locale"].startswith("en-")
        )
    if not candidates:
        raise RuntimeError("no English voices offered by Edge TTS")

    # Probe-synthesize so a voice that is listed but not synthesizable is skipped
    for voice in candidates:
        try:
            mp3 = await _synthesize(PROBE_TEXT, voice)
            if mp3:
                return voice
        except Exception:
            logging.warning("voice %s failed probe, trying next", voice)
    raise RuntimeError("all voices failed probe synthesis")


def _to_wav(pcm: bytes) -> bytes:
    # ffmpeg streams raw PCM (u8); we wrap it in a proper WAV header.
    # 8-bit u8 halves the payload the ESP32 must pull over its (weak) link, and
    # the ESP plays the high byte of s16le anyway — identical audible quality.
    header = struct.pack(
        "<4sI4s4sIHHIIHH4sI",
        b"RIFF", 36 + len(pcm), b"WAVE",
        b"fmt ", 16, 1, 1, 16000, 16000, 1, 8,
        b"data", len(pcm),
    )
    return header + pcm


def _synthesize_to_wav(text: str, voice: str, rate: str = RATE, pitch: str = PITCH) -> bytes:
    proc = subprocess.run(
        [
            "ffmpeg", "-hide_banner", "-loglevel", "error",
            "-i", "pipe:0",
            "-ar", "16000", "-ac", "1", "-c:a", "pcm_u8",
            "-f", "u8", "pipe:1",
        ],
        input=asyncio.run(_synthesize(text, voice, rate, pitch)),
        capture_output=True, timeout=30,
    )
    if proc.returncode != 0 or not proc.stdout:
        raise RuntimeError(proc.stderr.decode("utf-8", "ignore")[:300])
    return _to_wav(proc.stdout)


def _valid_rate(rate) -> bool:
    return (isinstance(rate, str) and len(rate) <= 8 and rate[:1] in "+-"
            and rate.endswith("%") and rate[1:-1].isdigit())


def _valid_pitch(pitch) -> bool:
    return (isinstance(pitch, str) and len(pitch) <= 9 and pitch[:1] in "+-"
            and pitch.endswith("Hz") and pitch[1:-2].isdigit())


async def _probe_voice(voice: str) -> bool:
    try:
        return bool(await _synthesize(PROBE_TEXT, voice))
    except Exception as e:
        logging.warning("voice %s failed probe: %s", voice, e)
        return False


async def _probe_all(candidates: list) -> list:
    # Gather is built INSIDE the running loop — constructing it in the sync
    # main thread would call asyncio.get_event_loop() there (no current loop
    # after previous asyncio.run() calls) and crash.
    return await asyncio.gather(*(_probe_voice(v) for v in candidates))


def _build_selectable() -> list:
    # Curated list, intersected with what Edge actually offers, then
    # probe-synthesized in parallel so unusable voices never reach the ESP menu.
    try:
        available = {v["ShortName"] for v in asyncio.run(edge_tts.list_voices())}
    except Exception as e:
        logging.error("selectable: voice list failed: %s", e)
        return []
    candidates = [v for v in VOICE_PREFERENCES if v in available]
    if not candidates:  # nothing preferred available — take any English voice
        candidates = sorted(v for v in available if v.startswith("en-"))[:12]
    results = asyncio.run(_probe_all(candidates))
    return [v for v, ok in zip(candidates, results) if ok]


def _select_voice() -> None:
    global VOICE, SELECTABLE_VOICES
    try:
        VOICE = asyncio.run(_pick_voice())
        logging.info("TTS voice selected: %s", VOICE)
    except Exception as e:
        VOICE = None
        logging.error("voice selection failed: %s", e)
    try:
        SELECTABLE_VOICES = _build_selectable()
        logging.info("selectable voices (%d): %s", len(SELECTABLE_VOICES), SELECTABLE_VOICES)
    except Exception as e:
        logging.error("selectable build failed: %s", e)


def _check_ffmpeg() -> None:
    # Fail fast at startup if ffmpeg is missing/broken — otherwise every /tts
    # request dies with a silent 500 and the ESP only sees the status code.
    try:
        proc = subprocess.run(["ffmpeg", "-version"], capture_output=True, timeout=10)
        if proc.returncode != 0:
            logging.error("ffmpeg check failed (rc=%d): %s", proc.returncode,
                          proc.stderr.decode("utf-8", "ignore")[:200])
        else:
            first = proc.stdout.decode("utf-8", "ignore").splitlines()[0]
            logging.info("ffmpeg ok: %s", first)
    except Exception as e:
        logging.error("ffmpeg check failed: %s", e)


_check_ffmpeg()
_select_voice()


@app.route("/tts", methods=["POST"])
def tts():
    if not VOICE:
        return Response("no voice available: " + (str(VOICE)), status=500)
    data = request.get_json(force=True, silent=True) or {}
    text = (data.get("text") or request.data.decode("utf-8", "ignore")).strip()
    if not text:
        return Response("no text", status=400)
    text = text[:MAX_TEXT_LEN]

    voice = data.get("voice") or VOICE
    rate = data.get("rate") if _valid_rate(data.get("rate")) else RATE
    pitch = data.get("pitch") if _valid_pitch(data.get("pitch")) else PITCH
    if voice not in SELECTABLE_VOICES and voice != VOICE:
        logging.warning("voice %s unavailable, falling back to %s", voice, VOICE)
        voice = VOICE

    key = (voice, rate, pitch, text)
    if key in _cache:
        return Response(_cache[key], mimetype="audio/wav")

    try:
        wav = _synthesize_to_wav(text, voice, rate, pitch)
        if len(_cache) >= CACHE_MAX:
            _cache.clear()
        _cache[key] = wav
        return Response(wav, mimetype="audio/wav")
    except Exception as e:
        logging.error("tts request failed: %s", e)
        return Response(f"tts error: {e}", status=500)


@app.route("/voices/selectable", methods=["GET"])
def voices_selectable():
    return Response(json.dumps(SELECTABLE_VOICES), mimetype="application/json")


@app.route("/health", methods=["GET"])
def health():
    return f"ok voice={VOICE}"


@app.route("/voices", methods=["GET"])
def voices():
    async def _list():
        return await edge_tts.list_voices()

    try:
        v = asyncio.run(_list())
        return Response(json.dumps([x["ShortName"] for x in v if x["Locale"].startswith("en")]),
                        mimetype="application/json")
    except Exception as e:
        return Response(f"error: {e}", status=500)


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=8000, threaded=True)