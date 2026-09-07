#include "screens/ownership/StockOwnershipPanel.h"

#include "screens/ownership/FirmDetailPanel.h"
#include "screens/ownership/HoldersTable.h"
#include "screens/ownership/InsidersPanel.h"
#include "screens/ownership/OwnershipHeader.h"
#include "screens/ownership/OwnershipUi.h"
#include "screens/ownership/ShortInterestPanel.h"
#include "services/ownership/OwnershipService.h"

#include <QAbstractScrollArea>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QWheelEvent>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace fincept::screens {

using namespace fincept::ownership;
using namespace fincept::screens::ownership_ui;

StockOwnershipPanel::StockOwnershipPanel(QWidget* parent) : QWidget(parent) {
    build_ui();
    setStyleSheet(table_stylesheet());

    auto& svc = services::OwnershipService::instance();
    connect(&svc, &services::OwnershipService::snapshot_updated, this, [this](const QString& sym) {
        if (sym.compare(symbol_, Qt::CaseInsensitive) == 0)
            render();
    });
    connect(&svc, &services::OwnershipService::load_finished, this, [this](const QString& sym) {
        if (sym.compare(symbol_, Qt::CaseInsensitive) == 0)
            render();
    });
    // The index arriving mid-session is the one event that changes what this
    // panel can show: the holders half goes from "build one" to a table.
    connect(&svc, &services::OwnershipService::index_changed, this, [this](const QString&) {
        if (!symbol_.isEmpty())
            services::OwnershipService::instance().ensure_holders(symbol_);
        render();
    });
}

void StockOwnershipPanel::build_ui() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 8, 10, 8);
    root->setSpacing(6);

    status_ = new QLabel;
    status_->setStyleSheet(QString("color:%1;font-size:12px;").arg(ui::colors::TEXT_SECONDARY()));
    status_->setWordWrap(true);
    root->addWidget(status_);

    stack_ = new QStackedWidget;
    root->addWidget(stack_, 1);

    // ── The stock page: header on top, three panels in a splitter, the whole
    // page in a scroll area ───────────────────────────────────────────────────
    // The splitter keeps every panel on screen with dividers the reader can
    // drag; the scroll area is for the window that is too short for three
    // panels at their minimum — at 1080p with the terminal's chrome, that is
    // every window. The two only work together with one more piece: a table
    // inside a scroll area swallows the wheel, so the event filter below hands
    // the wheel to the page whenever the table under it cannot scroll further.
    stock_page_ = new QWidget;
    auto* page = new QVBoxLayout(stock_page_);
    page->setContentsMargins(0, 0, 8, 0);
    page->setSpacing(8);

    header_ = new OwnershipHeader;
    page->addWidget(header_);
    page->addWidget(rule());

    auto* split = new QSplitter(Qt::Vertical);
    split->setChildrenCollapsible(false);
    split->setHandleWidth(6);

    holders_ = new HoldersTable;
    holders_->setMinimumHeight(120);
    connect(holders_, &HoldersTable::holder_activated, this,
            [this](const QString& cik, const QString& name) { show_filer(cik, name); });
    connect(holders_, &HoldersTable::sort_requested, this, [this](bool by_weight) {
        services::OwnershipService::instance().set_holder_sort(
            symbol_, by_weight ? services::OwnershipService::HolderSort::ByWeight
                               : services::OwnershipService::HolderSort::BySize);
    });
    auto* holders_host = new QWidget;
    auto* hh = new QVBoxLayout(holders_host);
    hh->setContentsMargins(0, 0, 0, 0);
    hh->setSpacing(4);
    hh->addWidget(holders_, 1);

    // Without the index the holders half has one thing to say and one thing
    // to offer. Inline, because a reader in Equity Research may never visit
    // the OWNERSHIP screen where the index otherwise lives.
    no_index_ = new QWidget;
    auto* ni = new QHBoxLayout(no_index_);
    ni->setContentsMargins(0, 0, 0, 0);
    no_index_text_ = new QLabel;
    no_index_text_->setWordWrap(true);
    no_index_text_->setStyleSheet(QString("color:%1;font-size:13px;").arg(ui::colors::TEXT_PRIMARY()));
    ni->addWidget(no_index_text_, 1);
    build_btn_ = new QPushButton(QStringLiteral("BUILD 13F INDEX"));
    build_btn_->setMinimumHeight(30);
    connect(build_btn_, &QPushButton::clicked, this, []() {
        services::OwnershipService::instance().build_index();
    });
    ni->addWidget(build_btn_);
    hh->addWidget(no_index_);
    split->addWidget(holders_host);

    insiders_ = new InsidersPanel;
    insiders_->setMinimumHeight(150);
    split->addWidget(insiders_);

    shorts_ = new ShortInterestPanel;
    shorts_->setMinimumHeight(140);
    split->addWidget(shorts_);
    split->setStretchFactor(0, 5);
    split->setStretchFactor(1, 4);
    split->setStretchFactor(2, 2);
    // Stretch factors only divide what is left once every widget has its size
    // hint, and a populated table hints far taller than a chart; seed the
    // proportions so the page opens balanced.
    split->setSizes({360, 320, 220});
    split->setMinimumHeight(360 + 320 + 220);
    page->addWidget(split, 1);

    scroll_ = new QScrollArea;
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setWidget(stock_page_);
    stack_->addWidget(scroll_);   // 0
    for (auto* area : stock_page_->findChildren<QAbstractScrollArea*>())
        area->viewport()->installEventFilter(this);

    // ── The filer page: one holder's whole book, and the way back ───────────
    filer_page_ = new QWidget;
    auto* fp = new QVBoxLayout(filer_page_);
    fp->setContentsMargins(0, 0, 0, 0);
    fp->setSpacing(6);
    back_btn_ = new QPushButton;
    back_btn_->setCursor(Qt::PointingHandCursor);
    back_btn_->setStyleSheet(QString("QPushButton{color:%1;background:%2;border:1px solid %3;"
                                     "padding:4px 12px;text-align:left;font-size:12px;}"
                                     "QPushButton:hover{color:%4;}")
                                 .arg(ui::colors::TEXT_SECONDARY(), ui::colors::BG_RAISED(),
                                      ui::colors::BORDER_DIM(), ui::colors::TEXT_PRIMARY()));
    connect(back_btn_, &QPushButton::clicked, this, [this]() { show_stock(); });
    auto* bh = new QHBoxLayout;
    bh->addWidget(back_btn_);
    bh->addStretch(1);
    fp->addLayout(bh);
    filer_ = new FirmDetailPanel;
    // A holding in the book is another security: open it in Equity Research
    // rather than nesting a register inside a book inside a register.
    connect(filer_, &FirmDetailPanel::navigate_to_symbol, this, [this](const QString& ticker) {
        emit navigate_to_screen(QStringLiteral("equity_research"), ticker);
    });
    fp->addWidget(filer_, 1);
    stack_->addWidget(filer_page_);   // 1
}

