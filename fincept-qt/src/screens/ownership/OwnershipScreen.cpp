#include "screens/ownership/OwnershipScreen.h"

#include "screens/ownership/FirmBookPanel.h"
#include "screens/ownership/FirmDetailPanel.h"
#include "screens/ownership/InsiderLeadersPanel.h"
#include "screens/ownership/StockOwnershipPanel.h"
#include "services/ownership/OwnershipService.h"
#include "ui/theme/Theme.h"
#include "ui/theme/ThemeManager.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QShowEvent>
#include <QSplitter>
#include <QTabWidget>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace fincept::screens {

OwnershipScreen::OwnershipScreen(QWidget* parent) : QWidget(parent) {
    build_ui();
    apply_theme();
    connect(&ui::ThemeManager::instance(), &ui::ThemeManager::theme_changed, this,
            [this]() { apply_theme(); });

    auto& svc = services::OwnershipService::instance();
    connect(&svc, &services::OwnershipService::index_changed, this,
            [this](const QString& msg) { refresh_index_ui(msg); });
    refresh_index_ui({});
}

// Painted here rather than inherited: the tables are the screen, and Qt's
// default palette renders them light-on-light against the terminal's dark
// ground — near-white text on Qt's own alternating-row grey.
void OwnershipScreen::apply_theme() {
    setStyleSheet(QString("QWidget{background:%1;color:%2;}"
                          "QTableWidget{background:%1;gridline-color:%3;"
                          "alternate-background-color:%6;color:%2;}"
                          "QTableWidget::item{color:%2;}"
                          "QHeaderView::section{background:%4;color:%5;padding:4px;border:0;}")
                      .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY(),
                           ui::colors::BORDER_DIM(), ui::colors::BG_RAISED(),
                           ui::colors::TEXT_SECONDARY(), ui::colors::BG_SURFACE()));
}

