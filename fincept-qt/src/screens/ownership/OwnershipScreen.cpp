#include "screens/ownership/OwnershipScreen.h"

#include "screens/ownership/OwnershipUi.h"
#include "screens/ownership/ScanPanels.h"
#include "services/ownership/OwnershipService.h"
#include "ui/components/TooltipText.h"
#include "ui/theme/ThemeManager.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QShowEvent>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace fincept::screens {

using namespace fincept::screens::ownership_ui;

OwnershipScreen::OwnershipScreen(QWidget* parent) : QWidget(parent) {
    build_ui();
    setStyleSheet(table_stylesheet() +
                  QString("QTabBar::tab{color:%1;background:%2;padding:6px 16px;font-size:12px;"
                          "letter-spacing:1px;border:1px solid %3;margin-right:2px;}"
                          "QTabBar::tab:selected{color:%4;background:%5;}"
                          "QTabWidget::pane{border:0;}"
                          "QLineEdit{color:%4;background:%2;border:1px solid %3;padding:4px 8px;font-size:12px;}"
                          "QListWidget{background:%2;border:1px solid %3;font-size:12px;}")
                      .arg(ui::colors::TEXT_SECONDARY(), ui::colors::BG_RAISED(), ui::colors::BORDER_DIM(),
                           ui::colors::TEXT_PRIMARY(), ui::colors::BG_SURFACE()));
    connect(&ui::ThemeManager::instance(), &ui::ThemeManager::theme_changed, this,
            [this]() { setStyleSheet(table_stylesheet()); });

    auto& svc = services::OwnershipService::instance();
    connect(&svc, &services::OwnershipService::index_changed, this,
            [this](const QString& msg) { refresh_index_ui(msg); });
    connect(&svc, &services::OwnershipService::firms_found, this, [this]() {
        filer_results_->clear();
        const auto firms = services::OwnershipService::instance().last_firm_results();
        for (const auto& m : firms) {
            auto* it = new QListWidgetItem(QStringLiteral("%1   ·   %2 · %3 names")
                                               .arg(m.name, fmt::format_compact(m.book_value))
                                               .arg(m.position_count));
            it->setData(Qt::UserRole, m.cik);
            it->setData(Qt::UserRole + 1, m.name);
            filer_results_->addItem(it);
        }
        filer_results_->setVisible(!firms.isEmpty() && filer_search_->hasFocus());
    });
    refresh_index_ui({});
}

