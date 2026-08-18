#pragma once
#include <QVector>
#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;

namespace fincept::screens {

/// Market-wide Form 4 — where insiders are buying, across every issuer.
///
/// The per-stock insider table answers "what did insiders do at this company",
/// which is only askable once you already have a company. This asks the
/// question that comes first, and is the only view here that can hand you a
/// ticker you were not already thinking about.
///
/// What it ranks is deliberately narrow. Around a thousand Form 4 rows are
/// filed every day and most carry no information: grants, option exercises and
/// tax withholding are compensation mechanics, and an "insider buying" screen
/// that counts them shows relentless buying everywhere forever. Only
/// open-market purchases are ranked. Sells are available but do not drive the
/// ranking, because insiders sell for a dozen reasons and buy for one.
///
/// Clusters — several insiders at one issuer buying within days — are marked
/// rather than scored, because "three officers bought" and "one officer bought
/// three times" are different events and which matters is the reader's call.
class InsiderLeadersPanel : public QWidget {
    Q_OBJECT
  public:
    explicit InsiderLeadersPanel(QWidget* parent = nullptr);

  signals:
    /// An issuer was picked. Carries the ticker where the filing named one.
    void issuer_selected(const QString& symbol, const QString& issuer);

  private:
    void reload();
    /// Label the insiders behind the current ranking (per-owner EDGAR fetch).
    void classify();
    void run_scan();

    QComboBox*    direction_ = nullptr;
    QComboBox*    window_    = nullptr;
    QPushButton*  scan_btn_  = nullptr;
    QLabel*       status_    = nullptr;
    QTableWidget* table_     = nullptr;
    bool          scanning_  = false;
    bool          loading_   = false;
    /// A request arrived while one was in flight; re-run when it lands so the
    /// table always ends up matching the controls.
    bool          restack_   = false;
    /// Business days per scan press. A day is ~500 submission fetches.
    static constexpr int kMaxScanDays = 5;
};

} // namespace fincept::screens