void OwnershipScreen::build_ui() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 6, 8, 6);
    root->setSpacing(6);

    auto* bar = new QHBoxLayout;
    bar->setSpacing(8);
    auto* title = new QLabel(QStringLiteral("OWNERSHIP"));
    title->setStyleSheet(QString("color:%1;font-size:13px;font-weight:700;letter-spacing:1px;")
                             .arg(ui::colors::ORANGE()));
    bar->addWidget(title);

    auto* sub = new QLabel(QStringLiteral(
        "Who is running the money, and what they own. For a single stock's register, "
        "open it in Equity Research → Ownership."));
    sub->setStyleSheet(QString("color:%1;font-size:12px;").arg(ui::colors::TEXT_SECONDARY()));
    bar->addWidget(sub, 1);

    // The index controls live on the top bar, not inside a panel: without an
    // index every view here is empty, so "how do I fix that" must be the most
    // findable thing on the screen rather than something to hunt for.
    index_btn_ = new QPushButton;
    connect(index_btn_, &QPushButton::clicked, this, [this]() {
        auto& svc = services::OwnershipService::instance();
        if (!svc.index_ready())
            svc.build_index();
        else
            svc.resolve_symbols(2000);
    });
    bar->addWidget(index_btn_);
    root->addLayout(bar);

    index_lbl_ = new QLabel;
    index_lbl_->setVisible(false);   // shown only when it has something to add
    index_lbl_->setWordWrap(true);
    index_lbl_->setStyleSheet(QString("color:%1;font-size:12px;")
                                  .arg(ui::colors::TEXT_SECONDARY()));
    root->addWidget(index_lbl_);

    // The whole window. This is the one view that exists nowhere else, and
    // sharing the screen with a per-security register left the ranked list of
    // filers in a third of the width with its columns scrolled off.
    // Left: the scan across filers. Right: whatever the reader just clicked.
    // Keeping the list fixed while the right half changes is what makes
    // comparing two firms possible without losing your place in the ranking.
    firm_book_ = new FirmBookPanel;
    firm_detail_ = new FirmDetailPanel;
    stock_panel_ = new StockOwnershipPanel;
    stock_panel_->set_chrome_visible(false);   // the click supplies the symbol

    detail_stack_ = new QStackedWidget;
    detail_stack_->addWidget(firm_detail_);   // 0 — a firm's book
    detail_stack_->addWidget(stock_panel_);   // 1 — a security's register

    connect(firm_book_, &FirmBookPanel::firm_selected, this, [this](const QString& cik) {
        firm_detail_->set_firm(cik);
        detail_stack_->setCurrentIndex(0);
    });
    // A holding is a security, and the question after "they own this" is "who
    // else does". Answer it in place rather than sending the reader to another
    // screen and losing the firm they were reading.
    connect(firm_detail_, &FirmDetailPanel::navigate_to_symbol, this,
            [this](const QString& ticker) {
                if (ticker.isEmpty())
                    return;
                stock_panel_->set_symbol(ticker);
                detail_stack_->setCurrentIndex(1);
            });

    // Two aggregated views, one detail pane. BY FIRM asks who is running the
    // money; INSIDERS asks where the people who run the companies are putting
    // their own. Both hand the same right-hand pane a thing to open, so the
    // reader never loses their place in either list.
    insiders_ = new InsiderLeadersPanel;
    connect(insiders_, &InsiderLeadersPanel::issuer_selected, this,
            [this](const QString& symbol, const QString&) {
                if (symbol.isEmpty())
                    return;   // the filing named no security to open
                stock_panel_->set_symbol(symbol);
                detail_stack_->setCurrentIndex(1);
            });

    left_ = new QTabWidget;
    // Only the firm view depends on the 13F index. INSIDERS reads Form 4 from
    // EDGAR and works with no index at all, so the "build an index" page
    // belongs inside this tab rather than over the whole screen.
    firm_stack_ = new QStackedWidget;
    firm_stack_->addWidget(firm_book_);   // 0
    left_->addTab(firm_stack_, QStringLiteral("BY FIRM"));
    left_->addTab(insiders_, QStringLiteral("INSIDERS"));

    auto* split = new QSplitter(Qt::Horizontal);
    split->setChildrenCollapsible(false);
    split->addWidget(left_);
    split->addWidget(detail_stack_);
    split->setStretchFactor(0, 5);
    split->setStretchFactor(1, 5);
    split->setSizes({760, 900});
    split_ = split;

    // ── Empty state ─────────────────────────────────────────────────────────
    // Without an index the screen has exactly one thing to say and one button
    // to offer, so it says it once, in the middle.
    auto* empty = new QWidget;
    auto* ev = new QVBoxLayout(empty);
    ev->addStretch(1);
    auto* etitle = new QLabel(QStringLiteral("No 13F ownership index yet"));
    etitle->setAlignment(Qt::AlignCenter);
    etitle->setStyleSheet(QString("color:%1;font-size:20px;font-weight:700;")
                              .arg(ui::colors::TEXT_PRIMARY()));
    ev->addWidget(etitle);
    auto* ebody = new QLabel(QStringLiteral(
        "Downloads two quarterly SEC 13F data sets — around 10,600 filers and 2.4 million "
        "positions each — and indexes them locally. Every filer then gets its complete "
        "book and its quarter-over-quarter changes, answered in milliseconds with no "
        "network.\n\nRuns once, takes a couple of minutes."));
    ebody->setAlignment(Qt::AlignCenter);
    ebody->setWordWrap(true);
    ebody->setMaximumWidth(620);
    ebody->setStyleSheet(QString("color:%1;font-size:13px;").arg(ui::colors::TEXT_SECONDARY()));
    auto* ebrow = new QHBoxLayout;
    ebrow->addStretch(1);
    ebrow->addWidget(ebody);
    ebrow->addStretch(1);
    ev->addLayout(ebrow);
    auto* ebtn = new QPushButton(QStringLiteral("BUILD 13F INDEX"));
    ebtn->setMinimumWidth(220);
    ebtn->setMinimumHeight(34);
    connect(ebtn, &QPushButton::clicked, this, [this]() {
        services::OwnershipService::instance().build_index();
        refresh_index_ui({});
    });
    auto* ebtnrow = new QHBoxLayout;
    ebtnrow->addStretch(1);
    ebtnrow->addWidget(ebtn);
    ebtnrow->addStretch(1);
    ev->addSpacing(14);
    ev->addLayout(ebtnrow);
    ev->addStretch(2);
    empty_page_ = empty;

    firm_stack_->addWidget(empty_page_);   // 1 — swapped in when there is no index
    root->addWidget(split_, 1);
}

void OwnershipScreen::refresh_index_ui(const QString& msg) {
    auto& svc = services::OwnershipService::instance();
    const bool ready = svc.index_ready();
    if (firm_stack_)
        firm_stack_->setCurrentIndex(ready ? 0 : 1);
    // The toolbar control is redundant while the empty page owns the action.
    index_btn_->setVisible(ready);
    index_lbl_->setVisible(ready && !msg.isEmpty());
    index_btn_->setText(ready ? QStringLiteral("MAP MORE SYMBOLS")
                              : QStringLiteral("BUILD 13F INDEX"));
    index_btn_->setToolTip(
        ready ? QStringLiteral("Resolve more CUSIPs to tickers via OpenFIGI so more securities "
                               "are searchable by symbol.")
              : QStringLiteral("Download one quarterly SEC 13F data set — every filer, every "
                               "position — and index it locally. About 100 MB, runs once."));
    index_btn_->setEnabled(!svc.index_busy());
    index_lbl_->setText(msg.isEmpty() ? svc.index_status_text() : msg);
}

void OwnershipScreen::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    if (!loaded_once_) {
        loaded_once_ = true;
        services::OwnershipService::instance().check_for_newer_quarter();
    }
}

void OwnershipScreen::restore_state(const QVariantMap& /*state*/) {
    // Nothing per-ticker survives here any more: the firm list is the screen,
    // and which firm is selected is a browse position rather than a setting.
}

QVariantMap OwnershipScreen::save_state() const { return {}; }

} // namespace fincept::screens