void OwnershipScreen::build_ui() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 8, 10, 8);
    root->setSpacing(8);

    // ── Top bar: title, index state, filer search ───────────────────────────
    auto* bar = new QHBoxLayout;
    bar->setSpacing(10);
    auto* title = new QLabel(QStringLiteral("OWNERSHIP"));
    title->setStyleSheet(QString("color:%1;font-size:14px;font-weight:700;letter-spacing:2px;")
                             .arg(ui::colors::ORANGE()));
    bar->addWidget(title);
    auto* sub = new QLabel(QStringLiteral("Where the informed parties are acting. Every row is a stock; "
                                          "click one to open it in Equity Research."));
    sub->setStyleSheet(QString("color:%1;font-size:12px;").arg(ui::colors::TEXT_SECONDARY()));
    bar->addWidget(sub, 1);

    index_lbl_ = new QLabel;
    index_lbl_->setStyleSheet(QString("color:%1;font-size:12px;").arg(ui::colors::TEXT_SECONDARY()));
    index_lbl_->setWordWrap(true);
    index_btn_ = new QPushButton;
    connect(index_btn_, &QPushButton::clicked, this, []() {
        auto& svc = services::OwnershipService::instance();
        if (!svc.index_ready())
            svc.build_index();
        else
            svc.resolve_symbols();
    });
    bar->addWidget(index_btn_);

    filer_search_ = new QLineEdit;
    filer_search_->setPlaceholderText(QStringLiteral("Filer — e.g. Berkshire, Citadel"));
    filer_search_->setClearButtonEnabled(true);
    filer_search_->setMinimumWidth(240);
    filer_search_->setMaximumWidth(300);
    filer_search_->setToolTip(ui::tooltip_wrap(QStringLiteral(
        "Open one 13F filer's whole disclosed equity book — what they hold, at what weight, and "
        "what moved last quarter. A drill, not a ranking: the question starts with a firm you have "
        "in mind.")));
    bar->addWidget(filer_search_);
    root->addLayout(bar);
    // The index state gets its own line: on the title bar it ran off the
    // right edge the moment it had something to say.
    root->addWidget(index_lbl_);

    // Suggestions drop under the search box; the list is the completer.
    filer_results_ = new QListWidget(this);
    filer_results_->setWindowFlags(Qt::ToolTip);
    filer_results_->setMinimumWidth(420);
    filer_results_->setMaximumHeight(260);
    filer_results_->hide();
    connect(filer_results_, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) {
        filer_results_->hide();
        show_filer(it->data(Qt::UserRole).toString());
    });
    filer_debounce_ = new QTimer(this);
    filer_debounce_->setSingleShot(true);
    filer_debounce_->setInterval(250);
    connect(filer_debounce_, &QTimer::timeout, this, [this]() {
        const QString q = filer_search_->text().trimmed();
        if (q.length() < 2) {
            filer_results_->hide();
            return;
        }
        const QPoint at = filer_search_->mapToGlobal(QPoint(0, filer_search_->height()));
        filer_results_->move(at);
        services::OwnershipService::instance().search_firms(q);
    });
    connect(filer_search_, &QLineEdit::textEdited, this, [this](const QString&) { filer_debounce_->start(); });
    connect(filer_search_, &QLineEdit::returnPressed, this, [this]() {
        if (filer_results_->count() > 0) {
            auto* it = filer_results_->item(0);
            filer_results_->hide();
            show_filer(it->data(Qt::UserRole).toString());
        }
    });

    // ── Body: the scans, or the empty state ─────────────────────────────────
    stack_ = new QStackedWidget;
    root->addWidget(stack_, 1);

    tabs_ = new QTabWidget;
    insider_buys_ = new InsiderBuysPanel;
    short_rank_ = new ShortRankPanel;
    movers_ = new MoversPanel;
    largest_funds_ = new LargestFundsPanel;
    for (auto* p : {static_cast<QWidget*>(insider_buys_), static_cast<QWidget*>(short_rank_),
                    static_cast<QWidget*>(movers_), static_cast<QWidget*>(largest_funds_)})
        p->setContentsMargins(0, 8, 0, 0);
    tabs_->addTab(insider_buys_, QStringLiteral("INSIDER BUYS"));
    tabs_->addTab(short_rank_, QStringLiteral("SHORT-CONSTRAINED"));
    tabs_->addTab(movers_, QStringLiteral("13F MOVERS"));
    // Appended, not inserted: saved tab indices stay valid.
    tabs_->addTab(largest_funds_, QStringLiteral("LARGEST FUNDS"));
    connect(tabs_, &QTabWidget::currentChanged, this, [this](int i) { load_tab(i); });
    auto open = [this](const QString& sym) {
        emit navigate_to_screen(QStringLiteral("equity_research"), sym);
    };
    connect(insider_buys_, &InsiderBuysPanel::stock_activated, this, open);
    connect(short_rank_, &ShortRankPanel::stock_activated, this, open);
    connect(movers_, &MoversPanel::stock_activated, this, open);
    connect(largest_funds_, &LargestFundsPanel::stock_activated, this, open);

    stack_->addWidget(tabs_);   // 0

    auto* empty = new QWidget;
    auto* ev = new QVBoxLayout(empty);
    ev->addStretch(1);
    auto* etitle = new QLabel(QStringLiteral("No 13F index yet"));
    etitle->setAlignment(Qt::AlignCenter);
    etitle->setStyleSheet(QString("color:%1;font-size:20px;font-weight:700;").arg(ui::colors::TEXT_PRIMARY()));
    ev->addWidget(etitle);
    auto* ebody = new QLabel(QStringLiteral(
        "The short-interest ranking and the 13F movers need the local index: two quarterly SEC 13F "
        "data sets — around 10,600 filers and 3 million positions each — indexed once. About 200 MB, "
        "a couple of minutes. Insider buys work without it and are on the first tab."));
    ebody->setAlignment(Qt::AlignCenter);
    ebody->setWordWrap(true);
    ebody->setMaximumWidth(640);
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
    stack_->addWidget(empty_page_);   // 1
}

