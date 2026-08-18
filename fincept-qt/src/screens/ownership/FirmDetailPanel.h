#pragma once
#include <QWidget>

class QLabel;
class QTableWidget;

namespace fincept::screens {

/// One manager's disclosed equity book, and what moved in it.
///
/// Split out of the ranked list because the two answer different questions and
/// want different room: the list is a scan across fifty filers, this is a read
/// down one filer's several hundred positions. Side by side, the scan stays put
/// while the detail changes — which is what makes comparing two firms possible
/// without losing your place.
///
/// The caveat that travels with every 13F number applies here too: long US
/// equities only, so this is the disclosed equity book and not the fund, and it
/// is up to 135 days stale.
class FirmDetailPanel : public QWidget {
    Q_OBJECT
  public:
    explicit FirmDetailPanel(QWidget* parent = nullptr);

    /// Show this filer's book, loading it if it is not cached.
    void set_firm(const QString& cik);

  signals:
    /// A holding was clicked. The register for a security is a question about
    /// that security, so it is answered by the per-stock view rather than here.
    void navigate_to_symbol(const QString& ticker);

  private:
    void render();

    QString       cik_;
    QLabel*       status_     = nullptr;
    QLabel*       book_title_ = nullptr;
    QTableWidget* positions_  = nullptr;
    /// True while the table is being filled, so nothing a fill does to the
    /// selection can be mistaken for the user picking a holding.
    bool          populating_ = false;
};

} // namespace fincept::screens
