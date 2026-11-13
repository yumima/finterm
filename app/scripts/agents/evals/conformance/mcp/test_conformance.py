"""MCP spec conformance suite (skeleton — populated by Track 5).

Verifies that finterm's internal `McpProvider` implements the full
MCP spec — tools, resources, prompts, sampling, elicitation,
progress, cancellation, logging — with proper capability
negotiation in the handshake.

External marketplace servers (Track 4) get a separate per-server
conformance run; this file covers internal-server conformance.
"""
from __future__ import annotations

import pytest

pytestmark = pytest.mark.skip(
    reason="conformance skeleton — populated by Track 5 (full MCP spec)"
)


def test_handshake_capability_negotiation() -> None:
    """initialize response advertises every spec capability."""


def test_tools_list_and_call() -> None:
    """tools/list returns registered tools; tools/call dispatches."""


def test_resources_list_and_read() -> None:
    """resources/list + resources/read for portfolio_snapshot etc."""


def test_prompts_list_and_get() -> None:
    """prompts/list returns curated prompts; prompts/get renders one."""


def test_sampling_create_message() -> None:
    """Server-initiated sampling/createMessage flows through the client."""


def test_elicitation_create() -> None:
    """elicitation/create asks the user a structured question."""


def test_progress_notifications() -> None:
    """Long tools emit progress notifications during execution."""


def test_cancellation() -> None:
    """notifications/cancelled aborts an in-flight tool call."""


def test_logging_message() -> None:
    """notifications/message conveys structured server logs."""
