#pragma once
#include <QWidget>

#include "screens/IStatefulScreen.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QSplitter;
class QStackedWidget;
class QStringListModel;
class QTabWidget;
class QTimer;

namespace fincept::screens {

class FirmBookPanel;
class FirmDetailPanel;
class InsiderLeadersPanel;
class StockOwnershipPanel;

/// OWNERSHIP — the 13F universe keyed by MANAGER.
///
/// "Who is holding what, and what are they doing with it" has two readings,
/// and they want different screens. Keyed by security it answers "who owns
/// AAPL", which belongs inside the workflow where someone is already looking
/// at AAPL — Equity Research, where StockOwnershipPanel now lives. Keyed by
/// manager it answers "what does this firm own", which is a browse: you arrive
/// without a ticker in mind and leave with one. Bloomberg splits the same way,
/// HDS from the security and the portfolio functions from the portfolio.
///
/// Carrying both on one screen meant the per-security half duplicated the
/// Equity Research tab exactly, and the manager half — the part that is only
/// here — was squeezed into a third of the width.
///
/// So this screen is the browse. The ranked list of filers and the selected
/// firm's book get the whole window, and the index's own state (build it, pull
/// the current quarter, map more symbols) lives in the toolbar because that is
/// the one place it is not repeated per ticker.
class OwnershipScreen : public QWidget, public IStatefulScreen {
    Q_OBJECT
  public:
    explicit OwnershipScreen(QWidget* parent = nullptr);

    // IStatefulScreen
    void restore_state(const QVariantMap& state) override;
    QVariantMap save_state() const override;
    QString state_key() const override { return QStringLiteral("ownership"); }
    int state_version() const override { return 2; }

  signals:
    /// Ask the shell to open another screen for @p ticker (Equity Research).
    void navigate_to_screen(const QString& screen_id, const QString& ticker);

  protected:
    void showEvent(QShowEvent* e) override;

  private:
    void build_ui();
    void apply_theme();
    void refresh_index_ui(const QString& msg);
    /// Put @p symbol's register in the detail pane and offer the way back.
    /// Every route to a security — a holding in a firm's book, an issuer in
    /// the insider ranking, the search box — lands here, so they cannot drift
    /// apart in what they show or in how the reader gets out.
    void show_symbol(const QString& symbol);

    QPushButton*    index_btn_  = nullptr;
    QLineEdit*      ticker_     = nullptr;
    QStringListModel* ticker_model_ = nullptr;
    /// Says what the search box is doing when it cannot just open something —
    /// still looking, or nothing by that name.
    QLabel*         ticker_note_ = nullptr;
    QTimer*         ticker_debounce_ = nullptr;
    QPushButton*    map_btn_    = nullptr;
    QLabel*         index_lbl_  = nullptr;
    QWidget*        empty_page_ = nullptr;
    FirmBookPanel*       firm_book_    = nullptr;
    FirmDetailPanel*     firm_detail_  = nullptr;
    StockOwnershipPanel* stock_panel_  = nullptr;
    QStackedWidget*      detail_stack_ = nullptr;
    QStackedWidget*      firm_stack_   = nullptr;
    InsiderLeadersPanel* insiders_     = nullptr;
    QTabWidget*          left_         = nullptr;
    QPushButton*         back_btn_     = nullptr;
    QString              selected_firm_name_;
    /// Set once a filer has actually been picked. The display name cannot
    /// stand in for this — a 13F record with an empty name would read as "no
    /// firm selected" and hide the way back out of a holding.
    QString              selected_firm_cik_;
    /// What the detail pane is currently showing, so a late suggestion list
    /// does not pop open over a register the reader has already opened.
    QString              shown_symbol_;
    /// The query the suggestions in @ref ticker_model_ answer, as reported by
    /// the search itself rather than read off the box when they arrive — two
    /// requests can be in flight and land out of order, and a list left over
    /// from "Alphabet" must not resolve an Enter pressed on "Berkshire".
    QString              ticker_results_query_;
    /// A search is out for this text; nothing has come back for it yet.
    QString              ticker_pending_query_;
    QWidget*             split_        = nullptr;
    bool            loaded_once_ = false;
};

} // namespace fincept::screens
