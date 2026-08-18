#pragma once
#include <QWidget>

#include "services/ownership/OwnershipService.h"

class QComboBox;
class QLineEdit;
class QTableWidget;
class QTimer;

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
    /// A filer was picked from the ranked list.
    void firm_selected(const QString& cik);

  private:
    /// Which of the two rankings the selector is on.
    services::OwnershipService::FirmRanking current_ranking() const;

    void reload_firms();

    QLineEdit*    search_ = nullptr;
    QComboBox*    ranking_ = nullptr;
    QTableWidget* firms_ = nullptr;
    QString       selected_cik_;
    QTimer*       debounce_ = nullptr;
};

} // namespace fincept::screens
