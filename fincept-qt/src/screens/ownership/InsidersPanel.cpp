#include "screens/ownership/InsidersPanel.h"

#include "screens/ownership/Form4Dialog.h"
#include "screens/ownership/InsiderTimeline.h"
#include "screens/ownership/OwnershipFlags.h"
#include "screens/ownership/OwnershipUi.h"
#include "ui/components/TooltipText.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cmath>

namespace fincept::screens {

using namespace fincept::ownership;
using namespace fincept::screens::ownership_ui;

namespace {

QString role_short(const QStringList& roles) {
    QStringList out;
    for (const QString& r : roles) {
        if (r.compare(QLatin1String("Director"), Qt::CaseInsensitive) == 0)
            out << QStringLiteral("Dir");
        else if (r.contains(QLatin1String("Chief Executive"), Qt::CaseInsensitive) || r.startsWith(QLatin1String("CEO")))
            out << QStringLiteral("CEO");
        else if (r.contains(QLatin1String("Chief Financial"), Qt::CaseInsensitive) || r.startsWith(QLatin1String("CFO")))
            out << QStringLiteral("CFO");
        else if (r.contains(QLatin1String("General Counsel"), Qt::CaseInsensitive))
            out << QStringLiteral("GC");
        else if (r.contains(QLatin1String("10%")))
            out << QStringLiteral("10% owner");
        else if (r.length() > 18)
            out << r.left(16) + QStringLiteral("…");
        else
            out << r;
    }
    out.removeDuplicates();
    return out.join(QStringLiteral(", "));
}

} // namespace

InsidersPanel::InsidersPanel(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    auto title = section_title(QStringLiteral("INSIDERS — FORM 4"));
    title_note_ = title.right;
    auto* bar = new QHBoxLayout;
    bar->addWidget(title.widget, 1);
    show_all_ = new QCheckBox(QStringLiteral("show grants, exercises and withholding"));
    show_all_->setStyleSheet(QString("color:%1;font-size:12px;").arg(ui::colors::TEXT_SECONDARY()));
    connect(show_all_, &QCheckBox::toggled, this, [this](bool) { render(); });
    bar->addWidget(show_all_);
    root->addLayout(bar);

    timeline_ = new InsiderTimeline;
    timeline_->setMinimumHeight(64);
    timeline_->setMaximumHeight(96);
    root->addWidget(timeline_);

    table_ = make_table({QStringLiteral("Traded"), QStringLiteral("Filed"), QStringLiteral("Insider"),
                         QStringLiteral("Role"), QStringLiteral("Action"), QStringLiteral("Shares"),
                         QStringLiteral("Price"), QStringLiteral("Value"), QStringLiteral("Held after"),
                         QStringLiteral("10b5-1"), QStringLiteral("1w"), QStringLiteral("1m"),
                         QStringLiteral("3m")});
    table_->setMinimumHeight(80);
    connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        auto* it = table_->item(row, 0);
        if (!it)
            return;
        // The filing's URL is raw XML; every transaction on that accession is
        // already parsed, so the filing is rendered here with EDGAR one click away.
        const QString url = it->data(Qt::UserRole).toString();
        QVector<InsiderTransaction> filing;
        for (const auto& t : snap_.transactions)
            if (t.source_url == url)
                filing.push_back(t);
        if (filing.isEmpty())
            return;
        Form4Dialog dlg(filing, snap_.company.isEmpty() ? snap_.symbol : snap_.company, this);
        dlg.exec();
    });
    root->addWidget(table_, 1);

    foot_ = note_label();
    root->addWidget(foot_);
    clear();
}

void InsidersPanel::clear() {
    snap_ = {};
    loading_ = false;
    table_->setRowCount(0);
    timeline_->set_transactions({}, {});
    timeline_->set_empty_text({});
    title_note_->clear();
    foot_->clear();
}

