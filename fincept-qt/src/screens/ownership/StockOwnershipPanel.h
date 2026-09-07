#pragma once
#include "screens/ownership/OwnershipTypes.h"

#include <QWidget>

class QAbstractScrollArea;
class QLabel;
class QPushButton;
class QScrollArea;

namespace fincept::screens {

class FirmDetailPanel;
class HoldersTable;
class InsidersPanel;
class OwnershipHeader;
class ShortInterestPanel;

/// Ownership for one security — the Equity Research tab.
///
/// One header and three panels, top to bottom, in the order the industry
/// reads them: who holds it (13F), what the insiders did (Form 4), what the
/// shorts hold (FINRA). Everything scrolls as one page; nothing is a tile
/// inside a tile, and nothing is hidden behind a button. The host names the
/// symbol; this panel is told what to show.
///
/// A holder row opens the filer's whole book in place, with a way back, so
/// "who else does BlackRock own" is answered without leaving the stock.
class StockOwnershipPanel : public QWidget {
    Q_OBJECT
  public:
    explicit StockOwnershipPanel(QWidget* parent = nullptr);

    /// Point the panel at a security. Idempotent for the same symbol.
    void set_symbol(const QString& symbol);
    QString symbol() const { return symbol_; }

  signals:
    /// Ask the shell to open another screen for @p ticker.
    void navigate_to_screen(const QString& screen_id, const QString& ticker);

  protected:
    /// Wheel events over a table that cannot scroll any further go to the
    /// page, so the reader is never stuck inside a panel.
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void build_ui();
    void render();
    void show_filer(const QString& cik, const QString& name);
    void show_stock();

    QString symbol_;
    QLabel* status_ = nullptr;
    QWidget* stock_page_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    OwnershipHeader* header_ = nullptr;
    HoldersTable* holders_ = nullptr;
    QWidget* no_index_ = nullptr;
    QLabel* no_index_text_ = nullptr;
    QPushButton* build_btn_ = nullptr;
    InsidersPanel* insiders_ = nullptr;
    ShortInterestPanel* shorts_ = nullptr;

    QWidget* filer_page_ = nullptr;
    QPushButton* back_btn_ = nullptr;
    FirmDetailPanel* filer_ = nullptr;
    class QStackedWidget* stack_ = nullptr;
};

} // namespace fincept::screens