void OwnershipScreen::show_filer(const QString& cik) {
    // A searched fund opens beside the ranking, exactly as a clicked row does,
    // rather than on a page of its own that hid the list behind a back button.
    //
    // The book is requested BEFORE the tab switch: switching can start the
    // ranking scan (first visit), and the searched fund must not queue behind
    // it for the interactive lane. If the fund is ranked, its row is
    // highlighted when the ranking lands.
    largest_funds_->show_fund(cik);
    stack_->setCurrentIndex(0);
    tabs_->setCurrentIndex(tabs_->indexOf(largest_funds_));   // loads the ranking if needed
}

void OwnershipScreen::load_tab(int index) {
    if (index < 0 || index > 3 || loaded_[index])
        return;
    loaded_[index] = true;
    if (index == 0)
        insider_buys_->refresh();
    else if (index == 1)
        short_rank_->refresh();
    else if (index == 2)
        movers_->refresh();
    else
        largest_funds_->refresh();
}

void OwnershipScreen::refresh_index_ui(const QString& msg) {
    auto& svc = services::OwnershipService::instance();
    const bool ready = svc.index_ready();
    // The insider scan works with no index at all, so the empty page only
    // takes over when there is nothing else to show.
    stack_->setCurrentIndex(ready ? 0 : 1);
    index_btn_->setText(ready ? QStringLiteral("MAP MORE SYMBOLS") : QStringLiteral("BUILD 13F INDEX"));
    index_btn_->setToolTip(ui::tooltip_wrap(
        ready ? QStringLiteral("Resolve more CUSIPs to tickers via OpenFIGI so more securities are "
                               "searchable and rankable by symbol.")
              : QStringLiteral("Download two quarterly SEC 13F data sets — every filer, every position "
                               "— and index them locally. About 200 MB, runs once.")));
    index_btn_->setEnabled(!svc.index_busy());
    index_btn_->setVisible(ready);
    index_lbl_->setText(msg.isEmpty() ? (ready ? svc.index_status_text() : QString()) : msg);
    filer_search_->setEnabled(ready);
    if (ready && shown_once_) {
        // The index arriving is what makes two of the scans answerable.
        if (tabs_->currentIndex() == 1 && !svc.short_rank().loaded) short_rank_->refresh();
        // An error result counts as not loaded: the usual one is "no 13F data
        // ingested yet", which the index arriving is exactly what fixes.
        if (tabs_->currentIndex() == 2 && (!svc.movers().loaded || !svc.movers().error.isEmpty()))
            movers_->refresh();
        if (tabs_->currentIndex() == 3 && (!svc.top_firms().loaded || !svc.top_firms().error.isEmpty()))
            largest_funds_->refresh();
    }
}

void OwnershipScreen::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    auto& svc = services::OwnershipService::instance();
    // The Form 4 store fills itself: a few business days per pass, checked
    // again every few hours. A scan that waits for a button is a tab that is
    // empty on every first open.
    svc.ensure_form4_current();
    if (!shown_once_) {
        shown_once_ = true;
        svc.check_for_newer_quarter();
        load_tab(tabs_->currentIndex());
    }
}

void OwnershipScreen::restore_state(const QVariantMap& state) {
    const int tab = state.value(QStringLiteral("tab"), 0).toInt();
    if (tab >= 0 && tab < tabs_->count())
        tabs_->setCurrentIndex(tab);
}

QVariantMap OwnershipScreen::save_state() const {
    return {{QStringLiteral("tab"), tabs_->currentIndex()}};
}

} // namespace fincept::screens
