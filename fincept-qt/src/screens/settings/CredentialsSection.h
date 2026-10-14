#pragma once
// CredentialsSection.h — Settings › API Keys.
//
// Every data-provider key and identity setting finterm can use (the
// generated keys::catalogue()), stored in the OS keyring on this machine.
// The user's configured keys are listed first under "Your keys"; the rest
// sit in groups by what they are for (market data, crypto, economics, …),
// filterable by name. Each card shows the variable name, lets a secret be
// revealed on request, and shows identifiers (emails, client ids) in clear.
// The page reloads when opened (SettingsScreen::select_section), so it
// reflects removals made on All Credentials.

#include <QHash>
#include <QSet>
#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QVBoxLayout;

namespace fincept::screens {

class CredentialsSection : public QWidget {
    Q_OBJECT
  public:
    explicit CredentialsSection(QWidget* parent = nullptr);

    /// Re-read which keys are set and re-place the cards. Called when the
    /// Settings screen is shown.
    void reload();

  private:
    struct Card {
        QWidget* frame = nullptr;
        QLineEdit* field = nullptr;
        QLabel* status = nullptr;
        QPushButton* reveal = nullptr;
        QPushButton* save = nullptr;
        QPushButton* remove = nullptr;
        QString group;
        QString label;
        bool secret = true;
    };

    void build_ui();
    void apply_theme();
    QWidget* build_card(const QString& key, Card& card);
    void place_cards();
    void apply_filter();
    void set_card_state(const QString& key, bool configured);
    void save_key(const QString& key);
    void remove_key(const QString& key);
    void toggle_reveal(const QString& key);

    QHash<QString, Card> cards_;
    QStringList order_; // catalogue order
    QSet<QString> configured_;
    bool loaded_ = false;  ///< at least one successful keyring read
    bool read_ok_ = true;  ///< the latest read succeeded

    QLabel* info_ = nullptr;
    QLabel* summary_ = nullptr;
    QLineEdit* filter_ = nullptr;
    QCheckBox* only_configured_ = nullptr;
    QWidget* mine_box_ = nullptr;
    QVBoxLayout* mine_layout_ = nullptr;
    QHash<QString, QWidget*> group_boxes_;
    QHash<QString, QVBoxLayout*> group_layouts_;
};

} // namespace fincept::screens
