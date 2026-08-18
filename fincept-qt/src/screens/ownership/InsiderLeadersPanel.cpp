#include "screens/ownership/InsiderLeadersPanel.h"

#include "python/PythonRunner.h"
#include "ui/formatting/NumberFormat.h"
#include "ui/theme/Theme.h"

#include <algorithm>

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace fincept::screens {

namespace fmt = fincept::ui::formatting;

namespace {

QTableWidgetItem* cell(const QString& text, const QString& colour = {}) {
    auto* it = new QTableWidgetItem(text);
    if (!colour.isEmpty())
        it->setForeground(QColor(colour));
    return it;
}

} // namespace

InsiderLeadersPanel::InsiderLeadersPanel(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(4);

    auto* bar = new QHBoxLayout;
    bar->setSpacing(6);

    direction_ = new QComboBox;
    direction_->addItem(QStringLiteral("Insider buying"), QStringLiteral("buy"));
    direction_->addItem(QStringLiteral("Insider selling"), QStringLiteral("sell"));
    direction_->setToolTip(QStringLiteral(
        "Open-market purchases and sales only — code P and S. Grants, option exercises and "
        "shares withheld for tax are compensation mechanics, not decisions about price, and "
        "counting them is what makes an insider screen show buying everywhere forever.\n\n"
        "Buying is the side worth ranking: insiders sell to diversify, to pay tax, on a "
        "schedule set a year ahead. They buy for one reason."));
    connect(direction_, &QComboBox::currentIndexChanged, this, [this]() { reload(); });
    bar->addWidget(direction_);

    window_ = new QComboBox;
    window_->addItem(QStringLiteral("Last 5 days"), 5);
    window_->addItem(QStringLiteral("Last 10 days"), 10);
    window_->addItem(QStringLiteral("Last 30 days"), 30);
    connect(window_, &QComboBox::currentIndexChanged, this, [this]() { reload(); });
    bar->addWidget(window_);

    bar->addStretch(1);

    scan_btn_ = new QPushButton(QStringLiteral("SCAN EDGAR"));
    scan_btn_->setToolTip(QStringLiteral(
        "Read the last few days of Form 4 filings from the EDGAR daily index — every issuer, "
        "not a watchlist. Around 500 filings a day, so the first run takes a few minutes; "
        "afterwards it only fetches what it has not already read.\n\n"
        "This is a daily scan rather than the SEC's quarterly bulk file on purpose: Form 4 is "
        "due within two business days, and that promptness is the whole reason it is worth "
        "reading."));
    connect(scan_btn_, &QPushButton::clicked, this, [this]() { run_scan(); });
    bar->addWidget(scan_btn_);
    root->addLayout(bar);

    status_ = new QLabel;
    status_->setWordWrap(true);
    status_->setStyleSheet(QString("color:%1;font-size:12px;").arg(ui::colors::TEXT_SECONDARY()));
    root->addWidget(status_);

    table_ = new QTableWidget;
    table_->setColumnCount(7);
    table_->setHorizontalHeaderLabels({QStringLiteral("Ticker"), QStringLiteral("Company"),
                                       QStringLiteral("Bought"), QStringLiteral("Insiders"),
                                       QStringLiteral("Stake +"), QStringLiteral("Roles"),
                                       QStringLiteral("Latest")});
    table_->verticalHeader()->setVisible(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(true);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table_->horizontalHeader()->setStretchLastSection(true);
    connect(table_, &QTableWidget::itemSelectionChanged, this, [this]() {
        const int r = table_->currentRow();
        if (r < 0)
            return;
        auto* tick = table_->item(r, 0);
        auto* name = table_->item(r, 1);
        if (tick)
            emit issuer_selected(tick->data(Qt::UserRole).toString(),
                                 name ? name->text() : QString());
    });
    root->addWidget(table_, 1);

    reload();
}

void InsiderLeadersPanel::run_scan() {
    if (scanning_)
        return;
    scanning_ = true;
    scan_btn_->setEnabled(false);

    // Scan the window the reader is looking at, but bounded: a day is around
    // 500 submission fetches, so thirty days is an hour of work against any
    // sane deadline. The scan is incremental, so pressing it again extends the
    // window rather than repeating it, and the status line says so.
    const int want = window_->currentData().toInt();
    const int days = std::min(want, kMaxScanDays);
    status_->setText(
        QStringLiteral("Reading the EDGAR daily index for the last %1 business days — around 500 "
                       "filings a day, each fetched and parsed. Only filings not already read "
                       "are fetched, so pressing this again extends the window.%2")
            .arg(days)
            .arg(days < want
                     ? QStringLiteral(" Up to %1 unread days per press; press again for more.")
                           .arg(kMaxScanDays)
                     : QString()));
    status_->setStyleSheet(QString("color:%1;font-size:12px;").arg(ui::colors::TEXT_SECONDARY()));
    QPointer<InsiderLeadersPanel> self = this;
    const QString payload = QString::fromUtf8(
        QJsonDocument(QJsonObject{{"days", want}, {"max_new_days", kMaxScanDays}})
            .toJson(QJsonDocument::Compact));
    python::PythonRunner::instance().run(
        QStringLiteral("sec_form4_market.py"), {QStringLiteral("scan"), payload},
        [self](python::PythonResult result) {
            if (!self)
                return;
            self->scanning_ = false;
            self->scan_btn_->setEnabled(true);
            if (!result.success) {
                self->status_->setText(QStringLiteral("Scan failed: ") + result.error);
                self->status_->setStyleSheet(
                    QString("color:%1;font-size:12px;").arg(ui::colors::AMBER()));
                return;
            }
            // Report whether the window is now fully covered — the scan reads
            // the oldest unread days first and is bounded per press, so
            // "press again for more" has to be true or absent, never implied.
            const auto root =
                QJsonDocument::fromJson(python::extract_json(result.output).toUtf8()).object();
            const int left = root.value(QStringLiteral("days_remaining")).toInt();
            if (left > 0)
                self->status_->setText(
                    QStringLiteral("Read %1 more day(s). %2 day(s) of the selected window still "
                                   "unread — press SCAN EDGAR again to fetch them.")
                        .arg(root.value(QStringLiteral("days_read")).toInt())
                        .arg(left));
            // Show the ranking straight away, then label the insiders behind
            // it. Classification is a per-owner fetch from EDGAR and would
            // otherwise hold an already-usable table hostage to it.
            self->reload();
            self->classify();
        },
        /*on_line=*/{}, days * 6 * 60 * 1000);
}

void InsiderLeadersPanel::classify() {
    QPointer<InsiderLeadersPanel> self = this;
    const QString payload = QString::fromUtf8(
        QJsonDocument(QJsonObject{{"days", 30}, {"limit", 80}}).toJson(QJsonDocument::Compact));
    python::PythonRunner::instance().run(
        QStringLiteral("sec_form4_market.py"), {QStringLiteral("classify"), payload},
        [self](python::PythonResult result) {
            if (self && result.success)
                self->reload();   // the labels are cached now; re-read with them
        },
        /*on_line=*/{}, 5 * 60 * 1000);
}

void InsiderLeadersPanel::reload() {
    // One interpreter at a time, but the LAST request must win: dropping it
    // outright let the table settle on a window the controls no longer show,
    // and the in-flight callback captured the old direction, so the header
    // could read "Bought" while the selector said selling.
    if (loading_) {
        restack_ = true;
        return;
    }
    loading_ = true;
    restack_ = false;
    QPointer<InsiderLeadersPanel> self = this;
    const int days = window_->currentData().toInt();
    const QString dir = direction_->currentData().toString();
    const QString payload = QString::fromUtf8(
        QJsonDocument(QJsonObject{{"days", days}, {"limit", 60}, {"direction", dir}})
            .toJson(QJsonDocument::Compact));
    python::PythonRunner::instance().run(
        QStringLiteral("sec_form4_market.py"), {QStringLiteral("leaders"), payload},
        [self, dir](python::PythonResult result) {
            if (!self)
                return;
            self->loading_ = false;
            if (self->restack_) {
                self->restack_ = false;
                self->reload();   // the controls moved on while this was in flight
                return;
            }
            self->table_->setRowCount(0);
            if (!result.success) {
                self->status_->setText(result.error);
                return;
            }
            const auto root =
                QJsonDocument::fromJson(python::extract_json(result.output).toUtf8()).object();
            const auto rows = root.value(QStringLiteral("leaders")).toArray();
            const bool buying = dir == QLatin1String("buy");
            self->table_->setHorizontalHeaderLabels(
                {QStringLiteral("Ticker"), QStringLiteral("Company"),
                 buying ? QStringLiteral("Bought") : QStringLiteral("Sold"),
                 QStringLiteral("Insiders"),
                 // The ratio measures a position growing, so the header only
                 // claims it on the buy view.
                 buying ? QStringLiteral("Stake +") : QStringLiteral("—"),
                 QStringLiteral("Roles"), QStringLiteral("Latest")});
            if (rows.isEmpty()) {
                self->status_->setText(QStringLiteral(
                    "Nothing scanned yet. Press SCAN EDGAR to read the last few days of Form 4 "
                    "filings — every issuer, not a watchlist."));
                return;
            }
            self->table_->setRowCount(rows.size());
            self->table_->setUpdatesEnabled(false);
            int clusters = 0;
            for (int i = 0; i < rows.size(); ++i) {
                const auto o = rows[i].toObject();
                const QString sym = o.value(QStringLiteral("symbol")).toString();
                const bool cluster = o.value(QStringLiteral("cluster")).toBool();
                clusters += cluster ? 1 : 0;

                auto* t = cell(sym.isEmpty() ? fmt::placeholder() : sym,
                               sym.isEmpty() ? ui::colors::TEXT_DIM() : ui::colors::CYAN());
                t->setData(Qt::UserRole, sym);
                if (sym.isEmpty())
                    t->setToolTip(QStringLiteral(
                        "This filing did not name a trading symbol, so there is no security to "
                        "open. The company name is as filed."));
                self->table_->setItem(i, 0, t);

                auto* nm = cell(o.value(QStringLiteral("issuer")).toString());
                nm->setToolTip(o.value(QStringLiteral("issuer")).toString());
                self->table_->setItem(i, 1, nm);

                self->table_->setItem(
                    i, 2,
                    cell(fmt::format_compact(o.value(QStringLiteral("value")).toDouble()),
                         buying ? ui::colors::GREEN() : ui::colors::RED()));

                // A cluster is marked, not scored: "three officers bought" and
                // "one officer bought three times" are different events, and
                // folding them into one number decides for the reader.
                const int people = o.value(QStringLiteral("insiders")).toInt();
                const int oppo = o.value(QStringLiteral("opportunistic")).toInt();
                QString ptext = cluster ? QStringLiteral("%1  cluster").arg(people)
                                        : QString::number(people);
                // Cohen, Malloy and Pomorski: an insider who trades the same
                // month every year is following a plan and predicts nothing,
                // while the same trade from someone with no such pattern does.
                // Only shown when the multi-year history exists to support it.
                if (oppo > 0)
                    ptext += QStringLiteral("  ·  %1 opportunistic").arg(oppo);
                auto* pc = cell(ptext, oppo > 0 ? ui::colors::GREEN()
                                                : (cluster ? ui::colors::AMBER() : QString()));
                pc->setToolTip(
                    QStringLiteral("%1 insider(s) · %2 transactions%3%4")
                        .arg(people)
                        .arg(o.value(QStringLiteral("trades")).toInt())
                        .arg(cluster ? QStringLiteral(
                                           "\n\nSeveral insiders acting within days of each "
                                           "other is a materially stronger signal than one "
                                           "person buying.")
                                     : QString())
                        .arg(oppo > 0
                                 ? QStringLiteral(
                                       "\n\n%1 of them trade on no annual pattern — the "
                                       "distinction that carries most of Form 4's signal. "
                                       "Insiders with too little filing history to judge are "
                                       "left unlabelled rather than assumed opportunistic.")
                                       .arg(oppo)
                                 : QString()));
                self->table_->setItem(i, 3, pc);

                // What the purchase did to the insider's OWN position. A $50k
                // buy by a director already holding $50m is noise; the same buy
                // from someone holding $200k is not, and only this ratio
                // separates them.
                const bool stake_applies =
                    o.value(QStringLiteral("stake_applies")).toBool(true);
                const auto stake = o.value(QStringLiteral("stake_increase"));
                auto* st = cell(stake.isDouble()
                                    ? fmt::format_percent(stake.toDouble() * 100.0, 0, true)
                                    : fmt::placeholder(),
                                stake.isDouble() && stake.toDouble() >= 0.25
                                    ? ui::colors::GREEN()
                                    : ui::colors::TEXT_SECONDARY());
                st->setToolTip(
                    !stake_applies
                        ? QStringLiteral("Only meaningful for purchases — this measures a "
                                         "position growing, which a sale does not do.")
                        : (stake.isDouble()
                               ? QStringLiteral("The biggest proportional buy here: one insider "
                                                "grew their own holding by this much. Not "
                                                "necessarily the largest purchase by value.")
                               : QStringLiteral("No filing in this window reported holdings after "
                                                "the trade, so the purchase cannot be sized "
                                                "against the insider's own position.")));
                self->table_->setItem(i, 4, st);

                // Roles arrive once per filer, so three directors buying yields
                // "Director, Director, Director". The distinct set is what the
                // reader wants; the count is already in its own column.
                QStringList roles;
                for (const auto& r : o.value(QStringLiteral("roles")).toArray()) {
                    const QString role = r.toString().trimmed();
                    if (!role.isEmpty() && !roles.contains(role, Qt::CaseInsensitive))
                        roles << role;
                }
                auto* rc = cell(roles.join(QStringLiteral(", ")), ui::colors::TEXT_SECONDARY());
                rc->setToolTip(roles.join(QStringLiteral(", ")));
                self->table_->setItem(i, 5, rc);
                self->table_->setItem(i, 6, cell(o.value(QStringLiteral("latest")).toString(),
                                                 ui::colors::TEXT_SECONDARY()));
            }
            self->table_->setUpdatesEnabled(true);
            self->table_->resizeColumnsToContents();
            // Company and roles are the two unbounded columns; left at content
            // width they push the date — the one column that says whether this
            // is news — off the pane. Both elide with the full text on hover.
            self->table_->setColumnWidth(1, qMin(self->table_->columnWidth(1), 190));
            self->table_->setColumnWidth(5, qMin(self->table_->columnWidth(5), 150));
            self->status_->setText(
                QStringLiteral("%1 issuers · %2 with more than one insider · open-market %3 only, "
                               "grants and option exercises excluded")
                    .arg(rows.size())
                    .arg(clusters)
                    .arg(buying ? QStringLiteral("purchases") : QStringLiteral("sales")));
            self->status_->setStyleSheet(
                QString("color:%1;font-size:12px;").arg(ui::colors::TEXT_SECONDARY()));
        },
        /*on_line=*/{}, 60'000);
}

} // namespace fincept::screens
