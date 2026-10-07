#include "screens/ownership/OwnershipHeader.h"

#include "screens/ownership/OwnershipFlags.h"
#include "screens/ownership/OwnershipUi.h"
#include "ui/components/TooltipText.h"

#include <QGridLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace fincept::screens {

using namespace fincept::ownership;
using namespace fincept::screens::ownership_ui;

namespace {

QString esc(const QString& s) { return s.toHtmlEscaped(); }

QString span(const QString& text, const QString& colour) {
    return QStringLiteral("<span style='color:%1;'>%2</span>").arg(colour, text);
}

QString dim(const QString& text) { return span(text, ui::colors::TEXT_SECONDARY()); }

QString short_date(const QDate& d) { return d.toString(QStringLiteral("d MMM yyyy")); }

QString quarter_label(const QDate& q) {
    if (!q.isValid())
        return {};
    return QStringLiteral("Q%1 %2").arg((q.month() - 1) / 3 + 1).arg(q.year());
}

} // namespace

OwnershipHeader::OwnershipHeader(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    grid_ = new QGridLayout;
    grid_->setContentsMargins(0, 0, 0, 0);
    grid_->setHorizontalSpacing(18);
    grid_->setVerticalSpacing(3);
    grid_->setColumnStretch(1, 5);
    grid_->setColumnStretch(2, 4);
    root->addLayout(grid_);

    institutions_ = add_row(QStringLiteral("INSTITUTIONS"));
    insiders_     = add_row(QStringLiteral("INSIDERS"));
    top10_        = add_row(QStringLiteral("TOP 10 HOLDERS"));
    change_       = add_row(QStringLiteral("QUARTER CHANGE"));
    shorts_       = add_row(QStringLiteral("SHORT"));
    stakes_       = add_row(QStringLiteral("5% STAKES"));

    flags_ = new QVBoxLayout;
    flags_->setContentsMargins(0, 4, 0, 0);
    flags_->setSpacing(4);
    root->addLayout(flags_);

    none_ = new QLabel;
    none_->setWordWrap(true);
    none_->setStyleSheet(QString("color:%1;font-size:12px;").arg(ui::colors::TEXT_SECONDARY()));
    flags_->addWidget(none_);
    clear();
}

OwnershipHeader::Row OwnershipHeader::add_row(const QString& key) {
    Row r;
    const int row = grid_->rowCount();
    r.key = new QLabel(key);
    r.key->setStyleSheet(QString("color:%1;font-size:11px;letter-spacing:1px;font-weight:600;")
                             .arg(ui::colors::TEXT_SECONDARY()));
    r.key->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    r.value = new QLabel;
    r.value->setWordWrap(true);
    r.value->setTextFormat(Qt::RichText);
    r.value->setStyleSheet(QString("color:%1;font-size:13px;").arg(ui::colors::TEXT_PRIMARY()));
    r.value->setTextInteractionFlags(Qt::TextSelectableByMouse);
    r.note = new QLabel;
    r.note->setWordWrap(true);
    r.note->setTextFormat(Qt::RichText);
    r.note->setStyleSheet(QString("color:%1;font-size:12px;").arg(ui::colors::TEXT_SECONDARY()));
    grid_->addWidget(r.key, row, 0);
    grid_->addWidget(r.value, row, 1);
    grid_->addWidget(r.note, row, 2);
    return r;
}

void OwnershipHeader::clear() {
    for (Row* r : {&institutions_, &insiders_, &top10_, &change_, &shorts_, &stakes_}) {
        r->value->setText(fmt::placeholder());
        r->note->clear();
    }
    for (auto* w : flag_widgets_)
        delete w;
    flag_widgets_.clear();
    none_->setText(QStringLiteral("Enter a ticker to load its ownership."));
    none_->show();
}

