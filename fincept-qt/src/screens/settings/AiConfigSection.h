#pragma once
// AiConfigSection.h — one home for everything AI.
//
// Settings used to scatter this across three sidebar entries: "LLM Config",
// "MCP Servers" and "AI System". Nothing told you they were related, and the
// order you had to touch them in (pick a provider → bind it to a role → grant
// tools → check what it did) ran across all three.
//
// This folds them into one section with four tabs in that order:
//
//   Models  — providers, keys, base URLs, profiles  (LlmConfigSection)
//   Roles   — which model each part of finterm uses (AiRolesSection)
//   Tools   — MCP servers and the tool catalogue    (McpServersSection)
//   Activity— traces, spend, kill-switch            (AiSystemSection)
//
// The existing sections are reused unchanged, so this is a navigation change
// rather than a rewrite of any panel.

#include <QTabWidget>
#include <QWidget>

namespace fincept::screens {

class LlmConfigSection;
class AiRolesSection;

class AiConfigSection : public QWidget {
    Q_OBJECT
  public:
    explicit AiConfigSection(QWidget* parent = nullptr);

    /// Forwarded to the Models tab so callers that used to hold an
    /// LlmConfigSection* keep working.
    LlmConfigSection* models_tab() const { return models_; }

  private:
    QTabWidget* tabs_ = nullptr;
    LlmConfigSection* models_ = nullptr;
    AiRolesSection* roles_ = nullptr;
};

} // namespace fincept::screens
