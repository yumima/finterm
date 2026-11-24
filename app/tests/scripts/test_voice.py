"""Voice input's scripts read their sound from stdin (the app opens the
microphone), and say what they heard as JSON lines. No microphone, no network."""
import json
import os
import subprocess
import sys
from pathlib import Path

VOICE = Path(__file__).resolve().parents[2] / "scripts" / "voice"
fails = 0


def check(name, ok, got=""):
    global fails
    if not ok:
        fails += 1
        print(f"FAIL - {name}: {got}")


for f in ("whisper_stt.py", "deepgram_stt.py"):
    src = (VOICE / f).read_text(encoding="utf-8")
    check(f"{f} opens no microphone", "import pyaudio" not in src and "Microphone" not in src)
    check(f"{f} reads stdin", "sys.stdin.buffer.read" in src or "os.read(0" in src)

sys.path.insert(0, str(VOICE))
import deepgram_stt as dg  # noqa: E402

final = {"type": "Results", "is_final": True, "channel": {"alternatives": [{"transcript": " buy apple "}]}}
check("a final result is text", dg.transcript(final) == "buy apple")
check("an interim one is not", dg.transcript({**final, "is_final": False}) == "")
check("nor silence, nor anything else", dg.transcript({**final, "channel": {"alternatives": [{"transcript": ""}]}}) == ""
      and dg.transcript({"type": "Metadata"}) == "" and dg.transcript([]) == "")
check("key terms by model", "keyterm=P%2FE" in dg.listen_url("nova-3", "en", ["P/E"])
      and "keywords=AAPL" in dg.listen_url("nova-2", "en", ["AAPL"]))
p = subprocess.run([sys.executable, str(VOICE / "deepgram_stt.py")], input=b"", capture_output=True, env=dict(os.environ, DEEPGRAM_API_KEY=""))
check("no key is said", b"DEEPGRAM_API_KEY not set" in p.stdout, p.stdout)

# Whisper's loop, with stand-ins for the three packages: tone is speech, zero is not.
import tempfile  # noqa: E402
with tempfile.TemporaryDirectory() as d:
    (Path(d) / "numpy.py").write_text("int16 = float32 = 0\nclass A(bytes):\n    def astype(s, t): return s\n    def __truediv__(s, x): return s\ndef frombuffer(b, dtype): return A(b)\n")
    (Path(d) / "webrtcvad.py").write_text("class Vad:\n    def __init__(s, a): pass\n    def is_speech(s, f, r): return f[0] != 0\n")
    (Path(d) / "faster_whisper.py").write_text(
        "class S:\n    def __init__(s, t): s.text = t\nclass WhisperModel:\n    def __init__(s, *a, **k): pass\n"
        "    def transcribe(s, audio, **k): return [S(f' {len(audio) // 960} frames')], None\n")
    F, Z = b"\x01" * 960, b"\x00" * 960
    sound = Z * 20 + F * 30 + Z * 30 + F * 3 + Z * 30 + F * 12 + Z * 26 + b"\x01" * 100
    p = subprocess.run([sys.executable, str(VOICE / "whisper_stt.py")], input=sound, capture_output=True, timeout=60,
                       env=dict(os.environ, PYTHONPATH=d))
    out = [json.loads(x) for x in p.stdout.decode().splitlines()]
    check("ready, then what was said, then the end of the sound", [next(iter(o)) for o in out][:2] == ["status", "status"]
          and out[1] == {"status": "listening"} and out[-1] == {"status": "stopped"}, (out, p.stderr[-300:]))
    said = [o["text"] for o in out if "text" in o]
    check("two utterances, a click is none", len(said) == 2 or len(said) == 3 and "frames" in said[0], said)

print("voice:", "ok" if not fails else f"{fails} failed")
sys.exit(1 if fails else 0)
