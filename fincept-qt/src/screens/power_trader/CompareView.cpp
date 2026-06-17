// src/screens/power_trader/CompareView.cpp
#include "screens/power_trader/CompareView.h"

#include "screens/power_trader/PowerTraderService.h"
#include "ui/components/SectionHeader.h"
#include "ui/theme/Theme.h"

#include <QFrame>
#include <QGridLayout>
#include <QVBoxLayout>
#include <algorithm>

namespace fincept::screens {

namespace {

const char* party_color(const QString& p) {
    if (p == QStringLiteral("D")) return "#3b82f6";
    if (p == QStringLiteral("R")) return "#ef4444";
    return "#eab308";
}

QString fmt_dollar(double v) {
    const double abs_v = std::abs(v);
    QString prefix = v < 0 ? QStringLiteral("-$") : QStringLiteral("$");
    if (abs_v >= 1e9) return prefix + QString::number(abs_v / 1e9, 'f', 2) + "B";
    if (abs_v >= 1e6) return prefix + QString::number(abs_v / 1e6, 'f', 1) + "M";
    if (abs_v >= 1e3) return prefix + QString::number(abs_v / 1e3, 'f', 0) + "K";
    return prefix + QString::number(abs_v, 'f', 0);
}

QString fmt_pct(double v) {
    return QString("%1%2%").arg(v >= 0 ? "+" : "").arg(v, 0, 'f', 1);
}

QLabel* make_tile(QWidget* parent, const QString& label, const QString& value,
                  const char* value_color) {
    auto* w = new QLabel(parent);
    w->setTextFormat(Qt::RichText);
    w->setText(QString(
        "<div style='color:%1;font-size:10px;letter-spacing:0.8px;'>%2</div>"
        "<div style='color:%3;font-size:16px;font-weight:700;'>%4</div>")
        .arg(ui::colors::TEXT_TERTIARY(), label,
             value_color ? QString(value_color) : ui::colors::TEXT_PRIMARY(),
             value));
    w->setStyleSheet(
        QString("QLabel { background:%1; border:1px solid %2; padding:6px 8px; }")
            .arg(ui::colors::BG_RAISED(), ui::colors::BORDER_DIM()));
    w->setMinimumHeight(48);
    return w;
}

} // namespace

CompareView::CompareView(QWidget* parent) : QWidget(parent) {
    build_ui();
}

void CompareView::build_ui() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    root->addWidget(fincept::ui::make_section_header(
        QStringLiteral("COMPARE \xe2\x80\x94 right-click any sidebar member, "
                       "then \"Add to Compare\" (up to 3)"), this));

    // Toolbar: clear button + slot indicators
    auto* toolbar = new QWidget(this);
    toolbar->setStyleSheet(
        QString("QWidget { background:%1; border-bottom:1px solid %2; }")
            .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));
    auto* tl = new QHBoxLayout(toolbar);
    tl->setContentsMargins(10, 6, 10, 6);
    tl->setSpacing(8);

    clear_btn_ = new QPushButton(QStringLiteral("Clear all"), toolbar);
    clear_btn_->setCursor(Qt::PointingHandCursor);
    clear_btn_->setStyleSheet(
        QString("QPushButton { background:%1; color:%2; border:1px solid %3;"
                "  border-radius:3px; padding:4px 12px; font-size:12px; }"
                "QPushButton:hover { color:%4; border:1px solid %4; }")
            .arg(ui::colors::BG_RAISED(), ui::colors::TEXT_SECONDARY(),
                 ui::colors::BORDER_MED(), ui::colors::AMBER()));
    connect(clear_btn_, &QPushButton::clicked, this, &CompareView::clear);
    tl->addWidget(clear_btn_);
    tl->addStretch();
    root->addWidget(toolbar);

    // Cards row
    cards_row_ = new QWidget(this);
    cards_row_->setStyleSheet(
        QString("QWidget { background:%1; }").arg(ui::colors::BG_BASE()));
    cards_layout_ = new QHBoxLayout(cards_row_);
    cards_layout_->setContentsMargins(10, 10, 10, 10);
    cards_layout_->setSpacing(10);
    root->addWidget(cards_row_, 1);

    empty_label_ = new QLabel(
        QStringLiteral("No members selected. Right-click a member in the sidebar "
                       "and choose \"Add to Compare\" to start a comparison."),
        this);
    empty_label_->setAlignment(Qt::AlignCenter);
    empty_label_->setWordWrap(true);
    empty_label_->setStyleSheet(
        QString("QLabel { color:%1; font-size:12px; padding:24px; }")
            .arg(ui::colors::TEXT_SECONDARY()));

    rebuild_cards();
}

void CompareView::set_summary(const power_trader::PowerTraderSummary& summary) {
    summary_ = summary;
    rebuild_cards();
}

