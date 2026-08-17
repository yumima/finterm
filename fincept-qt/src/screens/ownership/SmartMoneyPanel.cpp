#include "screens/ownership/SmartMoneyPanel.h"

#include "screens/ownership/HoldersChart.h"
#include "services/ownership/OwnershipService.h"
#include "ui/formatting/NumberFormat.h"
#include "ui/theme/Theme.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace fincept::screens {

using namespace fincept::ownership;
namespace fmt = fincept::ui::formatting;

SmartMoneyPanel::SmartMoneyPanel(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(4);

    auto* bar = new QHBoxLayout;
    bar->setSpacing(6);

    // Ranking is the one control here, and it is a two-item combo rather than a
    // hidden default: "largest holders" is what Bloomberg's HDS shows, and
    // "highest conviction" is the question a weight actually answers. Both are
    // legitimate and the reader should not have to guess which they are seeing.
    sort_ = new QComboBox;
    sort_->addItem(QStringLiteral("Highest conviction (% of their book)"),
                   static_cast<int>(Sort::Weight));
    sort_->addItem(QStringLiteral("Largest position ($)"), static_cast<int>(Sort::Value));
    sort_->setToolTip(QStringLiteral(
        "Conviction ranks by how much of the manager's own book this is. Largest ranks by "
        "dollars held, which surfaces the index complexes."));
    connect(sort_, &QComboBox::currentIndexChanged, this, [this](int) { render(); });
    bar->addWidget(sort_);

    // The index button lives here too, not only on the OWNERSHIP screen. This
    // panel is embedded in Equity Research, where a reader who hits "no 13F
    // index built" previously had no way to act on it from where they were
    // standing — they had to know the control existed on another screen.
    build_btn_ = new QPushButton(QStringLiteral("BUILD 13F INDEX"));
    build_btn_->setToolTip(QStringLiteral(
        "Download two quarterly SEC 13F data sets — every filer, every position — and index "
        "them locally. About 200 MB and a couple of minutes; runs once."));
    connect(build_btn_, &QPushButton::clicked, this, [this]() {
        auto& svc = services::OwnershipService::instance();
        // Once a complete index exists the useful action changes: the bulk data
        // sets lag a quarter, so what is missing is the current quarter, and
        // that comes from EDGAR directly.
        if (svc.index_ready())
            svc.pull_current_quarter();
        else
            svc.build_index();
        render();
    });
    bar->addWidget(build_btn_);

    status_ = new QLabel;
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    status_->setWordWrap(true);
    bar->addWidget(status_, 1);
    root->addLayout(bar);

    flow_ = new QLabel;
    flow_->setTextFormat(Qt::RichText);
    root->addWidget(flow_);

    chart_ = new RankedBarChart;
    chart_->set_empty_text(QStringLiteral("No 13F index built yet."));
    root->addWidget(chart_, 1);

    caveat_ = new QLabel(QStringLiteral(
        "13F: long US equities only, filed 45 days after quarter end. A weight is a share of "
        "the manager's disclosed equity book, not of their fund."));
    caveat_->setWordWrap(true);
    caveat_->setStyleSheet(QString("color:%1;font-size:12px;").arg(ui::colors::TEXT_SECONDARY()));
    root->addWidget(caveat_);

    connect(&services::OwnershipService::instance(),
            &services::OwnershipService::snapshot_updated, this, [this](const QString& sym) {
                if (sym.compare(symbol_, Qt::CaseInsensitive) == 0)
                    render();
            });
    connect(&services::OwnershipService::instance(),
            &services::OwnershipService::index_changed, this, [this](const QString&) {
                if (!symbol_.isEmpty())
                    services::OwnershipService::instance().load_smart_money(symbol_);
                render();
            });
    render();
}

void SmartMoneyPanel::set_symbol(const QString& symbol, bool /*auto_fetch*/) {
    const QString sym = symbol.trimmed().toUpper();
    if (sym == symbol_)
        return;
    symbol_ = sym;
    // Always fetch. Against the local index this is a ~20ms SQLite query, so
    // there is nothing to defer and no reason to make the user press a button —
    // which is what previously left this reading "Not loaded for AAPL".
    if (!sym.isEmpty())
        services::OwnershipService::instance().load_smart_money(sym);
    render();
}

