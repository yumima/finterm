// src/screens/power_trader/PowerTraderService.cpp
#include "screens/power_trader/PowerTraderService.h"

#include "core/logging/Logger.h"
#include "python/PythonRunner.h"
#include "python/PythonWorker.h"
#include "screens/power_trader/DataSourceDialog.h"

#include <QDate>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTimer>

namespace fincept::power_trader {

// ── Singleton ─────────────────────────────────────────────────────────────────

PowerTraderService& PowerTraderService::instance() {
    static PowerTraderService s;
    return s;
}

PowerTraderService::PowerTraderService(QObject* parent) : QObject(parent) {
    refresh_timer_ = new QTimer(this);
    refresh_timer_->setInterval(kRefreshIntervalMs);
    refresh_timer_->setSingleShot(false);
    connect(refresh_timer_, &QTimer::timeout, this, [this]() { load_data(); });
}

// ── Public API ────────────────────────────────────────────────────────────────

void PowerTraderService::load_data() {
    if (loading_) {
        LOG_DEBUG("PowerTrader", "load_data() called while already loading — skipped");
        return;
    }

    // If we have cached data that is less than 6 hours old, emit it immediately
    // and skip the network hit.
    if (summary_.loaded && summary_.last_updated.isValid()) {
        const qint64 age_secs = summary_.last_updated.secsTo(QDateTime::currentDateTimeUtc());
        if (age_secs < kRefreshIntervalMs / 1000) {
            emit data_loaded(summary_);
            return;
        }
    }

    loading_ = true;
    LOG_INFO("PowerTrader", "Loading congressional trade data from Senate eFD + House FDS");

    QPointer<PowerTraderService> self = this;
    QJsonObject req;
    req[QStringLiteral("days_back")] = 90;

    // The Congress.gov API key is delivered to the script via the env var
    // CONGRESS_GOV_API_KEY, injected by PythonRunner::build_python_env() from
    // SecureStorage (see kManagedCredentialKeys). No need to pass it in the
    // payload — that would also leak it into the subprocess argv visible via
    // ps/proc.

    // The Senate eFD scrape does many sequential HTTPs (one per PTR detail
    // page, partially parallelized). Cold start is ~35s for a typical 90-day
    // window; worst case during a busy filing quarter can push toward 2 min.
    // Override the default 30s timeout so the script isn't killed mid-scrape.
    constexpr int kSenateScrapeTimeoutMs = 180'000;  // 3 min

    python::PythonRunner::instance().run(
        QStringLiteral("senate_disclosures_data.py"),
        {QStringLiteral("all_data"),
         QString::fromUtf8(QJsonDocument(req).toJson(QJsonDocument::Compact))},
        [self](python::PythonResult result) {
            if (!self) return;
            self->loading_ = false;
            if (!self->refresh_timer_->isActive())
                self->refresh_timer_->start();

            if (!result.success || result.output.trimmed().isEmpty()) {
                LOG_ERROR("PowerTrader",
                          "senate_disclosures_data.py failed: " + result.error.left(300));
                emit self->error_occurred(
                    QStringLiteral("Could not reach the congressional disclosure sources.\n\n"
                                   "Sources tried:\n"
                                   "  • Senate eFD   — efdsearch.senate.gov\n"
                                   "  • House FDS    — disclosures-clerk.house.gov\n\n"
                                   "Check your network connection and try refreshing. "
                                   "If you haven't yet set a Congress.gov API key, "
                                   "press Refresh and use the prompt — it unlocks the "
                                   "live member roster and committee data."));
                return;
            }
            const QString json_str = python::extract_json(result.output);
            const auto    doc      = QJsonDocument::fromJson(json_str.toUtf8());
            if (!doc.isObject()) {
                LOG_ERROR("PowerTrader", "Invalid JSON from senate_disclosures_data.py");
                emit self->error_occurred(
                    QStringLiteral("Invalid data from congressional disclosure source."));
                return;
            }
            self->parse_summary(doc.object());
            if (self->summary_.members.isEmpty()) {
                emit self->error_occurred(
                    QStringLiteral("No congressional trades found in the last 90 days.\n\n"
                                   "Sources tried:\n"
                                   "  • Senate eFD   — efdsearch.senate.gov\n"
                                   "  • House FDS    — disclosures-clerk.house.gov\n\n"
                                   "The Senate eFD is occasionally in maintenance mode. "
                                   "Refresh to retry."));
            }
        },
        /*on_line=*/{},
        kSenateScrapeTimeoutMs);
}

QVector<CongressMember> PowerTraderService::members() const {
    return summary_.members;
}

QVector<PoliticalTrade> PowerTraderService::trades() const {
    return summary_.recent_trades;
}

QVector<PoliticalTrade> PowerTraderService::trades_for_member(const QString& member_id) const {
    QVector<PoliticalTrade> out;
    for (const auto& t : summary_.recent_trades)
        if (t.member_id == member_id)
            out.append(t);
    return out;
}

QVector<PoliticalTrade> PowerTraderService::trades_for_ticker(const QString& ticker) const {
    const QString u = ticker.toUpper();
    QVector<PoliticalTrade> out;
    for (const auto& t : summary_.recent_trades)
        if (t.ticker.toUpper() == u)
            out.append(t);
    return out;
}

// ── Daemon response handler (for future real data path) ───────────────────────

void PowerTraderService::on_daemon_response(bool ok, const QJsonObject& result, const QString& error) {
    loading_ = false;
    if (!ok) {
        LOG_ERROR("PowerTrader", "Daemon error: " + error.left(300));
        emit error_occurred(error.isEmpty() ? QStringLiteral("Failed to load congressional data") : error);
        return;
    }
    parse_summary(result);
}

void PowerTraderService::parse_summary(const QJsonObject& root) {
    PowerTraderSummary s;
    s.last_updated = QDateTime::currentDateTimeUtc();
    s.loaded = true;

    const auto members_arr = root.value(QStringLiteral("members")).toArray();
    s.members.reserve(members_arr.size());
    for (const auto& v : members_arr) {
        const auto o = v.toObject();
        CongressMember m;
        m.id             = o[QStringLiteral("id")].toString();
        m.full_name      = o[QStringLiteral("full_name")].toString();
        m.party          = o[QStringLiteral("party")].toString();
        m.chamber        = o[QStringLiteral("chamber")].toString() == QStringLiteral("Senate")
                               ? MemberChamber::Senate : MemberChamber::House;
        m.state          = o[QStringLiteral("state")].toString();
        m.district       = o[QStringLiteral("district")].toString();
        for (const auto& c : o[QStringLiteral("committees")].toArray())
            m.committees.append(c.toString());
        m.term_start         = QDate::fromString(o[QStringLiteral("term_start")].toString(), Qt::ISODate);
        m.estimated_net_worth= o[QStringLiteral("estimated_net_worth")].toDouble();
        m.trade_count_ytd    = o[QStringLiteral("trade_count_ytd")].toInt();
        m.portfolio_return_ytd = o[QStringLiteral("portfolio_return_ytd")].toDouble();
        m.spy_return_ytd     = o[QStringLiteral("spy_return_ytd")].toDouble();
        m.alpha_ytd          = m.portfolio_return_ytd - m.spy_return_ytd;
        s.members.append(m);
    }

    const auto trades_arr = root.value(QStringLiteral("trades")).toArray();
    s.recent_trades.reserve(trades_arr.size());
    for (const auto& v : trades_arr) {
        const auto o = v.toObject();
        PoliticalTrade t;
        t.id               = o[QStringLiteral("id")].toString();
        t.member_id        = o[QStringLiteral("member_id")].toString();
        t.member_name      = o[QStringLiteral("member_name")].toString();
        t.party            = o[QStringLiteral("party")].toString();
        t.chamber          = o[QStringLiteral("chamber")].toString() == QStringLiteral("Senate")
                                 ? MemberChamber::Senate : MemberChamber::House;
        t.transaction_date = QDate::fromString(o[QStringLiteral("transaction_date")].toString(), Qt::ISODate);
        t.disclosure_date  = QDate::fromString(o[QStringLiteral("disclosure_date")].toString(), Qt::ISODate);
        t.disclosure_lag_days = o[QStringLiteral("disclosure_lag_days")].toInt();
        t.ticker           = o[QStringLiteral("ticker")].toString();
        t.asset_name       = o[QStringLiteral("asset_name")].toString();
        t.amount_low       = o[QStringLiteral("amount_low")].toDouble();
        t.amount_high      = o[QStringLiteral("amount_high")].toDouble();
        t.amount_range_label = o[QStringLiteral("amount_range_label")].toString();
        t.committee_relevance= o[QStringLiteral("committee_relevance")].toString();
        t.signal_score     = o[QStringLiteral("signal_score")].toDouble();
        t.source_url       = o[QStringLiteral("source_url")].toString();

        const QString dir = o[QStringLiteral("direction")].toString();
        if (dir == QStringLiteral("Buy"))       t.direction = TradeDirection::Buy;
        else if (dir == QStringLiteral("Sell")) t.direction = TradeDirection::Sell;
        else if (dir == QStringLiteral("Exchange")) t.direction = TradeDirection::Exchange;
        else                                    t.direction = TradeDirection::Other;

        const QString at = o[QStringLiteral("asset_type")].toString();
        if (at == QStringLiteral("Stock"))           t.asset_type = AssetType::Stock;
        else if (at == QStringLiteral("Option"))     t.asset_type = AssetType::Option;
        else if (at == QStringLiteral("ETF"))        t.asset_type = AssetType::ETF;
        else if (at == QStringLiteral("Bond"))       t.asset_type = AssetType::Bond;
        else if (at == QStringLiteral("MutualFund")) t.asset_type = AssetType::MutualFund;
        else                                         t.asset_type = AssetType::Other;

        s.recent_trades.append(t);
    }

    summary_ = s;

    // Real SPY YTD return is provided by the script (fetched live from
    // yfinance with a daily cache). If the script didn't include it (network
    // failure, yfinance throttled), spy_ytd stays 0 and we skip the alpha
    // computation rather than ship a fake benchmark.
    const double spy_ytd  = root.value(QStringLiteral("spy_return_ytd")).toDouble(0.0);
    const bool   have_spy = qAbs(spy_ytd) > 1e-9;

    for (auto& m : summary_.members) {
        const bool needs_computation =
            qAbs(m.portfolio_return_ytd) < 1e-9 && m.trade_count_ytd > 0;
        if (needs_computation) {
            const auto p = compute_portfolio(m.id);
            if (p.est_total_cost > 100.0) {
                m.portfolio_return_ytd = p.est_total_pnl_pct;
                if (have_spy) {
                    m.spy_return_ytd = spy_ytd;
                    m.alpha_ytd      = m.portfolio_return_ytd - spy_ytd;
                }
            }
        }
    }

    LOG_INFO("PowerTrader", QString("Loaded %1 members, %2 trades (SPY YTD %3)")
                 .arg(s.members.size()).arg(s.recent_trades.size())
                 .arg(have_spy ? QStringLiteral("%1%").arg(spy_ytd, 0, 'f', 2)
                               : QStringLiteral("n/a")));
    emit data_loaded(summary_);
}

// ── Static classification maps ────────────────────────────────────────────────

const QHash<QString, QString>& PowerTraderService::ticker_sector_map() {
    static const QHash<QString, QString> m = {
        {"LMT","Defense"},  {"RTX","Defense"},  {"NOC","Defense"}, {"GD","Defense"},
        {"BA","Defense"},   {"LDOS","Defense"}, {"SAIC","Defense"},{"RKLB","Aerospace"},
        {"ASTR","Aerospace"},{"SPCE","Aerospace"},
        {"CVX","Energy"},   {"XOM","Energy"},   {"SLB","Energy"},  {"HAL","Energy"},
        {"PSX","Energy"},   {"PXD","Energy"},   {"OXY","Energy"},  {"NEE","Energy"},
        {"DUK","Energy"},
        {"JPM","Financials"},{"BAC","Financials"},{"WFC","Financials"},{"C","Financials"},
        {"GS","Financials"}, {"MS","Financials"}, {"BX","Financials"},{"KKR","Financials"},
        {"AXP","Financials"},{"V","Financials"},  {"MA","Financials"},
        {"SPGI","Financials"},{"ICE","Financials"},{"SCHW","Financials"},{"BLK","Financials"},
        {"AAPL","Technology"},{"MSFT","Technology"},{"AMZN","Technology"},
        {"GOOGL","Technology"},{"GOOG","Technology"},{"META","Technology"},
        {"NVDA","Technology"},{"AMD","Technology"},{"QCOM","Technology"},
        {"TSM","Technology"}, {"TSLA","Technology"},{"CRM","Technology"},
        {"JNJ","Healthcare"}, {"PFE","Healthcare"},{"ABBV","Healthcare"},
        {"MRK","Healthcare"}, {"UNH","Healthcare"},{"CVS","Healthcare"},
        {"DIS","Consumer"},   {"CMCSA","Consumer"},{"NFLX","Consumer"},
        {"SBUX","Consumer"},  {"COST","Consumer"}, {"HD","Consumer"},
        {"LOW","Consumer"},   {"WM","Industrials"},{"BRK-B","Financials"},
        {"SPY","ETF"},        {"VTI","ETF"},
    };
    return m;
}

const QHash<QString, QString>& PowerTraderService::ticker_committee_map() {
    static const QHash<QString, QString> m = {
        {"LMT","Armed Services"},  {"RTX","Armed Services"}, {"NOC","Armed Services"},
        {"GD","Armed Services"},   {"BA","Armed Services"},  {"LDOS","Armed Services"},
        {"SAIC","Armed Services"}, {"RKLB","Armed Services"},{"ASTR","Armed Services"},
        {"CVX","Energy"},  {"XOM","Energy"},  {"SLB","Energy"},  {"HAL","Energy"},
        {"PSX","Energy"},  {"PXD","Energy"},  {"OXY","Energy"},  {"NEE","Energy"},
        {"DUK","Energy"},
        {"JPM","Banking"}, {"BAC","Banking"}, {"WFC","Banking"}, {"C","Banking"},
        {"GS","Banking"},  {"MS","Banking"},  {"BX","Banking"},  {"KKR","Banking"},
        {"AXP","Finance"}, {"V","Financial Services"},{"MA","Financial Services"},
        {"AAPL","Commerce"},{"MSFT","Commerce"},{"AMZN","Commerce"},
        {"GOOGL","Commerce"},{"META","Commerce"},{"NVDA","Commerce"},
        {"JNJ","Health"},  {"PFE","Health"},  {"ABBV","Health"},
        {"MRK","Health"},  {"UNH","Health"},  {"CVS","Health"},
    };
    return m;
}

// ── Portfolio reconstruction ──────────────────────────────────────────────────

MemberPortfolio PowerTraderService::compute_portfolio(const QString& member_id) const {
    const auto trades = trades_for_member(member_id);
    MemberPortfolio portfolio;
    portfolio.member_id = member_id;

    if (trades.isEmpty())
        return portfolio;

    // Aggregate net position per ticker (using midpoints of STOCK/ETF trades only)
    QHash<QString, MemberHolding> positions;
    const auto& sec_map  = ticker_sector_map();

    // Work through trades chronologically for NAV series
    auto sorted = trades;
    std::sort(sorted.begin(), sorted.end(),
              [](const PoliticalTrade& a, const PoliticalTrade& b) {
                  return a.transaction_date < b.transaction_date;
              });

    QMap<QDate, double> cumulative_nav;  // date → running invested amount
    double running_cost = 0;

    for (const auto& t : sorted) {
        if (t.asset_type != AssetType::Stock && t.asset_type != AssetType::ETF)
            continue;

        const double midpoint = (t.amount_low + t.amount_high) / 2.0;
        auto& h = positions[t.ticker];
        h.ticker     = t.ticker;
        h.asset_name = t.asset_name;
        h.sector     = sec_map.value(t.ticker, QStringLiteral("Other"));

        if (t.direction == TradeDirection::Buy) {
            h.est_cost_basis += midpoint;
            h.buy_count++;
            running_cost += midpoint;
        } else if (t.direction == TradeDirection::Sell) {
            const double reduce = qMin(midpoint, h.est_cost_basis);
            h.est_cost_basis   -= reduce;
            h.sell_count++;
            running_cost -= reduce;
        }

        if (!t.committee_relevance.isEmpty()) {
            h.committee_overlap = true;
            h.committee_name    = t.committee_relevance;
        }

        cumulative_nav[t.transaction_date] = qMax(0.0, running_cost);
    }

    // Build NAV series (one point per trade date)
    for (auto it = cumulative_nav.begin(); it != cumulative_nav.end(); ++it)
        portfolio.nav_series.append({it.key(), it.value()});

    // Only keep net-long positions
    double total_cost = 0;
    for (const auto& h : positions) {
        if (h.est_cost_basis > 500.0) {  // filter tiny residuals
            portfolio.holdings.append(h);
            total_cost += h.est_cost_basis;
        }
    }
    portfolio.est_total_cost = total_cost;

    // Estimate market value: apply hypothetical 15% gain as a proxy when no live price
    // (real implementation should call MarketDataService per ticker)
    for (auto& h : portfolio.holdings) {
        // Rough P&L proxy: tech up ~30%, defense up ~15%, energy up ~10% YTD
        double sector_gain = 0.15;
        if (h.sector == QStringLiteral("Technology"))  sector_gain = 0.28;
        else if (h.sector == QStringLiteral("Defense"))sector_gain = 0.17;
        else if (h.sector == QStringLiteral("Energy"))  sector_gain = 0.09;
        else if (h.sector == QStringLiteral("Financials")) sector_gain = 0.22;
        else if (h.sector == QStringLiteral("Healthcare"))  sector_gain = 0.08;
        else if (h.sector == QStringLiteral("ETF"))          sector_gain = 0.12;

        h.est_market_value = h.est_cost_basis * (1.0 + sector_gain);
        h.est_pnl          = h.est_market_value - h.est_cost_basis;
        h.est_pnl_pct      = sector_gain * 100.0;
        h.est_weight       = total_cost > 0 ? (h.est_cost_basis / total_cost) * 100.0 : 0;
    }

    // Sort by weight descending
    std::sort(portfolio.holdings.begin(), portfolio.holdings.end(),
              [](const MemberHolding& a, const MemberHolding& b) {
                  return a.est_cost_basis > b.est_cost_basis;
              });

    double total_value = 0;
    double total_pnl   = 0;
    for (const auto& h : portfolio.holdings) {
        total_value += h.est_market_value;
        total_pnl   += h.est_pnl;
    }
    portfolio.est_total_value   = total_value;
    portfolio.est_total_pnl     = total_pnl;
    portfolio.est_total_pnl_pct = total_cost > 0 ? (total_pnl / total_cost) * 100.0 : 0;

    // Derived stats
    portfolio.net_buyer_90d     = net_buyer_amount(member_id, 90);
    portfolio.avg_signal_score  = avg_signal_score(member_id);
    portfolio.avg_lag_days      = avg_disclosure_lag(member_id);
    const auto bp               = best_pick(member_id);
    portfolio.best_pick_ticker  = bp.first;
    portfolio.best_pick_pnl_pct = bp.second;

    return portfolio;
}

// ── Sector breakdown ──────────────────────────────────────────────────────────

QVector<SectorExposure> PowerTraderService::sector_breakdown() const {
    const auto& sec_map = ticker_sector_map();
    QHash<QString, SectorExposure> sectors;

    for (const auto& t : summary_.recent_trades) {
        if (t.direction == TradeDirection::Sell) continue;
        const QString sector = sec_map.value(t.ticker, QStringLiteral("Other"));
        auto& s = sectors[sector];
        s.sector = sector;
        const double mid = (t.amount_low + t.amount_high) / 2.0;
        s.total_est_amount += mid;
        s.trade_count++;
        if (!s.top_tickers.contains(t.ticker) && s.top_tickers.size() < 5)
            s.top_tickers.append(t.ticker);
        if (!s.members.contains(t.member_id))
            s.members.append(t.member_id);
    }

    double grand_total = 0;
    for (const auto& s : sectors) grand_total += s.total_est_amount;
    for (auto& s : sectors) {
        s.pct_of_all  = grand_total > 0 ? (s.total_est_amount / grand_total) * 100.0 : 0;
        s.member_count = s.members.size();
    }

    auto result = sectors.values().toVector();
    std::sort(result.begin(), result.end(),
              [](const SectorExposure& a, const SectorExposure& b) {
                  return a.total_est_amount > b.total_est_amount;
              });
    return result;
}

// ── Committee insider signals ─────────────────────────────────────────────────

QVector<CommitteeInsiderSignal> PowerTraderService::committee_insider_signals() const {
    const auto& sec_map  = ticker_sector_map();

    // For each member, compute how many of their trades fall in their committee's sector
    QHash<QString, CommitteeInsiderSignal> committee_sigs;

    for (const auto& t : summary_.recent_trades) {
        if (t.committee_relevance.isEmpty()) continue;
        const QString key = t.member_id + QStringLiteral(":") + t.committee_relevance;
        auto& sig = committee_sigs[key];
        sig.member_id   = t.member_id;
        sig.member_name = t.member_name;
        sig.party       = t.party;
        sig.committee   = t.committee_relevance;
        sig.ticker      = t.ticker;
        sig.sector      = sec_map.value(t.ticker, QStringLiteral("Other"));
        sig.amount_midpoint += (t.amount_low + t.amount_high) / 2.0;
        sig.transaction_date = t.transaction_date;
        sig.signal_score     = qMax(sig.signal_score, t.signal_score);
        sig.overlap_trades++;
    }

    // Total trades per member
    QHash<QString, int> total_trades;
    for (const auto& t : summary_.recent_trades)
        total_trades[t.member_id]++;

    auto result = committee_sigs.values().toVector();
    for (auto& sig : result) {
        sig.total_trades  = total_trades.value(sig.member_id, 1);
        sig.overlap_pct   = (double)sig.overlap_trades / sig.total_trades * 100.0;
    }
    std::sort(result.begin(), result.end(),
              [](const CommitteeInsiderSignal& a, const CommitteeInsiderSignal& b) {
                  return a.overlap_pct > b.overlap_pct;
              });
    return result;
}

// ── Rankings ──────────────────────────────────────────────────────────────────

QVector<RankedMember> PowerTraderService::ranked_members(RankingDimension dim) const {
    QVector<RankedMember> result;
    result.reserve(summary_.members.size());

    for (const auto& m : summary_.members) {
        RankedMember r;
        r.member = m;

        switch (dim) {
        case RankingDimension::Alpha:
            r.rank_value = m.alpha_ytd;
            r.rank_label = (m.alpha_ytd >= 0 ? QStringLiteral("+") : QString())
                           + QString::number(m.alpha_ytd, 'f', 1) + QStringLiteral("%");
            break;
        case RankingDimension::Return:
            r.rank_value = m.portfolio_return_ytd;
            r.rank_label = (m.portfolio_return_ytd >= 0 ? QStringLiteral("+") : QString())
                           + QString::number(m.portfolio_return_ytd, 'f', 1) + QStringLiteral("%");
            break;
        case RankingDimension::NetWorth: {
            r.rank_value = m.estimated_net_worth;
            const double v = m.estimated_net_worth;
            if      (v >= 1e9) r.rank_label = QStringLiteral("$%1B").arg(v/1e9, 0,'f',1);
            else if (v >= 1e6) r.rank_label = QStringLiteral("$%1M").arg(v/1e6, 0,'f',1);
            else               r.rank_label = QStringLiteral("$%1K").arg(v/1e3, 0,'f',0);
            break;
        }
        case RankingDimension::NetBuyer: {
            const double nb = net_buyer_amount(m.id, 90);
            r.rank_value = nb;
            if (nb >= 1e6) r.rank_label = QStringLiteral("$%1M").arg(nb/1e6, 0,'f',1);
            else           r.rank_label = QStringLiteral("$%1K").arg(nb/1e3, 0,'f',0);
            break;
        }
        case RankingDimension::Frequency:
            r.rank_value = m.trade_count_ytd;
            r.rank_label = QString::number(m.trade_count_ytd) + QStringLiteral(" trades");
            break;
        case RankingDimension::AvgSignal: {
            const double s = avg_signal_score(m.id);
            r.rank_value = s;
            r.rank_label = QString::number(s, 'f', 1) + QStringLiteral("/100");
            break;
        }
        case RankingDimension::DisclosureLag: {
            const double l = avg_disclosure_lag(m.id);
            r.rank_value = -l;  // negate: higher lag = worse rank
            r.rank_label = QString::number(l, 'f', 1) + QStringLiteral("d avg");
            break;
        }
        case RankingDimension::BestPick: {
            const auto bp = best_pick(m.id);
            r.rank_value  = bp.second;
            r.rank_label  = bp.first.isEmpty() ? QStringLiteral("–")
                            : bp.first + QStringLiteral(" +")
                              + QString::number(bp.second, 'f', 0) + QStringLiteral("%");
            break;
        }
        }
        result.append(r);
    }

    std::sort(result.begin(), result.end(),
              [](const RankedMember& a, const RankedMember& b) {
                  return a.rank_value > b.rank_value;
              });
    for (int i = 0; i < result.size(); ++i)
        result[i].rank = i + 1;

    return result;
}

// ── Per-member derived stats ──────────────────────────────────────────────────

double PowerTraderService::net_buyer_amount(const QString& mid, int days) const {
    const QDate cutoff = QDate::currentDate().addDays(-days);
    double net = 0;
    for (const auto& t : summary_.recent_trades) {
        if (t.member_id != mid) continue;
        if (t.transaction_date < cutoff) continue;
        const double v = (t.amount_low + t.amount_high) / 2.0;
        if (t.direction == TradeDirection::Buy)  net += v;
        if (t.direction == TradeDirection::Sell) net -= v;
    }
    return net;
}

double PowerTraderService::avg_signal_score(const QString& mid) const {
    double sum = 0; int n = 0;
    for (const auto& t : summary_.recent_trades)
        if (t.member_id == mid) { sum += t.signal_score; ++n; }
    return n > 0 ? sum / n : 0;
}

double PowerTraderService::avg_disclosure_lag(const QString& mid) const {
    double sum = 0; int n = 0;
    for (const auto& t : summary_.recent_trades)
        if (t.member_id == mid) { sum += t.disclosure_lag_days; ++n; }
    return n > 0 ? sum / n : 0;
}

QPair<QString, double> PowerTraderService::best_pick(const QString& mid) const {
    const auto& sec_map = ticker_sector_map();
    QString best_ticker;
    double best_pct = 0;
    for (const auto& t : summary_.recent_trades) {
        if (t.member_id != mid || t.direction != TradeDirection::Buy) continue;
        const QString sector = sec_map.value(t.ticker, QStringLiteral("Other"));
        double gain = 15;
        if (sector == QStringLiteral("Technology"))  gain = 28;
        else if (sector == QStringLiteral("Defense"))gain = 17;
        else if (sector == QStringLiteral("Financials")) gain = 22;
        if (gain > best_pct) { best_pct = gain; best_ticker = t.ticker; }
    }
    return {best_ticker, best_pct};
}

// ── Cabinet data ──────────────────────────────────────────────────────────────

void PowerTraderService::load_cabinet_data() {
    if (cabinet_loading_) return;
    cabinet_loading_ = true;
    LOG_INFO("PowerTrader", "Loading cabinet financial disclosures (OGE Form 278)");

    QPointer<PowerTraderService> self = this;
    python::PythonRunner::instance().run(
        QStringLiteral("oge_cabinet_data.py"),
        {QStringLiteral("cabinet_data"), QStringLiteral("{}")},
        [self](python::PythonResult result) {
            if (!self) return;
            self->cabinet_loading_ = false;
            if (!result.success || result.output.trimmed().isEmpty()) {
                LOG_ERROR("PowerTrader", "oge_cabinet_data.py failed: " + result.error.left(200));
                return;
            }
            const QString json_str = python::extract_json(result.output);
            const auto    doc      = QJsonDocument::fromJson(json_str.toUtf8());
            if (!doc.isObject()) {
                LOG_ERROR("PowerTrader", "oge_cabinet_data.py returned invalid JSON");
                return;
            }
            self->parse_cabinet(doc.object());
        });
}

void PowerTraderService::parse_cabinet(const QJsonObject& root) {
    CabinetSummary cs;
    cs.last_updated   = QDateTime::currentDateTimeUtc();
    cs.loaded         = true;
    cs.disclosure_year= root[QStringLiteral("disclosure_year")].toInt(2025);
    cs.total_est_min  = root[QStringLiteral("total_est_min")].toDouble();
    cs.total_est_max  = root[QStringLiteral("total_est_max")].toDouble();
    cs.data_note      = root[QStringLiteral("data_note")].toString();

    const auto members_arr = root[QStringLiteral("members")].toArray();
    cs.members.reserve(members_arr.size());

    for (const auto& v : members_arr) {
        const auto obj = v.toObject();
        CabinetMember m;
        m.id              = obj[QStringLiteral("id")].toString();
        m.full_name       = obj[QStringLiteral("full_name")].toString();
        m.title           = obj[QStringLiteral("title")].toString();
        m.department      = obj[QStringLiteral("department")].toString();
        m.party           = obj[QStringLiteral("party")].toString();
        m.disclosure_year = obj[QStringLiteral("disclosure_year")].toInt();
        m.est_total_min   = obj[QStringLiteral("est_total_min")].toDouble();
        m.est_total_max   = obj[QStringLiteral("est_total_max")].toDouble();
        m.conflict_score  = obj[QStringLiteral("conflict_score")].toDouble();
        m.conflict_count  = obj[QStringLiteral("conflict_count")].toInt();
        m.source_url      = obj[QStringLiteral("source_url")].toString();
        m.data_source     = obj[QStringLiteral("data_source")].toString();

        for (const auto& c : obj[QStringLiteral("regulated_sectors")].toArray())
            m.regulated_sectors.append(c.toString());
        for (const auto& f : obj[QStringLiteral("conflict_flags")].toArray())
            m.conflict_flags.append(f.toString());

        for (const auto& hv : obj[QStringLiteral("holdings")].toArray()) {
            const auto ho = hv.toObject();
            CabinetHolding h;
            h.asset_name         = ho[QStringLiteral("asset_name")].toString();
            h.ticker             = ho[QStringLiteral("ticker")].toString();
            h.asset_type         = ho[QStringLiteral("asset_type")].toString();
            h.sector             = ho[QStringLiteral("sector")].toString();
            h.value_min          = ho[QStringLiteral("value_min")].toDouble();
            h.value_max          = ho[QStringLiteral("value_max")].toDouble();
            h.value_range_label  = ho[QStringLiteral("value_range_label")].toString();
            h.is_conflict        = ho[QStringLiteral("is_conflict")].toBool();
            h.conflict_note      = ho[QStringLiteral("conflict_note")].toString();
            m.holdings.append(h);
        }

        for (const auto& sv : obj[QStringLiteral("sector_breakdown")].toArray()) {
            const auto so = sv.toObject();
            SectorExposure se;
            se.sector            = so[QStringLiteral("sector")].toString();
            se.total_est_amount  = so[QStringLiteral("est_amount")].toDouble();
            m.sector_breakdown.append(se);
        }

        cs.members.append(m);
    }

    cabinet_ = cs;
    LOG_INFO("PowerTrader", QString("Cabinet data loaded: %1 members").arg(cs.members.size()));
    emit cabinet_data_loaded(cs);
}

QVector<CabinetMember> PowerTraderService::cabinet_conflict_ranking() const {
    auto result = cabinet_.members;
    std::sort(result.begin(), result.end(),
              [](const CabinetMember& a, const CabinetMember& b) {
                  return a.conflict_score > b.conflict_score;
              });
    return result;
}

QVector<SectorExposure> PowerTraderService::cabinet_sector_exposure() const {
    QHash<QString, SectorExposure> sectors;
    for (const auto& m : cabinet_.members) {
        for (const auto& se : m.sector_breakdown) {
            auto& s = sectors[se.sector];
            s.sector            = se.sector;
            s.total_est_amount += se.total_est_amount;
            s.member_count++;
            if (!s.members.contains(m.id)) s.members.append(m.id);
        }
    }
    auto result = sectors.values().toVector();
    std::sort(result.begin(), result.end(),
              [](const SectorExposure& a, const SectorExposure& b) {
                  return a.total_est_amount > b.total_est_amount;
              });
    return result;
}

// ── Signal Builder ────────────────────────────────────────────────────────────

QVector<TradeFactorScores> PowerTraderService::compute_trade_base_scores() const {
    // Build herd map: ticker → list of (member_id, date) for cluster detection
    QHash<QString, QVector<QPair<QString, QDate>>> herd_map;
    for (const auto& t : summary_.recent_trades)
        if (!t.ticker.isEmpty())
            herd_map[t.ticker].append({t.member_id, t.transaction_date});

    // Member avg signal (0-100) for history factor
    QHash<QString, double> member_avg_sig;
    QHash<QString, int>    member_trade_cnt;
    for (const auto& t : summary_.recent_trades) {
        member_avg_sig[t.member_id] += t.signal_score;
        member_trade_cnt[t.member_id]++;
    }

    QVector<TradeFactorScores> result;
    result.reserve(summary_.recent_trades.size());

    for (const auto& t : summary_.recent_trades) {
        TradeFactorScores s;
        s.trade_id = t.id;

        // ── Committee (0–100) ─────────────────────────────────────────────────
        s.committee = t.committee_relevance.isEmpty() ? 15.0 : 85.0;

        // ── Size (0–100) ──────────────────────────────────────────────────────
        const double mid = (t.amount_low + t.amount_high) / 2.0;
        if      (mid >= 1'000'000) s.size = 100;
        else if (mid >= 500'000)   s.size = 85;
        else if (mid >= 250'000)   s.size = 70;
        else if (mid >= 100'000)   s.size = 55;
        else if (mid >= 50'000)    s.size = 40;
        else if (mid >= 15'000)    s.size = 25;
        else                       s.size = 10;

        // ── Lag (0–100) ───────────────────────────────────────────────────────
        // Short lag = actionable; over deadline = suspicious flag
        const int lag = t.disclosure_lag_days;
        if      (lag <= 5)  s.lag = 95;  // filed immediately — high conviction
        else if (lag <= 14) s.lag = 75;
        else if (lag <= 30) s.lag = 50;
        else if (lag <= 44) s.lag = 25;
        else                s.lag = 80;  // STOCK Act violation — flagged high

        // ── Herd (0–100) ──────────────────────────────────────────────────────
        // Count other members trading same ticker within 14 days
        const auto& peers = herd_map.value(t.ticker);
        int cluster = 0;
        for (const auto& [pid, pdate] : peers)
            if (pid != t.member_id && qAbs(pdate.daysTo(t.transaction_date)) <= 14)
                ++cluster;
        s.herd = qMin(100.0, cluster * 30.0);

        // ── History (0–100) ───────────────────────────────────────────────────
        // Member's track record: avg signal score across all their trades
        const int cnt = member_trade_cnt.value(t.member_id, 0);
        s.history = cnt > 0
            ? qMin(100.0, member_avg_sig.value(t.member_id) / cnt)
            : 0.0;

        // timing / bill / lobbying stay 0 until external data integrated

        result.append(s);
    }
    return result;
}

QVector<SignalPreset> PowerTraderService::builtin_presets() {
    // Use explicit field assignment to stay correct if SignalPreset fields are reordered.
    auto make = [](const QString& id, const QString& name,
                   double cmte, double sz, double lag, double herd, double hist) {
        SignalPreset p;
        p.id = id; p.name = name; p.builtin = true;
        p.w_committee = cmte;  p.w_size    = sz;
        p.w_lag       = lag;   p.w_herd    = herd;
        p.w_timing    = 0.0;   p.w_bill    = 0.0;   p.w_lobbying = 0.0;
        p.w_history   = hist;
        return p;
    };
    return {
        make("default",           "Default",          1.0, 1.0, 1.0, 1.0, 0.5),
        make("committee_heavy",   "Committee Heavy",   2.0, 0.8, 0.8, 1.0, 0.5),
        make("herd_focus",        "Herd Focus",        1.5, 0.5, 0.5, 2.0, 0.5),
        make("size_first",        "Large Trades",      0.8, 2.0, 0.8, 0.8, 0.5),
        make("timing_sensitive",  "Fast Filers",       1.0, 1.0, 2.0, 1.0, 0.5),
    };
}

// ── Committee groups ──────────────────────────────────────────────────────────

QVector<CommitteeGroup> PowerTraderService::committee_groups() const {
    const auto& sec_map = ticker_sector_map();
    QHash<QString, CommitteeGroup> groups;

    // Seed one group per committee found in any member
    for (const auto& m : summary_.members) {
        for (const auto& c : m.committees) {
            if (c.trimmed().isEmpty()) continue;
            auto& g       = groups[c];
            g.committee   = c;
            if (!g.member_ids.contains(m.id)) {
                g.member_ids.append(m.id);
                g.member_count++;
            }
        }
    }

    // Aggregate trades
    for (const auto& t : summary_.recent_trades) {
        const QString cmte = t.committee_relevance;
        if (cmte.isEmpty()) continue;
        auto& g = groups[cmte];
        g.committee = cmte;
        g.trade_count++;
        g.total_est_amount += (t.amount_low + t.amount_high) / 2.0;
        g.avg_signal_score += t.signal_score;
        if (!t.ticker.isEmpty() && !g.top_tickers.contains(t.ticker) && g.top_tickers.size() < 5)
            g.top_tickers.append(t.ticker);
        const QString sector = sec_map.value(t.ticker, QStringLiteral("Other"));
        if (g.top_sector.isEmpty()) g.top_sector = sector;
    }

    // Compute correlation_pct (trades in cmte-relevant tickers / total trades for those members)
    QHash<QString, int> member_total;
    for (const auto& t : summary_.recent_trades)
        member_total[t.member_id]++;

    QVector<CommitteeGroup> result;
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        auto& g = it.value();
        if (g.trade_count > 0)
            g.avg_signal_score /= g.trade_count;
        // correlation: trades-with-overlap / (total trades of members on this committee)
        int total_for_members = 0;
        for (const auto& mid : g.member_ids)
            total_for_members += member_total.value(mid, 0);
        g.correlation_pct = total_for_members > 0
                            ? (g.trade_count * 100.0) / total_for_members
                            : 0;
        if (g.member_count > 0 || g.trade_count > 0)
            result.append(g);
    }

    std::sort(result.begin(), result.end(),
              [](const CommitteeGroup& a, const CommitteeGroup& b) {
                  return a.trade_count > b.trade_count;
              });
    return result;
}

// ── Party stats ───────────────────────────────────────────────────────────────

PartyStats PowerTraderService::party_stats(const QString& party) const {
    const auto& sec_map = ticker_sector_map();
    PartyStats ps;
    ps.party = party;
    const QDate cutoff = QDate::currentDate().addDays(-90);

    QHash<QString, double> ticker_amt;
    QHash<QString, SectorExposure> sectors;

    for (const auto& m : summary_.members)
        if (m.party == party) {
            ps.member_count++;
            const double nb = net_buyer_amount(m.id, 90);
            ps.net_bought_90d += nb;
            if (nb > 0)  ps.net_buyer_count++;
            if (nb < 0)  ps.net_seller_count++;
        }

    for (const auto& t : summary_.recent_trades) {
        if (t.party != party) continue;
        if (t.disclosure_date < cutoff) continue;
        ps.trade_count_90d++;
        const double mid = (t.amount_low + t.amount_high) / 2.0;
        ps.total_disc_90d += mid;
        ps.avg_signal     += t.signal_score;
        ps.avg_lag        += t.disclosure_lag_days;
        ticker_amt[t.ticker] += mid;

        const QString sector = sec_map.value(t.ticker, QStringLiteral("Other"));
        auto& se  = sectors[sector];
        se.sector = sector;
        se.total_est_amount += mid;
        se.trade_count++;
        if (!se.members.contains(t.member_id)) se.members.append(t.member_id);
    }

    if (ps.trade_count_90d > 0) {
        ps.avg_signal /= ps.trade_count_90d;
        ps.avg_lag    /= ps.trade_count_90d;
    }

    // Top tickers by amount
    QVector<QPair<double, QString>> sorted_tickers;
    for (auto it = ticker_amt.begin(); it != ticker_amt.end(); ++it)
        sorted_tickers.append({it.value(), it.key()});
    std::sort(sorted_tickers.begin(), sorted_tickers.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    for (int i = 0; i < qMin(5, sorted_tickers.size()); ++i)
        ps.top_tickers.append(sorted_tickers[i].second);

    // Top sectors
    ps.top_sectors = sectors.values().toVector();
    for (auto& s : ps.top_sectors) s.member_count = s.members.size();
    std::sort(ps.top_sectors.begin(), ps.top_sectors.end(),
              [](const SectorExposure& a, const SectorExposure& b) {
                  return a.total_est_amount > b.total_est_amount;
              });
    if (ps.top_sectors.size() > 6)
        ps.top_sectors.resize(6);

    return ps;
}

// ── Utilities ─────────────────────────────────────────────────────────────────

QStringList PowerTraderService::available_committees() const {
    QSet<QString> seen;
    QStringList   cmtes;
    for (const auto& m : summary_.members)
        for (const auto& c : m.committees)
            if (!c.isEmpty() && !seen.contains(c)) {
                seen.insert(c);
                cmtes.append(c);
            }
    cmtes.sort();
    return cmtes;
}

QVector<PoliticalTrade> PowerTraderService::trades_by_committee(const QString& committee) const {
    if (committee.isEmpty()) return summary_.recent_trades;
    QVector<PoliticalTrade> out;
    for (const auto& t : summary_.recent_trades)
        if (t.committee_relevance.contains(committee, Qt::CaseInsensitive))
            out.append(t);
    return out;
}

PowerTraderSummary PowerTraderService::filtered_summary(BodyFilter body) const {
    if (body == BodyFilter::All || body == BodyFilter::Cabinet) return summary_;
    PowerTraderSummary s = summary_;
    const MemberChamber want = (body == BodyFilter::Senate)
                               ? MemberChamber::Senate : MemberChamber::House;
    auto& mems = s.members;
    mems.erase(std::remove_if(mems.begin(), mems.end(),
               [&](const CongressMember& m){ return m.chamber != want; }), mems.end());
    auto& trades = s.recent_trades;
    trades.erase(std::remove_if(trades.begin(), trades.end(),
                 [&](const PoliticalTrade& t){ return t.chamber != want; }), trades.end());
    return s;
}

// ── Insider watch list ────────────────────────────────────────────────────────

QVector<InsiderWatchEntry> PowerTraderService::insider_watch_list() const {
    const auto& sec_map = ticker_sector_map();

    // Peer-level benchmarks
    double peer_avg_trade_size  = 0;
    int    total_trades_all     = summary_.recent_trades.size();
    for (const auto& t : summary_.recent_trades)
        peer_avg_trade_size += (t.amount_low + t.amount_high) / 2.0;
    if (total_trades_all > 0) peer_avg_trade_size /= total_trades_all;

    // Cluster detection: ticker → list of (member_id, trade_date) within 14-day window
    QHash<QString, QVector<QPair<QString, QDate>>> ticker_cluster;
    for (const auto& t : summary_.recent_trades)
        if (!t.ticker.isEmpty())
            ticker_cluster[t.ticker].append({t.member_id, t.transaction_date});

    // Build one entry per member
    QHash<QString, InsiderWatchEntry> entries;
    for (const auto& m : summary_.members) {
        InsiderWatchEntry e;
        e.member_id  = m.id;
        e.member_name= m.full_name;
        e.party      = m.party;
        e.chamber    = m.chamber;
        e.state      = m.state;
        e.committees = m.committees;
        entries[m.id] = e;
    }

    // Pass 1: per-trade scoring
    for (const auto& t : summary_.recent_trades) {
        if (!entries.contains(t.member_id)) continue;
        auto& e = entries[t.member_id];
        e.total_trades++;

        const double mid       = (t.amount_low + t.amount_high) / 2.0;
        const QString sector   = sec_map.value(t.ticker, QStringLiteral("Other"));
        const bool cmte_match  = !t.committee_relevance.isEmpty();

        if (cmte_match) {
            e.committee_trades++;
            if (e.top_ticker.isEmpty() || t.signal_score > 0) e.top_ticker    = t.ticker;
            if (e.top_committee.isEmpty()) e.top_committee = t.committee_relevance;
        }
        if (e.top_sector.isEmpty()) e.top_sector = sector;

        e.avg_disclosure_lag += t.disclosure_lag_days;
        if (mid > e.biggest_trade_amt) {
            e.biggest_trade_amt = mid;
            if (e.top_ticker.isEmpty()) e.top_ticker = t.ticker;
        }
    }

    // Pass 2: compute scores per member
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        auto& e = it.value();
        if (e.total_trades == 0) continue;

        e.avg_disclosure_lag /= e.total_trades;
        e.cmte_overlap_pct = (e.total_trades > 0)
                             ? (e.committee_trades * 100.0 / e.total_trades) : 0;

        // ── cmte_overlap_score (0-35) ──────────────────────────────────────────
        // More committee-relevant trades = higher score
        e.cmte_overlap_score = qMin(35.0, e.cmte_overlap_pct * 0.35);

        // ── timing_score (0-25) ───────────────────────────────────────────────
        // Very fast disclosure (< 10d) is normal; very slow (> 45d) is a flag
        // We reward short lag after sells (possible selling before bad news)
        // and penalise long lag (burying a disclosure)
        if (e.avg_disclosure_lag > 45)       e.timing_score = 25;
        else if (e.avg_disclosure_lag > 30)  e.timing_score = 18;
        else if (e.avg_disclosure_lag > 20)  e.timing_score = 10;
        else if (e.avg_disclosure_lag <= 5)  e.timing_score = 12; // suspiciously fast
        else                                 e.timing_score = 5;

        // ── size_score (0-15) ─────────────────────────────────────────────────
        // Trade size relative to the peer average
        if (peer_avg_trade_size > 0) {
            double ratio = e.biggest_trade_amt / peer_avg_trade_size;
            e.size_score = qMin(15.0, ratio * 5.0);
        }

        // ── pattern_score (0-15) ──────────────────────────────────────────────
        // Coordinated trades: same ticker, multiple members, within 14 days
        double cluster_pts = 0;
        const QDate cutoff_cluster = QDate::currentDate().addDays(-90);
        for (const auto& t : summary_.recent_trades) {
            if (t.member_id != e.member_id || t.transaction_date < cutoff_cluster) continue;
            if (t.ticker.isEmpty()) continue;
            const auto& peers = ticker_cluster.value(t.ticker);
            int same_window = 0;
            for (const auto& [pid, pdate] : peers) {
                if (pid != e.member_id && qAbs(pdate.daysTo(t.transaction_date)) <= 14)
                    same_window++;
            }
            if (same_window >= 3) cluster_pts += 15;
            else if (same_window >= 2) cluster_pts += 8;
            else if (same_window >= 1) cluster_pts += 3;
        }
        e.pattern_score = qMin(15.0, cluster_pts);

        // ── return_score (0-10) ───────────────────────────────────────────────
        // Members with high alpha vs SPY = potentially acting on inside info
        const CongressMember* mem = nullptr;
        for (const auto& m : summary_.members)
            if (m.id == e.member_id) { mem = &m; break; }
        if (mem && mem->alpha_ytd > 0)
            e.return_score = qMin(10.0, mem->alpha_ytd / 3.0);

        // ── composite ─────────────────────────────────────────────────────────
        e.insider_score = e.cmte_overlap_score
                        + e.timing_score
                        + e.size_score
                        + e.pattern_score
                        + e.return_score;
        e.insider_score = qMin(100.0, e.insider_score);

        // ── evidence bullets ──────────────────────────────────────────────────
        if (e.cmte_overlap_pct > 50)
            e.evidence_bullets.append(
                QString("%1% of trades in committee-relevant sectors (%2)")
                    .arg(int(e.cmte_overlap_pct)).arg(e.top_committee));
        if (e.avg_disclosure_lag > 40)
            e.evidence_bullets.append(
                QString("Average disclosure lag %1 days — above 45-day STOCK Act deadline")
                    .arg(int(e.avg_disclosure_lag)));
        if (e.biggest_trade_amt > 250'000)
            e.evidence_bullets.append(
                QString("Largest single trade ~$%1 in %2")
                    .arg(e.biggest_trade_amt >= 1e6
                         ? QString::number(e.biggest_trade_amt/1e6,'f',1)+"M"
                         : QString::number(e.biggest_trade_amt/1e3,'f',0)+"K")
                    .arg(e.top_ticker));
        if (e.pattern_score > 8)
            e.evidence_bullets.append(
                QStringLiteral("Coordinated trades: other members traded same tickers within 14 days"));
        if (e.return_score > 6 && mem)
            e.evidence_bullets.append(
                QString("YTD alpha +%1% above SPY").arg(mem->alpha_ytd, 0, 'f', 1));
        if (e.evidence_bullets.isEmpty())
            e.evidence_bullets.append(QStringLiteral("Low insider signal — no strong flags detected"));
    }

    // Collect members with trades
    QVector<InsiderWatchEntry> result;
    for (const auto& e : entries)
        if (e.total_trades > 0)
            result.append(e);

    // Sort: insider_score desc, then cmte_overlap_pct desc
    std::sort(result.begin(), result.end(),
              [](const InsiderWatchEntry& a, const InsiderWatchEntry& b) {
                  if (qAbs(a.insider_score - b.insider_score) > 0.5)
                      return a.insider_score > b.insider_score;
                  return a.cmte_overlap_pct > b.cmte_overlap_pct;
              });
    return result;
}

} // namespace fincept::power_trader