bool StockOwnershipPanel::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::Wheel && scroll_) {
        auto* viewport = qobject_cast<QWidget*>(watched);
        auto* area = viewport ? qobject_cast<QAbstractScrollArea*>(viewport->parentWidget()) : nullptr;
        if (area) {
            auto* we = static_cast<QWheelEvent*>(event);
            auto* bar = area->verticalScrollBar();
            const int dy = we->angleDelta().y();
            const bool can_scroll = bar && bar->maximum() > bar->minimum() &&
                                    ((dy < 0 && bar->value() < bar->maximum()) ||
                                     (dy > 0 && bar->value() > bar->minimum()));
            if (!can_scroll) {
                QWheelEvent forwarded(scroll_->viewport()->mapFromGlobal(we->globalPosition().toPoint()),
                                      we->globalPosition(), we->pixelDelta(), we->angleDelta(),
                                      we->buttons(), we->modifiers(), we->phase(), we->inverted(),
                                      we->source());
                QCoreApplication::sendEvent(scroll_->viewport(), &forwarded);
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void StockOwnershipPanel::set_symbol(const QString& symbol) {
    const QString sym = symbol.trimmed().toUpper();
    if (sym.isEmpty() || sym == symbol_)
        return;
    symbol_ = sym;
    show_stock();
    header_->clear();
    holders_->clear();
    insiders_->clear();
    shorts_->clear();
    services::OwnershipService::instance().load(sym);
    render();
}

void StockOwnershipPanel::show_filer(const QString& cik, const QString& name) {
    back_btn_->setText(QStringLiteral("←  Back to %1's holders").arg(symbol_));
    filer_->set_firm(cik);
    stack_->setCurrentIndex(1);
    status_->setText(QStringLiteral("%1 — disclosed 13F equity holdings").arg(name));
}

void StockOwnershipPanel::show_stock() {
    stack_->setCurrentIndex(0);
    render();
}

void StockOwnershipPanel::render() {
    if (symbol_.isEmpty()) {
        status_->setText(QStringLiteral("Enter a ticker to load its ownership."));
        return;
    }
    if (stack_->currentIndex() == 1)
        return;
    auto& svc = services::OwnershipService::instance();
    const auto snap = svc.snapshot(symbol_);
    const bool loading = svc.is_loading(symbol_);

    QStringList bits;
    bits << (snap.company.isEmpty() ? symbol_ : QStringLiteral("%1 — %2").arg(symbol_, snap.company));
    if (loading)
        bits << QStringLiteral("loading…");
    // Errors elided; the full text is in the tooltip so an unbounded message
    // cannot push the line off the pane.
    QStringList full;
    auto brief = [](const QString& e) { return e.length() > 90 ? e.left(87) + QStringLiteral("…") : e; };
    if (!snap.edgar_error.isEmpty()) {
        bits << QStringLiteral("EDGAR: ") + brief(snap.edgar_error);
        full << QStringLiteral("EDGAR: ") + snap.edgar_error;
    }
    if (!snap.market_error.isEmpty()) {
        bits << QStringLiteral("share data: ") + brief(snap.market_error);
        full << QStringLiteral("Share data: ") + snap.market_error;
    }
    status_->setText(bits.join(QStringLiteral("   ·   ")));
    status_->setToolTip(full.join(QStringLiteral("\n")));

    header_->set_snapshot(snap, loading);
    holders_->set_snapshot(snap, loading);
    insiders_->set_snapshot(snap, loading);
    shorts_->set_snapshot(snap, loading);

    const bool ready = svc.index_ready();
    no_index_->setVisible(!ready);
    holders_->setVisible(ready);
    if (!ready) {
        no_index_text_->setText(QStringLiteral(
            "No 13F index yet. The holder table, the quarter-over-quarter counts, the top-10 "
            "concentration and the short-interest ÷ 13F ratio all come from it: two quarterly SEC "
            "data sets, around 10,600 filers and 3 million positions each, indexed locally once. "
            "About 200 MB, a couple of minutes."));
        build_btn_->setEnabled(!svc.index_busy());
        build_btn_->setText(svc.index_busy() ? QStringLiteral("BUILDING…") : QStringLiteral("BUILD 13F INDEX"));
    }
}

} // namespace fincept::screens