void SmartMoneyPanel::render() {
    auto& svc = services::OwnershipService::instance();
    // Only offered when it is the thing to do; once an index exists it is
    // clutter, and OWNERSHIP owns the incremental controls.
    build_btn_->setText(svc.index_ready() ? QStringLiteral("PULL CURRENT QUARTER")
                                          : QStringLiteral("BUILD 13F INDEX"));
    build_btn_->setToolTip(
        svc.index_ready()
            ? QStringLiteral("SEC's bulk data sets publish a quarter late. This reads the newest "
                             "filings for the largest filers straight from EDGAR.")
            : QStringLiteral("Download two quarterly SEC 13F data sets — every filer, every "
                             "position — and index them locally. Runs once."));
    build_btn_->setVisible(true);
    build_btn_->setEnabled(!svc.index_busy());
    sort_->setVisible(svc.index_ready());
    if (symbol_.isEmpty()) {
        status_->setText(QStringLiteral("No symbol selected."));
        chart_->set_bars({});
        return;
    }

    // Cleared up front, then repopulated only on the success path. It used to
    // be hidden at the BOTTOM of this function, which four early returns never
    // reach — so switching symbols left the previous ticker's buy/trim/exit
    // counts sitting under the new ticker's status line.
    flow_->clear();
    flow_->hide();

    const auto snap = svc.snapshot(symbol_);
    const auto& rows = snap.smart_money;

    if (!snap.smart_money_error.isEmpty()) {
        status_->setText(snap.smart_money_error);
        status_->setStyleSheet(QString("color:%1;").arg(ui::colors::AMBER()));
        chart_->set_bars({});
        chart_->set_empty_text(
            svc.index_ready()
                ? snap.smart_money_error
                : QStringLiteral("No 13F index yet.\n\nPress BUILD 13F INDEX above: it downloads "
                                 "two quarters of SEC filings — around 10,600 filers and 2.4m "
                                 "positions each — so every ticker gets its complete holder "
                                 "list and its quarter-over-quarter changes."));
        return;
    }
    if (svc.is_loading(symbol_) && !snap.smart_money_ok) {
        status_->setText(QStringLiteral("Reading the 13F index…"));
        status_->setStyleSheet(QString("color:%1;").arg(ui::colors::TEXT_SECONDARY()));
        return;
    }
    if (!snap.smart_money_ok) {
        status_->setText(svc.index_ready()
                             ? svc.index_status_text()
                             : QStringLiteral("No institutional holder data yet. Build the 13F "
                                              "index once — every filer, every position — and "
                                              "this fills in for every ticker."));
        status_->setStyleSheet(QString("color:%1;").arg(ui::colors::TEXT_SECONDARY()));
        chart_->set_bars({});
        return;
    }

    // Stock lines only. An option line is a different instrument and a put is a
    // bearish position; mixing them into a holder ranking would report a short
    // as a long.
    QVector<ManagerPosition> stock;
    for (const auto& p : rows)
        if (!p.is_derivative)
            stock.push_back(p);

    const auto mode = static_cast<Sort>(sort_->currentData().toInt());
    std::stable_sort(stock.begin(), stock.end(),
                     [mode](const ManagerPosition& a, const ManagerPosition& b) {
                         return mode == Sort::Weight ? (a.weight.value_or(0) > b.weight.value_or(0))
                                                     : (a.value.value_or(0) > b.value.value_or(0));
                     });

    QString head = QStringLiteral("%1 institutional holders")
                       .arg(snap.holder_universe ? snap.holder_universe : stock.size());
    if (snap.index_quarter.isValid())
        head += QStringLiteral(" · as of ") + snap.index_quarter.toString(QStringLiteral("MMM yyyy"));
    if (snap.prior_quarter.isValid())
        head += QStringLiteral(" · vs ") + snap.prior_quarter.toString(QStringLiteral("MMM yyyy"));
    if (snap.option_holders > 0)
        head += QStringLiteral(" · %1 hold options only").arg(snap.option_holders);
    status_->setText(head);
    // The net direction of the whole register, counted across every filer —
    // not across the rows on screen, which are the top of a ranking and would
    // give a badly skewed count.
    // A newer quarter exists but is partial — say so on the number rather than
    // quietly serving it, and offer the one action that helps.
    if (snap.partial_quarter.isValid()) {
        head += QStringLiteral("  ·  %1 filed but only %2 large filers pulled so far")
                    .arg(snap.partial_quarter.toString(QStringLiteral("MMM yyyy")))
                    .arg(snap.partial_filers);
        status_->setText(head);
    }
    if (snap.prior_quarter.isValid() && (snap.buyers || snap.sellers || snap.exited)) {
        flow_->setText(QStringLiteral(
            "<span style='color:%1;'>%2 bought</span>  ·  "
            "<span style='color:%3;'>%4 trimmed</span>  ·  "
            "<span style='color:%3;'>%5 exited</span>")
                           .arg(ui::colors::GREEN()).arg(snap.buyers)
                           .arg(ui::colors::RED()).arg(snap.sellers).arg(snap.exited));
        flow_->show();
    }
    status_->setStyleSheet(QString("color:%1;").arg(ui::colors::TEXT_PRIMARY()));

    // Bars are scaled to the largest in view so the ranking is legible even
    // when every weight is small — an absolute 0..1 scale would render a set of
    // 1% positions as a column of empty tracks.
    double top = 0.0;
    for (const auto& p : stock)
        top = std::max(top, mode == Sort::Weight ? p.weight.value_or(0) : p.value.value_or(0));

    QVector<RankedBar> bars;
    for (const auto& p : stock) {
        const double metric = mode == Sort::Weight ? p.weight.value_or(0) : p.value.value_or(0);
        RankedBar b;
        b.label = p.manager;
        b.value_text = mode == Sort::Weight
                           ? (p.weight ? fmt::format_percent(*p.weight * 100.0, 1)
                                       : fmt::placeholder())
                           : (p.value ? fmt::format_compact(*p.value) : fmt::placeholder());
        b.fraction = top > 0 ? metric / top : 0.0;
        // A large weight is the thing worth looking at, so it carries the
        // attention colour; everything else stays neutral rather than every row
        // shouting.
        b.colour = (p.weight.value_or(0) >= 0.05) ? QColor(ui::colors::AMBER())
                                                  : QColor(ui::colors::CYAN());
        // Colour by what they DID, not by how big it is: green built, red cut,
        // neutral held. That is the question a reader brings to this table.
        if (p.action == QLatin1String("added") || p.action == QLatin1String("new"))
            b.colour = QColor(ui::colors::GREEN());
        else if (p.action == QLatin1String("trimmed") || p.action == QLatin1String("exited"))
            b.colour = QColor(ui::colors::RED());
        if (!p.action.isEmpty()) {
            QString move = p.action;
            if (p.shares_delta && *p.shares_delta != 0.0)
                move += QStringLiteral(" %1%2")
                            .arg(*p.shares_delta > 0 ? QStringLiteral("+") : QStringLiteral("-"),
                                 fmt::format_compact(std::abs(*p.shares_delta)));
            b.label += QStringLiteral("   ·   ") + move;
        }
        b.tooltip = QStringLiteral("%1\n%2 shares · %3\n%4 of a %5 book across %6 positions")
                        .arg(p.manager,
                             p.shares ? fmt::format_compact(*p.shares) : fmt::placeholder(),
                             p.value ? fmt::format_money(*p.value) : fmt::placeholder(),
                             p.weight ? fmt::format_percent(*p.weight * 100.0, 2)
                                      : fmt::placeholder(),
                             p.book_total ? fmt::format_compact(*p.book_total)
                                          : fmt::placeholder())
                        .arg(p.position_count);
        if (!p.note.isEmpty())
            b.tooltip += QStringLiteral("\n\n\u26a0 ") + p.note;
        else if (!p.action.isEmpty())
            b.tooltip += QStringLiteral("\n\n%1 since %2").arg(p.action).arg(
                snap.prior_quarter.isValid()
                    ? snap.prior_quarter.toString(QStringLiteral("MMM yyyy"))
                    : QStringLiteral("the prior quarter"));
        bars.push_back(b);
    }
    chart_->set_bars(bars);
    chart_->set_empty_text(
        QStringLiteral("No institutional holder of %1 in the indexed quarter.").arg(symbol_));
}

} // namespace fincept::screens
