#!/usr/bin/env python3
"""
deepgram_stt.py — sound on stdin → Deepgram live transcription → JSON stdout.

The app opens the microphone and writes it here: 16 kHz, mono, 16-bit PCM.
Same JSON lines as whisper_stt.py, so SpeechService reads either:
    {"status": "calibrating"}   — connecting
    {"status": "listening"}     — ready for sound
    {"text": "hello world"}     — finalized transcription
    {"error": "..."}            — recoverable error (keeps listening)
    {"fatal": "..."}            — unrecoverable error (process exits)

Environment variables (injected by DeepgramSttProvider in C++):
    DEEPGRAM_API_KEY       — required
    FINTERM_STT_MODEL      — default "nova-3"
    FINTERM_STT_LANGUAGE   — default "en"
    FINTERM_STT_KEYTERMS   — comma-separated boosted terms (optional)

The process runs until stdin is closed or the connection ends. Deepgram's
WebSocket is plain enough to need no SDK (its SDK changes its API every major
version, and opens the microphone with PyAudio).
"""

import json
import os
import sys
import threading
from urllib.parse import urlencode

URL = os.environ.get("FINTERM_DEEPGRAM_URL", "wss://api.deepgram.com/v1/listen")  # (tests point it at their own)
CHUNK = 3200  # 100 ms
KEEPALIVE_S = 5.0  # Deepgram closes a connection it hears nothing on for 10 s


def emit(obj: dict) -> None:
    """Write a JSON line to stdout and flush immediately."""
    sys.stdout.write(json.dumps(obj) + "\n")
    sys.stdout.flush()


def listen_url(model: str, language: str, keyterms: list[str]) -> str:
    query = [("model", model), ("language", language), ("encoding", "linear16"), ("sample_rate", "16000"),
             ("channels", "1"), ("punctuate", "true"), ("smart_format", "true"), ("interim_results", "false"),
             ("endpointing", "300")]
    # Nova-3 uses `keyterm` (no weights); older models `keywords`.
    query += [("keyterm" if model.startswith("nova-3") else "keywords", k) for k in keyterms]
    return f"{URL}?{urlencode(query)}"


def transcript(message: object) -> str:
    """The text of a finalized result, else "". Interim ones are not asked for;
    a long utterance comes as several final pieces, each to be passed on."""
    if not isinstance(message, dict) or message.get("type") != "Results" or not message.get("is_final"):
        return ""
    alternatives = (message.get("channel") or {}).get("alternatives") or [{}]
    return (alternatives[0].get("transcript") or "").strip()


def main() -> None:
    api_key = os.environ.get("DEEPGRAM_API_KEY", "").strip()
    if not api_key:
        emit({"fatal": "DEEPGRAM_API_KEY not set — configure it in Settings → Voice"})
        sys.exit(1)
    model = os.environ.get("FINTERM_STT_MODEL", "nova-3").strip() or "nova-3"
    language = os.environ.get("FINTERM_STT_LANGUAGE", "en").strip() or "en"
    keyterms = [t.strip() for t in os.environ.get("FINTERM_STT_KEYTERMS", "").split(",") if t.strip()]

    try:
        from websockets.exceptions import ConnectionClosed, InvalidStatus
        from websockets.sync.client import connect
    except ImportError as e:
        emit({"fatal": f"Voice input's packages are not installed ({e}): restart finterm to let it finish its Python setup"})
        sys.exit(1)

    emit({"status": "calibrating"})
    try:
        ws = connect(listen_url(model, language, keyterms), additional_headers={"Authorization": f"Token {api_key}"})
    except InvalidStatus as e:
        why = {401: "the API key was refused", 402: "the account has no credit left",
               400: "the model, language or key terms were refused"}.get(e.response.status_code, "")
        emit({"fatal": f"Deepgram: HTTP {e.response.status_code}" + (f" — {why}" if why else "")})
        sys.exit(1)
    except Exception as e:  # noqa: BLE001 — DNS, TLS, timeout
        emit({"fatal": f"Could not reach Deepgram: {e}"})
        sys.exit(1)
    emit({"status": "listening"})

    ended = threading.Event()

    def send_sound() -> None:
        while not ended.is_set():
            data = sys.stdin.buffer.read1(CHUNK)
            try:
                if not data:  # the app closed the microphone
                    ws.send(json.dumps({"type": "CloseStream"}))
                    return
                ws.send(data)
            except ConnectionClosed:
                return

    def keep_alive() -> None:
        while not ended.wait(KEEPALIVE_S):
            try:
                ws.send(json.dumps({"type": "KeepAlive"}))
            except ConnectionClosed:
                return

    for f in (send_sound, keep_alive):
        threading.Thread(target=f, daemon=True).start()

    try:
        for raw in ws:
            try:
                message = json.loads(raw)
            except ValueError:
                continue
            text = transcript(message)
            if text:
                emit({"text": text})
            elif isinstance(message, dict) and message.get("type") == "Error":
                emit({"error": f"Deepgram: {message.get('description') or message.get('message')}"})
    except ConnectionClosed as e:
        emit({"fatal": f"Deepgram ended the connection: {e.rcvd.reason if e.rcvd else e}"})
        sys.exit(1)
    finally:
        ended.set()
    emit({"status": "stopped"})


if __name__ == "__main__":
    main()
