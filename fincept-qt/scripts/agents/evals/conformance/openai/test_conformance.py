"""OpenAI API conformance suite (skeleton — populated by Track 2).

Verifies that the local AI engine (plans/local-ai-engine.md)
accepts and emits OpenAI-compatible Chat Completions / Embeddings
/ Audio Transcriptions traffic.

Tied to a pinned openai-python SDK version per release (when the
engine ships).  Drift in either direction — OpenAI changes their
spec, or the engine regresses — fails the suite loudly.
"""
from __future__ import annotations

import pytest

pytestmark = pytest.mark.skip(
    reason="conformance skeleton — populated by Track 2 (local runtime)"
)


def test_chat_completions_minimal() -> None:
    """POST /v1/chat/completions returns the OpenAI envelope shape."""


def test_chat_completions_streaming_chunks() -> None:
    """Streaming responses use OpenAI SSE chunk format."""


def test_chat_completions_tool_calls_round_trip() -> None:
    """tools[] submitted by client come back in tool_calls field."""


def test_embeddings_response_shape() -> None:
    """POST /v1/embeddings returns { data: [{ embedding: [...] }] }."""


def test_audio_transcriptions_response_shape() -> None:
    """POST /v1/audio/transcriptions returns { text: ... }."""


def test_models_list_response_shape() -> None:
    """GET /v1/models returns strict OpenAI shape (no extra fields)."""
