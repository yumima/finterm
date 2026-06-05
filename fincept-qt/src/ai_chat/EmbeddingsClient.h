#pragma once

#include <QString>
#include <QVector>

#include <functional>
#include <optional>

namespace fincept {

/// Async client for a local OpenAI-compatible `/v1/embeddings` endpoint —
/// hearth (the local AI engine) by default.  Embeddings always go to the
/// local engine, even when the chat profile is Anthropic (the Anthropic API
/// has no embeddings endpoint), so this resolves its base URL independently
/// of the active chat profile.
///
/// Resolution order for the base URL:
///   1. env `FINCEPT_EMBEDDINGS_BASE_URL` (embeddings-specific override)
///   2. the shared engine base from `HearthService` (env override → hearth
///      default :11435) — the same base the chat path uses, so the two
///      never diverge
///
/// The request sends `model: "embedding"` — hearth's role registry resolves
/// that alias to the bound embedding model (e.g. nomic-embed-text).  Failure
/// (engine down, timeout, bad shape) calls back with `std::nullopt`; callers
/// degrade gracefully (MemoryTools falls back to keyword search).
class EmbeddingsClient {
  public:
    /// Embed a single string.  `done` is invoked exactly once on the app
    /// thread with the float vector, or nullopt on any failure.
    static void embed(const QString& text,
                      std::function<void(std::optional<QVector<float>>)> done);

    /// Resolved embeddings base URL (without the `/embeddings` suffix).
    static QString base_url();
};

} // namespace fincept
