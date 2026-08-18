#include "screens/ownership/InsiderLeadersPanel.h"

#include "python/PythonRunner.h"
#include "ui/formatting/NumberFormat.h"
#include "ui/theme/Theme.h"

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
    table_->setColumnCount(6);
    table_->setHorizontalHeaderLabels({QStringLiteral("Ticker"), QStringLiteral("Company"),
                                       QStringLiteral("Bought"), QStringLiteral("Insiders"),
                                       QStringLiteral("Roles"), QStringLiteral("Latest")});
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
    status_->setText(QStringLiteral("Reading the EDGAR daily index… around 500 filings a day, "
                                    "each one fetched and parsed."));
    QPointer<InsiderLeadersPanel> self = this;
    const QString payload = QString::fromUtf8(
        QJsonDocument(QJsonObject{{"days", 3}}).toJson(QJsonDocument::Compact));
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
            self->reload();
        },
        /*on_line=*/{}, 15 * 60 * 1000);
}

void InsiderLeadersPanel::reload() {
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
                 QStringLiteral("Insiders"), QStringLiteral("Roles"), QStringLiteral("Latest")});
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
                auto* pc = cell(cluster ? QStringLiteral("%1  cluster").arg(people)
                                        : QString::number(people),
                                cluster ? ui::colors::AMBER() : QString());
                pc->setToolTip(cluster
                                   ? QStringLiteral("%1 separate insiders bought in this window. "
                                                    "Several insiders acting within days of each "
                                                    "other is a materially stronger signal than "
                                                    "one person buying.")
                                         .arg(people)
                                   : QStringLiteral("%1 insider · %2 transactions")
                                         .arg(people)
                                         .arg(o.value(QStringLiteral("trades")).toInt()));
                self->table_->setItem(i, 3, pc);

                // Roles arrive once per filer, so three directors buying yields
                // "Director, Director, Director". The distinct set is what the
                // reader wants; the count is already in its own column.
                QStringList roles;
                for (const auto& r : o.value(QStringLiteral("roles")).toArray()) {
                    const QString role = r.toString().trimmed();
                    if (!role.isEmpty() && !roles.contains(role, Qt::CaseInsensitive))
                        roles << role;
                }
                self->table_->setItem(i, 4, cell(roles.join(QStringLiteral(", ")),
                                                 ui::colors::TEXT_SECONDARY()));
                self->table_->setItem(i, 5, cell(o.value(QStringLiteral("latest")).toString(),
                                                 ui::colors::TEXT_SECONDARY()));
            }
            self->table_->setUpdatesEnabled(true);
            self->table_->resizeColumnsToContents();
            self->table_->setColumnWidth(1, qMin(self->table_->columnWidth(1), 200));
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

void InsiderLeadersPanel::render() {}

} // namespace fincept::screens