void OwnershipHeader::set_snapshot(const OwnershipSnapshot& s, bool loading) {
    const HeaderStats h = derive_header(s);
    const auto& sum = s.summary;
    const QString wait = loading ? dim(QStringLiteral("loading…")) : dim(fmt::placeholder());

    // ── Institutions ────────────────────────────────────────────────────────
    {
        QStringList parts;
        if (h.inst_pct_out)
            parts << QStringLiteral("<b>%1</b> of shares out").arg(pct_or_dash(h.inst_pct_out, 0));
        if (h.inst_pct_float)
            parts << QStringLiteral("<b>%1</b> of float").arg(pct_or_dash(h.inst_pct_float, 0));
        QString v = parts.join(QStringLiteral(" · "));
        if (h.vendor_inst_pct) {
            // With the filings in hand the vendor's number is a cross-check;
            // without them it is the only number, and says so.
            v += v.isEmpty()
                     ? QStringLiteral("<b>%1</b> of float ").arg(pct_or_dash(h.vendor_inst_pct, 1)) +
                           dim(QStringLiteral("(vendor figure — 13F holders not loaded)"))
                     : QStringLiteral(" ") + dim(QStringLiteral("(vendor %1)").arg(pct_or_dash(h.vendor_inst_pct, 1)));
        }
        if (v.isEmpty())
            v = s.holders_ok || s.market_ok ? dim(fmt::placeholder()) : wait;
        institutions_.value->setText(v);

        QString n;
        if (s.holders_ok) {
            n = QStringLiteral("%1 filers · %2 · filed by %3")
                    .arg(QLocale().toString(sum.holder_count), quarter_label(sum.quarter),
                         short_date(sum.filed_by()));
            if (sum.min_book_value > 0.0 || sum.min_book_positions > 0)
                n += QStringLiteral(" · books ≥ $%1 and ≥ %2 positions only")
                         .arg(fmt::format_compact(sum.min_book_value))
                         .arg(sum.min_book_positions);
            if (sum.partial_quarter.isValid())
                n += QStringLiteral(" · %1 filers already in for %2")
                         .arg(sum.partial_filers).arg(quarter_label(sum.partial_quarter));
            n += QStringLiteral(" · positions up to 135 days old");
        } else if (!s.holders_error.isEmpty()) {
            n = QStringLiteral("13F holders: ") + esc(s.holders_error.left(120));
        } else {
            n = QStringLiteral("Computed from 13F filings in the local index whose equity book "
                               "passes a size and position-count floor — a floor, since smaller "
                               "books and filers below the 13F threshold do not appear.");
        }
        institutions_.note->setText(n);
        institutions_.note->setToolTip(ui::tooltip_wrap(QStringLiteral(
            "Institutional shares summed from the filings of filers whose equity book passes "
            "the size and position-count floors noted below, divided by the vendor's share "
            "count (and float). The vendor's own percentage is shown beside it for comparison; "
            "it is institutions ÷ float in practice and the two can differ by a quarter of drift.")));
    }

    // ── Insiders ────────────────────────────────────────────────────────────
    {
        QString v = h.insiders_pct ? QStringLiteral("<b>%1</b> of shares out")
                                         .arg(pct_or_dash(h.insiders_pct, 1))
                                   : (s.market_ok ? dim(fmt::placeholder()) : wait);
        insiders_.value->setText(v);
        QString n;
        if (s.edgar_ok) {
            n = QStringLiteral("Form 4, %1 months: %2 · %3")
                    .arg(kHeaderWindowMonths)
                    .arg(h.insider_buys > 0
                             ? span(QStringLiteral("%1 buy%2 ($%3)")
                                        .arg(h.insider_buys)
                                        .arg(h.insider_buys == 1 ? QString() : QStringLiteral("s"),
                                             fmt::format_compact(h.insider_buy_value)),
                                    ui::colors::GREEN())
                             : QStringLiteral("0 open-market buys"),
                         h.insider_sells > 0
                             ? span(QStringLiteral("%1 sell%2 ($%3)")
                                        .arg(h.insider_sells)
                                        .arg(h.insider_sells == 1 ? QString() : QStringLiteral("s"),
                                             fmt::format_compact(h.insider_sell_value)),
                                    ui::colors::RED())
                             : QStringLiteral("0 sells"));
        } else if (!s.edgar_error.isEmpty()) {
            n = QStringLiteral("EDGAR: ") + esc(s.edgar_error.left(120));
        } else {
            n = dim(QStringLiteral("reading Form 4 filings from EDGAR…"));
        }
        insiders_.note->setText(n);
        insiders_.note->setToolTip(ui::tooltip_wrap(QStringLiteral(
            "Open-market transactions only (codes P and S). Buys exclude 10% owners and "
            "10b5-1 plan trades — the two exclusions the evidence turns on. Sells are counted "
            "but never scored: insiders sell for a dozen reasons and buy for one.")));
    }

    // ── Top 10 ──────────────────────────────────────────────────────────────
    {
        QString v;
        if (h.top10_share)
            v = QStringLiteral("<b>%1</b> of institutional value").arg(pct_or_dash(h.top10_share, 0));
        if (h.top10_pct_out)
            v += QStringLiteral(" · <b>%1</b> of the company").arg(pct_or_dash(h.top10_pct_out, 0));
        top10_.value->setText(v.isEmpty() ? (s.holders_ok ? dim(fmt::placeholder()) : wait) : v);
        top10_.note->setText(
            h.broad_share ? QStringLiteral("index and broad books hold %1 of institutional value")
                                .arg(pct_or_dash(h.broad_share, 0))
                          : QString());
        top10_.note->setToolTip(ui::tooltip_wrap(QStringLiteral(
            "Concentration, over every filer. A broad book (a thousand or more names) or a named "
            "passive complex rebalances rather than decides; the share of value they hold says how "
            "much of the register moves with index flow.")));
    }

    // ── Quarter change ──────────────────────────────────────────────────────
    if (s.holders_ok && sum.prior_quarter.isValid()) {
        change_.value->setText(QStringLiteral("%1 · %2 · %3 new · %4 closed")
                                   .arg(span(QStringLiteral("%1 added").arg(QLocale().toString(sum.buyers)),
                                             ui::colors::GREEN()),
                                        span(QStringLiteral("%1 reduced").arg(QLocale().toString(sum.sellers)),
                                             ui::colors::RED()),
                                        QLocale().toString(sum.new_holders),
                                        QLocale().toString(sum.exited)));
        change_.note->setText(QStringLiteral("%1 vs %2 · counts of filers, not shares · %3 shares left with the closers")
                                  .arg(quarter_label(sum.quarter), quarter_label(sum.prior_quarter),
                                       fmt::format_compact(sum.exited_shares)));
    } else {
        change_.value->setText(s.holders_ok ? dim(QStringLiteral("one quarter indexed — no comparison"))
                                            : wait);
        change_.note->clear();
    }
    change_.note->setToolTip(ui::tooltip_wrap(QStringLiteral(
        "Bloomberg OWN's buyers/sellers count. A filer that stopped filing has not sold and "
        "one that started has not bought — only filers present in both quarters are compared.")));

    // ── Short ───────────────────────────────────────────────────────────────
    {
        QStringList parts;
        if (h.si_pct_float)
            parts << QStringLiteral("<b>%1</b> of float").arg(pct_or_dash(h.si_pct_float, 1));
        if (h.days_to_cover)
            parts << QStringLiteral("<b>%1</b> days to cover").arg(QString::number(*h.days_to_cover, 'f', 1));
        if (h.sirio) {
            QString sirio = QStringLiteral("SI ÷ 13F shares <b>%1</b>").arg(QString::number(*h.sirio, 'f', 3));
            if (s.sirio_percentile)
                sirio += dim(QStringLiteral(" (pctl %1 of %2)")
                                 .arg(qRound(*s.sirio_percentile * 100)).arg(s.sirio_universe));
            parts << sirio;
        }
        if (h.si_change_pct)
            parts << span(QStringLiteral("%1 vs prior").arg(pct_or_dash(h.si_change_pct, 0, true)),
                          *h.si_change_pct > 0 ? ui::colors::RED() : ui::colors::GREEN());
        QString v = parts.join(QStringLiteral(" · "));
        if (v.isEmpty())
            v = s.short_history.has_data() || s.market_ok ? dim(fmt::placeholder()) : wait;
        shorts_.value->setText(v);
        QString n;
        if (h.si_settlement.isValid()) {
            n = QStringLiteral("settlement %1").arg(short_date(h.si_settlement));
            if (h.si_published_after.isValid())
                n += QStringLiteral(" · published ~%1").arg(short_date(h.si_published_after));
            n += s.short_history.has_data() ? QStringLiteral(" · FINRA")
                                             : QStringLiteral(" · vendor figure (FINRA unavailable)");
        } else if (!s.short_history.error.isEmpty()) {
            n = QStringLiteral("FINRA: ") + esc(s.short_history.error.left(120));
        }
        shorts_.note->setText(n);
        shorts_.note->setToolTip(ui::tooltip_wrap(QStringLiteral(
            "Settled short position from FINRA, twice a month, published about ten business days "
            "after settlement. SI ÷ 13F shares is the borrow-fee proxy of Drechsler & Drechsler; "
            "days to cover is the measure that survived post-2000 (Hong et al.).")));
    }

    // ── 5% stakes ───────────────────────────────────────────────────────────
    {
        int passive = 0;
        for (const auto& st : s.stakes)
            if (!st.activist) ++passive;
        QString v;
        if (h.activist_filings > 0)
            v = span(QStringLiteral("<b>%1</b> Schedule 13D filing%2, latest %3")
                         .arg(h.activist_filings)
                         .arg(h.activist_filings == 1 ? QString() : QStringLiteral("s"),
                              short_date(h.latest_activist)),
                     ui::colors::AMBER());
        else if (s.edgar_ok)
            v = QStringLiteral("no 13D on file");
        else
            v = wait;
        stakes_.value->setText(v);
        stakes_.note->setText(
            s.edgar_ok ? QStringLiteral("%1 passive 13G filing%2 in %3 months · badges on the holder rows")
                             .arg(passive).arg(passive == 1 ? QString() : QStringLiteral("s"))
                             .arg(s.window_months)
                       : QString());
        stakes_.note->setToolTip(ui::tooltip_wrap(QStringLiteral(
            "13D declares intent to influence and is due within five business days — a dated "
            "catalyst. 13G is the passive equivalent. The percentage lives in the filing body "
            "and is not extracted; the filing is one click away on the holder row.")));
    }

    // ── Flags ───────────────────────────────────────────────────────────────
    for (auto* w : flag_widgets_)
        delete w;
    flag_widgets_.clear();
    const auto flags = derive_flags(s);
    for (const auto& f : flags) {
        auto* line = new QLabel;
        line->setWordWrap(true);
        line->setTextFormat(Qt::RichText);
        line->setStyleSheet(QString("QLabel{color:%1;font-size:13px;border-left:3px solid %2;"
                                    "background:%3;padding:4px 8px;}")
                                .arg(ui::colors::TEXT_PRIMARY(), ui::colors::AMBER(),
                                     ui::colors::BG_SURFACE()));
        line->setText(QStringLiteral("<b>%1</b> — %2 %3")
                          .arg(esc(f.headline), esc(f.detail),
                               dim(f.as_of.isValid()
                                       ? QStringLiteral("as of %1").arg(short_date(f.as_of))
                                       : QString())));
        line->setToolTip(ui::tooltip_wrap(f.basis));
        line->setTextInteractionFlags(Qt::TextSelectableByMouse);
        flags_->insertWidget(flags_->count() - 1, line);
        flag_widgets_.push_back(line);
    }
    // What was checked and not found is a finding too — "nothing flagged" on a
    // half-loaded register is not.
    const bool checked = s.edgar_ok && (s.holders_ok || !s.holders_error.isEmpty() || s.market_ok) &&
                         (s.short_history.has_data() || !s.short_history.error.isEmpty() || s.market_ok);
    if (flags.isEmpty()) {
        none_->setText(loading || !checked
                           ? QStringLiteral("Checking for an insider cluster, a constrained short "
                                            "and a concentrated register…")
                           : QStringLiteral("No insider cluster buy · not short-constrained · not "
                                            "concentrated. The three flags with evidence behind "
                                            "them; hover a row above for what each number means."));
        none_->show();
    } else {
        QStringList absent;
        bool has_cluster = false, has_short = false, has_conc = false;
        for (const auto& f : flags) {
            has_cluster |= f.kind == FlagKind::InsiderClusterBuy;
            has_short |= f.kind == FlagKind::ShortConstrained;
            has_conc |= f.kind == FlagKind::Concentrated;
        }
        if (!has_cluster) absent << QStringLiteral("no insider cluster buy");
        if (!has_short) absent << QStringLiteral("not short-constrained");
        if (!has_conc) absent << QStringLiteral("not concentrated");
        none_->setText(absent.join(QStringLiteral(" · ")));
        none_->setVisible(!absent.isEmpty());
    }
}

} // namespace fincept::screens