void InsidersPanel::set_snapshot(const OwnershipSnapshot& snap, bool loading) {
    snap_ = snap;
    loading_ = loading;
    render();
}

void InsidersPanel::render() {
    const auto& s = snap_;
    if (!s.edgar_ok) {
        table_->setRowCount(0);
        timeline_->set_transactions({}, {});
        const bool pending = loading_ && s.edgar_error.isEmpty();
        timeline_->set_empty_text(pending ? QStringLiteral("Reading Form 4 filings from EDGAR — each "
                                                            "filing is its own document, so a busy "
                                                            "issuer takes a minute.")
                                  : s.edgar_error.isEmpty() ? QString()
                                                            : QStringLiteral("EDGAR: ") + s.edgar_error);
        title_note_->setText(pending ? QStringLiteral("loading…") : QString());
        foot_->clear();
        return;
    }

    const auto clusters = find_cluster_buys(s.transactions);
    timeline_->set_transactions(s.transactions, clusters);
    int open_market = 0;
    for (const auto& t : s.transactions)
        if (t.open_market && !t.derivative) ++open_market;
    timeline_->set_empty_text(
        s.transactions.isEmpty()
            ? QStringLiteral("No Form 4 filings in the last %1 months.").arg(s.window_months)
            : QStringLiteral("%1 filings, none of them open-market buys or sells — all grants, "
                             "option exercises or tax withholding.").arg(s.transactions.size()));

    QString note = QStringLiteral("%1 months · %2 open-market of %3 rows")
                       .arg(s.window_months).arg(open_market).arg(s.transactions.size());
    if (!clusters.isEmpty()) {
        const auto& c = clusters.first();
        note += QStringLiteral(" · <span style='color:%1;'>cluster: %2 insiders bought %3–%4</span>")
                    .arg(ui::colors::AMBER())
                    .arg(c.insiders.size())
                    .arg(c.start.toString(QStringLiteral("d MMM")), c.end.toString(QStringLiteral("d MMM")));
    }
    title_note_->setText(note);

    const bool all = show_all_->isChecked();
    QVector<const InsiderTransaction*> rows;
    for (const auto& t : s.transactions)
        if (all || (t.open_market && !t.derivative))
            rows.push_back(&t);

    table_->setUpdatesEnabled(false);
    table_->setRowCount(rows.size());
    for (int i = 0; i < rows.size(); ++i) {
        const auto& t = *rows[i];
        // Only decisions carry a direction colour; a 10% owner's or a plan
        // buy is a decision the evidence says not to score, so it is dimmed.
        QString colour;
        if (t.open_market && !t.derivative)
            colour = t.acquired ? (t.scorable_buy() ? ui::colors::GREEN() : ui::colors::TEXT_SECONDARY())
                                : ui::colors::RED();
        else
            colour = ui::colors::TEXT_SECONDARY();

        auto* traded = cell(date_or_dash(t.date), colour);
        traded->setData(Qt::UserRole, t.source_url);
        traded->setToolTip(ui::tooltip_wrap(QStringLiteral("Double-click to read the filing")));
        table_->setItem(i, 0, traded);
        table_->setItem(i, 1, cell(date_or_dash(t.filed_date), ui::colors::TEXT_SECONDARY()));
        auto* who = cell(t.insider);
        who->setToolTip(ui::tooltip_wrap(t.roles.isEmpty() ? t.insider
                                                           : t.insider + QStringLiteral(" — ") +
                                                                 t.roles.join(QStringLiteral(", "))));
        table_->setItem(i, 2, who);
        table_->setItem(i, 3, cell(role_short(t.roles), ui::colors::TEXT_SECONDARY()));

        QString action = t.code_label.isEmpty() ? t.code : t.code_label;
        if (t.open_market && !t.derivative)
            action = t.acquired ? QStringLiteral("Buy") : QStringLiteral("Sell");
        else if (t.derivative)
            action += QStringLiteral(" (deriv)");
        auto* act = cell(action, colour);
        if (t.acquired && t.ten_percent_owner && t.open_market)
            act->setToolTip(ui::tooltip_wrap(QStringLiteral(
                "Bought by a 10% beneficial owner — a holder, not an insider in the sense the "
                "evidence is about. Shown, not scored: their purchase ranking inverts.")));
        table_->setItem(i, 4, act);

        table_->setItem(i, 5, num_cell(compact_or_dash(t.shares), colour));
        table_->setItem(i, 6, num_cell(t.price ? fmt::format_money(*t.price) : fmt::placeholder()));
        table_->setItem(i, 7, num_cell(compact_or_dash(t.value), colour));
        table_->setItem(i, 8, num_cell(compact_or_dash(t.shares_held_after), ui::colors::TEXT_SECONDARY()));

        auto* plan = cell(!t.plan_10b5_1 ? fmt::placeholder()
                                         : *t.plan_10b5_1 ? QStringLiteral("plan") : QStringLiteral("no"),
                          ui::colors::TEXT_SECONDARY(), Qt::AlignCenter);
        plan->setToolTip(ui::tooltip_wrap(
            !t.plan_10b5_1 ? QStringLiteral("The 10b5-1 checkbox exists on filings since 2023; this one predates it.")
                           : *t.plan_10b5_1
                                 ? QStringLiteral("Made under a written 10b5-1 plan — decided months before it printed, so not scored.")
                                 : QStringLiteral("Not a plan trade.")));
        table_->setItem(i, 9, plan);

        auto ret = [&](const std::optional<double>& v, const QDate& due) {
            auto* it = num_cell(v ? pct_or_dash(v, 1, true) : fmt::placeholder(), signed_colour(v));
            if (!v) {
                it->setToolTip(ui::tooltip_wrap(
                    !t.open_market || t.derivative
                        ? QStringLiteral("Returns are computed for open-market trades only.")
                        : due > QDate::currentDate()
                              ? QStringLiteral("Not yet — the window ends %1.").arg(due.toString(QStringLiteral("d MMM")))
                              : s.returns_ok ? QStringLiteral("No close on file for this window.")
                                             : QStringLiteral("Price history not loaded yet.")));
            }
            return it;
        };
        table_->setItem(i, 10, ret(t.ret_1w, t.date.addDays(7)));
        table_->setItem(i, 11, ret(t.ret_1m, t.date.addMonths(1)));
        table_->setItem(i, 12, ret(t.ret_3m, t.date.addMonths(3)));
    }
    table_->setUpdatesEnabled(true);
    table_->resizeColumnsToContents();
    table_->setColumnWidth(2, qMin(qMax(table_->columnWidth(2), 150), 220));

    QStringList notes;
    if (!all && s.transactions.size() > open_market)
        notes << QStringLiteral("%1 grants, exercises and withholding rows hidden")
                     .arg(s.transactions.size() - open_market);
    if (s.filings_truncated > 0)
        notes << QStringLiteral("%1 older filings not fetched — the most recent slice, not the whole window")
                     .arg(s.filings_truncated);
    if (s.insider_rows_filed_as_owner > 0) {
        QString where = s.insider_other_issuers.mid(0, 3).join(QStringLiteral(", "));
        if (s.insider_other_issuers.size() > 3)
            where += QStringLiteral(" and %1 more").arg(s.insider_other_issuers.size() - 3);
        notes << QStringLiteral("%1 rows this company filed about %2 — shown on those companies")
                     .arg(s.insider_rows_filed_as_owner)
                     .arg(where.isEmpty() ? QStringLiteral("other issuers") : where);
    }
    notes << QStringLiteral("1w / 1m / 3m: the stock since the trade date, shown after the fact — never a forecast");
    foot_->setText(notes.join(QStringLiteral(" · ")));
}

} // namespace fincept::screens