void CompareView::add_member(const QString& member_id) {
    if (member_id.isEmpty()) return;
    // Already present? no-op.
    for (const auto& id : slot_member_ids_)
        if (id == member_id) return;
    // First empty slot.
    for (auto& id : slot_member_ids_) {
        if (id.isEmpty()) { id = member_id; rebuild_cards(); return; }
    }
    // All three slots full — evict the oldest (slot 0), shift left, place new
    // member in slot 2.
    slot_member_ids_[0] = slot_member_ids_[1];
    slot_member_ids_[1] = slot_member_ids_[2];
    slot_member_ids_[2] = member_id;
    rebuild_cards();
}

void CompareView::clear() {
    for (auto& id : slot_member_ids_) id.clear();
    rebuild_cards();
}

void CompareView::rebuild_cards() {
    // Wipe existing children. Skip empty_label_ — it's a permanent member we
    // recycle across rebuilds, so we must not deleteLater() it.
    QLayoutItem* item;
    while ((item = cards_layout_->takeAt(0)) != nullptr) {
        if (auto* w = item->widget(); w && w != empty_label_)
            w->deleteLater();
        delete item;
    }

    const int filled = std::count_if(slot_member_ids_.begin(), slot_member_ids_.end(),
                                     [](const QString& s) { return !s.isEmpty(); });
    if (filled == 0) {
        cards_layout_->addWidget(empty_label_);
        empty_label_->show();
        return;
    }
    empty_label_->setParent(this);  // reparent off the cards row
    empty_label_->hide();

    for (int i = 0; i < 3; ++i) {
        QWidget* card = slot_member_ids_[i].isEmpty()
            ? build_empty_slot(i)
            : build_card(i, slot_member_ids_[i]);
        cards_layout_->addWidget(card, 1);
    }
}

QWidget* CompareView::build_empty_slot(int /*slot*/) {
    auto* w = new QFrame(cards_row_);
    w->setFrameShape(QFrame::StyledPanel);
    w->setStyleSheet(
        QString("QFrame { background:%1; border:1px dashed %2; }")
            .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));
    auto* l = new QVBoxLayout(w);
    auto* hint = new QLabel(QStringLiteral("(empty slot)"), w);
    hint->setAlignment(Qt::AlignCenter);
    hint->setStyleSheet(QString("color:%1; font-size:11px;")
                            .arg(ui::colors::TEXT_TERTIARY()));
    l->addStretch();
    l->addWidget(hint);
    l->addStretch();
    return w;
}

