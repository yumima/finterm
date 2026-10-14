#pragma once
// StoredCredentialsSection.h — Settings › All Credentials.
//
// One read-only map of every secret finterm keeps in the OS keyring for the
// active profile — data-provider keys, alert channels, broker and exchange
// logins, prediction-market accounts, MCP sign-ins, AI provider keys — grouped
// by what they are for, each naming the screen that edits it. Values are
// never shown here. Entries whose owning screen holds related records
// (AI providers, MCP servers, voice) are removed there, not here, so the
// two cannot fall out of step.

#include <QWidget>

class QLabel;
class QVBoxLayout;

namespace fincept::screens {

class StoredCredentialsSection : public QWidget {
    Q_OBJECT
  public:
    explicit StoredCredentialsSection(QWidget* parent = nullptr);
    void reload();

  private:
    QLabel* title_ = nullptr;
    QLabel* info_ = nullptr;
    QLabel* summary_ = nullptr;
    QWidget* list_ = nullptr;
    QVBoxLayout* list_layout_ = nullptr;
};

} // namespace fincept::screens
