#pragma once
#include "ai_chat/EngineState.h"
#include "core/result/Result.h"

#include <QString>

namespace finterm {

/// Whether this user's finterm uses hearth, and what its chat used before.
/// Kept apart from the chat provider: memory and read-aloud may use hearth
/// while the chat is with a cloud model.
class EngineLinkRepository {
  public:
    static EngineLinkRepository& instance();

    engine::Link link() const;
    bool linked() const { return link() == engine::Link::Connected; }

    /// Use hearth; for the chat as well if `for_chat` (always, when there is
    /// no chat provider). The provider it replaces is remembered.
    Result<void> connect(bool for_chat);
    /// Stop using it, and give the chat its earlier provider back (none, if
    /// there was none). Not asked about again.
    Result<void> disconnect();
    /// "Not now": as disconnect, for one who never connected.
    Result<void> decline();

    /// The chat's provider, "" when there is none.
    QString chat_provider() const;

    bool start_with_app() const;
    Result<void> set_start_with_app(bool on);

  private:
    EngineLinkRepository() = default;
};

} // namespace finterm
