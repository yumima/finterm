#!/usr/bin/env python3
"""
whisper_stt.py — Microphone → faster-whisper (local) → JSON stdout.

Matches the same JSON-lines protocol as deepgram_stt.py so SpeechService can
parse either transparently:
    {"status": "calibrating"}   — model warming up / VAD calibrating
    {"status": "listening"}     — ready, waiting for speech
    {"text": "hello world"}     — finalized transcription
    {"error": "..."}            — recoverable error (keeps listening)
    {"fatal": "..."}            — unrecoverable error (process exits)

Environment variables (injected by WhisperSttProvider in C++):
    FINCEPT_STT_MODEL       — faster-whisper model id; default "medium"
                              ("tiny" / "base" / "small" / "medium" /
                               "large-v3" / "large-v3-turbo")
    FINCEPT_STT_LANGUAGE    — ISO 639-1 code; default "en"; empty = auto-detect
    FINCEPT_STT_DEVICE      — "cuda" / "cpu" / "auto" (default)
    FINCEPT_STT_COMPUTE     — "int8" / "int8_float16" / "float16" / "float32" /
                              "auto" (default)
    FINCEPT_STT_VAD_AGGR    — webrtcvad aggressiveness 0–3; default 2

First run downloads the model into ~/.cache/huggingface/hub (medium ≈ 1.5 GB,
large-v3 ≈ 3 GB).  Subsequent runs are offline.

The process runs until stdin is closed, a fatal error occurs, or it receives
SIGTERM (Windows: QProcess::kill sends taskkill which terminates anyway).
"""

import json
import os
import signal
import sys
import threading
from collections import deque


def emit(obj: dict) -> None:
    """Write a JSON line to stdout and flush immediately."""
    sys.stdout.write(json.dumps(obj) + "\n")
    sys.stdout.flush()


def _require(import_path: str, install_hint: str):
    """Import a module or emit a fatal install hint and exit."""
    try:
        return __import__(import_path)
    except ImportError as e:  # noqa: BLE001
        emit({"fatal": f"{import_path} not installed — {install_hint} ({e})"})
        sys.exit(1)


def main() -> None:
    # Dependencies — fail fast with clear install hints so the UI can surface them.
    _require("numpy", "run: pip install numpy")
    _require("pyaudio", "run: pip install PyAudio")
    _require("webrtcvad", "run: pip install webrtcvad")

    # faster-whisper is a Python package whose top-level module name has a dash
    # replaced with an underscore — guard with the direct import here.
    try:
        from faster_whisper import WhisperModel
    except ImportError as e:  # noqa: BLE001
        emit({"fatal": f"faster-whisper not installed — run: pip install faster-whisper ({e})"})
        sys.exit(1)

    import numpy as np
    import pyaudio
    import webrtcvad

    model_name = os.environ.get("FINCEPT_STT_MODEL", "medium").strip() or "medium"
    language = os.environ.get("FINCEPT_STT_LANGUAGE", "en").strip() or None
    device = os.environ.get("FINCEPT_STT_DEVICE", "auto").strip() or "auto"
    compute_type = os.environ.get("FINCEPT_STT_COMPUTE", "auto").strip() or "auto"
    try:
        vad_aggr = int(os.environ.get("FINCEPT_STT_VAD_AGGR", "2"))
    except ValueError:
        vad_aggr = 2
    vad_aggr = max(0, min(3, vad_aggr))

    # Graceful shutdown on SIGTERM (Windows lacks it but QProcess::kill works).
    stop_event = threading.Event()

    def on_signal(_signum, _frame):
        stop_event.set()

    if hasattr(signal, "SIGTERM"):
        signal.signal(signal.SIGTERM, on_signal)
    if hasattr(signal, "SIGINT"):
        signal.signal(signal.SIGINT, on_signal)

    emit({"status": "calibrating"})

    # Load Whisper model. First run pulls weights from HuggingFace; subsequent
    # runs hit the local cache. compute_type="auto" picks int8 on CPU, float16
    # on GPU.
    try:
        model = WhisperModel(model_name, device=device, compute_type=compute_type)
    except Exception as e:  # noqa: BLE001
        emit({"fatal": f"failed to load Whisper model '{model_name}': {e}"})
        sys.exit(1)

    # 16 kHz mono int16 PCM, 30 ms frames — the only frame size webrtcvad accepts.
    sample_rate = 16_000
    frame_ms = 30
    frame_samples = sample_rate * frame_ms // 1000  # 480 samples
    frame_bytes = frame_samples * 2  # int16

    pa = pyaudio.PyAudio()
    try:
        stream = pa.open(
            format=pyaudio.paInt16,
            channels=1,
            rate=sample_rate,
            input=True,
            frames_per_buffer=frame_samples,
        )
    except Exception as e:  # noqa: BLE001
        emit({"fatal": f"could not open microphone: {e}"})
        pa.terminate()
        sys.exit(1)

    vad = webrtcvad.Vad(vad_aggr)

    # Speech-segment buffer: collect frames while VAD reports speech, plus a
    # small lead-in so the start of the first word isn't clipped, and finalize
    # the segment once we've seen ~750 ms of silence.
    lead_in = deque(maxlen=10)  # 10 × 30 ms = 300 ms pre-roll
    speech_frames: list[bytes] = []
    silence_frames = 0
    in_speech = False
    silence_finalize_frames = int(750 / frame_ms)  # ~25 frames
    min_speech_frames = int(300 / frame_ms)        # discard < 300 ms blips

    emit({"status": "listening"})

    try:
        while not stop_event.is_set():
            try:
                frame = stream.read(frame_samples, exception_on_overflow=False)
            except OSError as e:
                emit({"error": f"audio read failed: {e}"})
                continue
            if len(frame) < frame_bytes:
                continue

            try:
                is_speech = vad.is_speech(frame, sample_rate)
            except Exception:  # noqa: BLE001
                is_speech = False

            if is_speech:
                if not in_speech:
                    in_speech = True
                    speech_frames = list(lead_in) + [frame]
                else:
                    speech_frames.append(frame)
                silence_frames = 0
            else:
                lead_in.append(frame)
                if in_speech:
                    speech_frames.append(frame)
                    silence_frames += 1
                    if silence_frames >= silence_finalize_frames:
                        if len(speech_frames) >= min_speech_frames:
                            _transcribe_and_emit(
                                speech_frames, model, language, sample_rate, np
                            )
                        in_speech = False
                        speech_frames = []
                        silence_frames = 0
    finally:
        try:
            stream.stop_stream()
            stream.close()
        finally:
            pa.terminate()

    emit({"status": "stopped"})


def _transcribe_and_emit(frames, model, language, sample_rate, np) -> None:
    """Run faster-whisper on a buffered speech segment and emit any text."""
    pcm = b"".join(frames)
    audio = np.frombuffer(pcm, dtype=np.int16).astype(np.float32) / 32768.0
    try:
        segments, _info = model.transcribe(
            audio,
            language=language,  # None → auto-detect
            beam_size=1,
            vad_filter=False,   # our webrtcvad already segmented
            condition_on_previous_text=False,
        )
        text = "".join(seg.text for seg in segments).strip()
    except Exception as e:  # noqa: BLE001
        emit({"error": f"transcription failed: {e}"})
        return
    if text:
        emit({"text": text})


if __name__ == "__main__":
    main()