QWidget* CompareView::build_card(int slot, const QString& member_id) {
    auto* card = new QFrame(cards_row_);
    card->setFrameShape(QFrame::StyledPanel);
    card->setStyleSheet(
        QString("QFrame { background:%1; border:1px solid %2; }")
            .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_MED()));

    auto* col = new QVBoxLayout(card);
    col->setContentsMargins(10, 10, 10, 10);
    col->setSpacing(8);

    // Find member
    const power_trader::CongressMember* m = nullptr;
    for (const auto& cm : summary_.members)
        if (cm.id == member_id) { m = &cm; break; }
    if (!m) {
        auto* err = new QLabel(QString("(member %1 not in current summary)").arg(member_id), card);
        err->setStyleSheet(QString("color:%1; font-size:11px;").arg(ui::colors::TEXT_TERTIARY()));
        col->addWidget(err);
        col->addStretch();
        return card;
    }

    // Header: name + party badge + remove
    auto* header_row = new QWidget(card);
    auto* hl = new QHBoxLayout(header_row);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(8);

    auto* name = new QLabel(QString("<b>%1</b>").arg(m->full_name.toHtmlEscaped()), header_row);
    name->setStyleSheet(
        QString("color:%1; font-size:14px;").arg(ui::colors::CYAN()));
    hl->addWidget(name, 1);

    auto* party = new QLabel(m->party, header_row);
    party->setAlignment(Qt::AlignCenter);
    party->setFixedSize(20, 18);
    party->setStyleSheet(
        QString("background:%1; color:#fff; font-size:10px; font-weight:700;")
            .arg(party_color(m->party)));
    hl->addWidget(party);

    auto* chb = new QLabel(m->chamber == power_trader::MemberChamber::Senate ? "SEN" : "HSE", header_row);
    chb->setAlignment(Qt::AlignCenter);
    chb->setFixedWidth(36);
    chb->setStyleSheet(
        QString("color:%1; font-size:11px; font-weight:600; border:1px solid %2; padding:2px;")
            .arg(ui::colors::TEXT_SECONDARY(), ui::colors::BORDER_MED()));
    hl->addWidget(chb);

    auto* remove_btn = new QPushButton(QStringLiteral("\xc3\x97"), header_row);
    remove_btn->setFixedSize(20, 18);
    remove_btn->setCursor(Qt::PointingHandCursor);
    remove_btn->setToolTip(QStringLiteral("Remove from compare"));
    remove_btn->setStyleSheet(
        QString("QPushButton { background:transparent; color:%1; border:none;"
                "  font-size:14px; font-weight:700; }"
                "QPushButton:hover { color:%2; }")
            .arg(ui::colors::TEXT_TERTIARY(), ui::colors::NEGATIVE()));
    connect(remove_btn, &QPushButton::clicked, this, [this, slot]() {
        slot_member_ids_[slot].clear();
        rebuild_cards();
    });
    hl->addWidget(remove_btn);
    col->addWidget(header_row);

    auto* meta = new QLabel(
        QString("%1 \xc2\xb7 %2").arg(m->state, m->district.isEmpty() ? "Senator" : m->district),
        card);
    meta->setStyleSheet(QString("color:%1; font-size:10px;").arg(ui::colors::TEXT_TERTIARY()));
    col->addWidget(meta);

    // Compute portfolio
    auto portfolio = power_trader::PowerTraderService::instance()
                         .compute_portfolio(m->id);
    auto trades = power_trader::PowerTraderService::instance()
                      .trades_for_member(m->id);

    // 2x2 stat grid
    auto* tiles = new QWidget(card);
    auto* tg = new QGridLayout(tiles);
    tg->setContentsMargins(0, 0, 0, 0);
    tg->setHorizontalSpacing(6);
    tg->setVerticalSpacing(6);

    // PORTFOLIO / YTD RETURN / ALPHA are real only when the portfolio is priced
    // (live + trade-date closes available); otherwise "—", never fabricated.
    // NET WORTH is disclosure-derived and always real.
    const QString kNA    = QString::fromUtf8("\xe2\x80\x94");
    const bool    priced = portfolio.priced;
    const char* alpha_color = (priced && m->alpha_ytd >= 0) ? ui::colors::POSITIVE : ui::colors::NEGATIVE;
    const char* ret_color   = (priced && m->portfolio_return_ytd >= 0) ? ui::colors::POSITIVE : ui::colors::NEGATIVE;

    tg->addWidget(make_tile(tiles, QStringLiteral("NET WORTH"),
                            fmt_dollar(m->estimated_net_worth), ui::colors::AMBER), 0, 0);
    tg->addWidget(make_tile(tiles, QStringLiteral("PORTFOLIO"),
                            priced ? fmt_dollar(portfolio.est_total_value) : kNA,
                            ui::colors::TEXT_PRIMARY), 0, 1);
    tg->addWidget(make_tile(tiles, QStringLiteral("YTD RETURN"),
                            priced ? fmt_pct(m->portfolio_return_ytd) : kNA,
                            priced ? ret_color : ui::colors::TEXT_SECONDARY), 1, 0);
    tg->addWidget(make_tile(tiles, QStringLiteral("ALPHA vs SPY"),
                            priced ? fmt_pct(m->alpha_ytd) : kNA,
                            priced ? alpha_color : ui::colors::TEXT_SECONDARY), 1, 1);
    col->addWidget(tiles);

    // Stats line
    double avg_sig = 0; int n_sig = 0;
    for (const auto& t : trades) { avg_sig += t.signal_score; ++n_sig; }
    avg_sig = n_sig ? avg_sig / n_sig : 0;
    auto* stats_line = new QLabel(card);
    stats_line->setTextFormat(Qt::RichText);
    stats_line->setText(QString(
        "<span style='color:%1;font-size:11px;'>%2 trades YTD \xc2\xb7 avg signal "
        "<span style='color:%3;font-weight:700;'>%4</span> \xc2\xb7 "
        "best pick %5</span>")
        .arg(ui::colors::TEXT_TERTIARY(),
             QString::number(m->trade_count_ytd),
             avg_sig >= 60 ? ui::colors::AMBER() : ui::colors::TEXT_SECONDARY(),
             QString::number(avg_sig, 'f', 0),
             portfolio.best_pick_ticker.isEmpty() ? QStringLiteral("\xe2\x80\x94")
                 : QString("%1 %2").arg(portfolio.best_pick_ticker,
                                        fmt_pct(portfolio.best_pick_pnl_pct))));
    col->addWidget(stats_line);

    // Top committee
    QString top_cmte = m->committees.isEmpty() ? QStringLiteral("\xe2\x80\x94") : m->committees.first();
    auto* cmte = new QLabel(QString("<span style='color:%1;font-size:10px;'>COMMITTEE</span><br>"
                                    "<span style='color:%2;font-size:11px;'>%3</span>")
                                .arg(ui::colors::TEXT_TERTIARY(), ui::colors::TEXT_SECONDARY(), top_cmte),
                            card);
    cmte->setWordWrap(true);
    col->addWidget(cmte);

    // Top 5 holdings
    auto* hdr_h = new QLabel(QStringLiteral("TOP HOLDINGS"), card);
    hdr_h->setStyleSheet(QString("color:%1; font-size:10px; letter-spacing:0.8px; "
                                 "border-bottom:1px solid %2; padding-bottom:2px;")
                             .arg(ui::colors::TEXT_TERTIARY(), ui::colors::BORDER_DIM()));
    col->addWidget(hdr_h);

    // Sort by estimated cost basis (always real); est_market_value is 0 for
    // unpriced positions and would sink them spuriously.
    auto sorted_holdings = portfolio.holdings;
    std::sort(sorted_holdings.begin(), sorted_holdings.end(),
              [](const power_trader::MemberHolding& a, const power_trader::MemberHolding& b) {
                  return a.est_cost_basis > b.est_cost_basis;
              });
    const int n_h = static_cast<int>(std::min<qsizetype>(5, sorted_holdings.size()));
    if (n_h == 0) {
        auto* none = new QLabel(QStringLiteral("(no reconstructible positions)"), card);
        none->setStyleSheet(QString("color:%1; font-size:11px;").arg(ui::colors::TEXT_TERTIARY()));
        col->addWidget(none);
    }
    for (int i = 0; i < n_h; ++i) {
        const auto& h = sorted_holdings[i];
        // Real value/return when priced, else "—".
        const bool    hp        = h.est_market_value > 0.0;
        const char*   pnl_color = (hp && h.est_pnl_pct >= 0) ? ui::colors::POSITIVE
                                  : hp                        ? ui::colors::NEGATIVE
                                                              : ui::colors::TEXT_SECONDARY;
        const QString val_str   = hp ? fmt_dollar(h.est_market_value) : QString::fromUtf8("\xe2\x80\x94");
        const QString pct_str   = hp ? fmt_pct(h.est_pnl_pct)         : QString::fromUtf8("\xe2\x80\x94");
        auto* row = new QLabel(card);
        row->setTextFormat(Qt::RichText);
        row->setText(QString(
            "<span style='color:%1;font-size:11px;'>%2</span> "
            "<span style='color:%3;font-size:11px;'>%4</span> "
            "<span style='color:%5;font-size:11px;'>%6</span>")
            .arg(ui::colors::CYAN(), h.ticker.leftJustified(6, ' ').toHtmlEscaped(),
                 ui::colors::TEXT_SECONDARY(), val_str,
                 pnl_color, pct_str));
        col->addWidget(row);
    }

    // Last 5 trades
    auto* hdr_t = new QLabel(QStringLiteral("RECENT TRADES"), card);
    hdr_t->setStyleSheet(QString("color:%1; font-size:10px; letter-spacing:0.8px; "
                                 "border-bottom:1px solid %2; padding-bottom:2px; margin-top:6px;")
                             .arg(ui::colors::TEXT_TERTIARY(), ui::colors::BORDER_DIM()));
    col->addWidget(hdr_t);

    auto recent_trades = trades;
    std::sort(recent_trades.begin(), recent_trades.end(),
              [](const power_trader::PoliticalTrade& a, const power_trader::PoliticalTrade& b) {
                  return a.disclosure_date > b.disclosure_date;
              });
    const int n_t = static_cast<int>(std::min<qsizetype>(5, recent_trades.size()));
    if (n_t == 0) {
        auto* none = new QLabel(QStringLiteral("(no recent trades)"), card);
        none->setStyleSheet(QString("color:%1; font-size:11px;").arg(ui::colors::TEXT_TERTIARY()));
        col->addWidget(none);
    }
    for (int i = 0; i < n_t; ++i) {
        const auto& t = recent_trades[i];
        const char* dir_color = t.direction == power_trader::TradeDirection::Buy
                                ? ui::colors::POSITIVE
                                : t.direction == power_trader::TradeDirection::Sell
                                  ? ui::colors::NEGATIVE
                                  : ui::colors::TEXT_SECONDARY;
        auto* row = new QLabel(card);
        row->setTextFormat(Qt::RichText);
        row->setText(QString(
            "<span style='color:%1;font-size:11px;'>%2</span> "
            "<span style='color:%3;font-size:11px;font-weight:700;'>%4</span> "
            "<span style='color:%5;font-size:11px;'>%6</span> "
            "<span style='color:%7;font-size:11px;'>%8</span>")
            .arg(ui::colors::TEXT_TERTIARY(), t.disclosure_date.toString("MM-dd"),
                 dir_color, power_trader::direction_label(t.direction),
                 ui::colors::CYAN(), t.ticker,
                 ui::colors::TEXT_SECONDARY(), t.amount_range_label));
        col->addWidget(row);
    }

    col->addStretch();
    return card;
}

} // namespace fincept::screens
