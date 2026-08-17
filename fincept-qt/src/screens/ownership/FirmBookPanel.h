#pragma once
#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;

namespace fincept::screens {

/// BY FIRM — one manager's whole disclosed equity book, and what moved.
///
/// The other traversal of the same 13F index. SmartMoneyPanel asks "who holds
/// this stock"; this asks "what does this firm own". Both read the same
/// filings, keyed differently, which is why the index is fetched per manager
/// and inverted for the stock view rather than the reverse.
///
/// Bloomberg's nearest equivalent is PHDC. The caveat is the same one that
/// travels with every 13F number and is stated once at the top: long US
/// equities only, so this is the disclosed equity book and not the fund, and
/// it is up to 135 days stale.
///
/// Also hosts the manager-list editor, because "which firms do I track" is a
/// question you ask while looking at the list, not from a settings screen.
class FirmBookPanel : public QWidget {
    Q_OBJECT
  public:
    explicit FirmBookPanel(QWidget* parent = nullptr);

  signals:
    /// The user asked to see one of this firm's holdings as a security.
    void navigate_to_symbol(const QString& issuer_name);

  private:
    void reload_managers();
    void render();

    QComboBox*    firm_ = nullptr;
    QPushButton*  load_btn_ = nullptr;
    QPushButton*  edit_btn_ = nullptr;
    QPushButton*  seed_btn_ = nullptr;
    QLabel*       status_ = nullptr;
    QLabel*       caveat_ = nullptr;
    QTableWidget* positions_ = nullptr;
    QTableWidget* moves_ = nullptr;
};

} // namespace fincept::screens
