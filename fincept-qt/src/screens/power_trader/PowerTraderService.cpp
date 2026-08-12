// src/screens/power_trader/PowerTraderService.cpp
#include "screens/power_trader/PowerTraderService.h"

#include "core/logging/Logger.h"
#include "python/PythonRunner.h"
#include "python/PythonWorker.h"
#include "screens/power_trader/DataSourceDialog.h"
#include "screens/power_trader/PositionReplay.h"
#include "services/util/DiskCache.h"

#include <QDate>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSet>
#include <QSettings>
#include <QTimer>

#include <algorithm>
#include <limits>

namespace fincept::power_trader {

namespace {
// Persistent on-disk cache so the next launch paints the most-recent dataset
// immediately (Senate eFD scrape can take 30–90s on cold start). Refreshes
// overwrite atomically; we never delete on app close.
fincept::services::util::DiskCache& disk_cache() {
    static fincept::services::util::DiskCache c(QStringLiteral("power_trader"));
    return c;
}
constexpr const char* kSummaryFile = "summary.json";
} // namespace

// ── Singleton ─────────────────────────────────────────────────────────────────

PowerTraderService& PowerTraderService::instance() {
    static PowerTraderService s;
    return s;
}

PowerTraderService::PowerTraderService(QObject* parent) : QObject(parent) {
    // Restore the user's persisted date-range cutoff (default 90 days).
    // Clamp to a sane range so a corrupted setting can't drive a 0- or
    // 100-year scrape.
    QSettings s;
    const int stored = s.value(QStringLiteral("power_trader/days_back"), 90).toInt();
    days_back_ = std::clamp(stored, 7, 3650);

    refresh_timer_ = new QTimer(this);
    refresh_timer_->setInterval(kRefreshIntervalMs);
    refresh_timer_->setSingleShot(false);
    connect(refresh_timer_, &QTimer::timeout, this, [this]() { load_data(); });

    // Hydrate from disk if we have a prior session's cache. The data_loaded
    // signals emitted from parse_*() go to no listeners (UI wires up later),
    // which is the intended drop — load_data() will re-emit once the UI
    // connects. We override last_updated to the file mtime so the in-memory
    // staleness check uses the real cache age, not "now".
    const auto sum_doc = disk_cache().load(QString::fromLatin1(kSummaryFile));
    if (sum_doc.isObject()) {
        parse_summary(sum_doc.object());
        QFileInfo fi(disk_cache().path(QString::fromLatin1(kSummaryFile)));
        if (fi.exists()) summary_.last_updated = fi.lastModified().toUTC();
        LOG_INFO("PowerTrader",
                 QString("Hydrated %1 members, %2 trades from cache")
                     .arg(summary_.members.size())
                     .arg(summary_.recent_trades.size()));
    }
}

void PowerTraderService::set_days_back(int days) {
    days = std::clamp(days, 7, 3650);
    if (days == days_back_) return;
    days_back_ = days;
    QSettings().setValue(QStringLiteral("power_trader/days_back"), days_back_);

    // Invalidate cached summary so load_data() doesn't short-circuit on the
    // age check and emit the previous range's data.
    summary_.loaded = false;
    summary_.last_updated = QDateTime();
    load_data();
}

// ── Public API ────────────────────────────────────────────────────────────────

void PowerTraderService::load_data() {
    if (loading_) {
        LOG_DEBUG("PowerTrader", "load_data() called while already loading — skipped");
        return;
    }

    // If we have any cached data (from this session or persisted from a prior
    // launch), emit it immediately so the UI paints instantly. Then decide
    // whether to skip or trigger a background refresh based on cache age.
    const bool have_cache = summary_.loaded && summary_.last_updated.isValid();
    if (have_cache) {
        emit data_loaded(summary_);
        const qint64 age_secs = summary_.last_updated.secsTo(QDateTime::currentDateTimeUtc());
        if (age_secs < kRefreshIntervalMs / 1000) {
            return;  // cache fresh; skip network
        }
        LOG_INFO("PowerTrader",
                 QString("Cache is %1h old — refreshing in background").arg(age_secs / 3600));
    }

    loading_ = true;
    LOG_INFO("PowerTrader", "Loading congressional trade data from Senate eFD + House FDS");

    QPointer<PowerTraderService> self = this;
    QJsonObject req;
    req[QStringLiteral("days_back")] = days_back_;

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
            // Persist the raw response so the next launch hydrates instantly.
            disk_cache().save(QString::fromLatin1(kSummaryFile), doc);
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
        m.spy_return_ytd     = o[QStringLiteral("spy_return_ytd")].toDouble();
        // Return and alpha are NOT read from the payload — the script emits a
        // placeholder 0.0 for the return, and they are computed here from real
        // prices in recompute_member_returns(). Deriving alpha at parse time
        // would give every member 0 - spy_return: a fabricated NEGATIVE alpha
        // equal to the benchmark, rendered and SORTED on by the leaderboard
        // and the sidebar (neither gates on return_priced) until prices land —
        // and permanently for a cohort with no priceable tickers, where the
        // price fetch returns early and never recomputes.
        m.portfolio_return_ytd = 0;
        m.alpha_ytd            = 0;
        m.return_priced        = false;
        s.members.append(m);
    }
    // Whether the benchmark exists is a fact the script reports, not something
    // to infer from the number. A flat SPY window legitimately yields ~0.
    s.benchmark_available =
        root.value(QStringLiteral("diagnostics")).toObject()
            .value(QStringLiteral("spy_ytd_fetched")).toBool();


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
        t.placeholder      = o[QStringLiteral("placeholder")].toBool();

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

    // member portfolio_return_ytd / alpha_ytd are computed from REAL prices
    // (fetch_real_prices() below) — never the old fabricated per-sector "gains".
    // Until that async fetch lands they stay 0 and the UI shows "—".
    const double spy_ytd  = s.members.isEmpty() ? 0.0 : s.members.first().spy_return_ytd;
    LOG_INFO("PowerTrader", QString("Loaded %1 members, %2 trades (SPY YTD %3)")
                 .arg(s.members.size()).arg(s.recent_trades.size())
                 .arg(qAbs(spy_ytd) > 1e-9 ? QStringLiteral("%1%").arg(spy_ytd, 0, 'f', 2)
                                           : QStringLiteral("n/a")));
    emit data_loaded(summary_);

    // Kick off real-price valuation. Until it lands, returns/values render "—";
    // when it completes, recompute_member_returns() re-emits with real numbers.
    fetch_real_prices();
}

// ── Real-price valuation (member returns) ─────────────────────────────────────

double PowerTraderService::close_on_or_before(const QString& ticker, const QDate& date) const {
    const auto it = close_history_.constFind(ticker);
    if (it == close_history_.constEnd() || it->isEmpty())
        return 0.0;
    const QMap<QDate, double>& series = it.value();
    // Latest close on or before `date`. upperBound(date) is the first entry
    // strictly after date; step back one for on-or-before.
    auto ub = series.upperBound(date);
    if (ub == series.constBegin())
        return 0.0; // no close at/earlier than the trade date
    --ub;
    return ub.value();
}

void PowerTraderService::fetch_real_prices() {
    // Unique stock/ETF tickers + earliest trade date across all members.
    QSet<QString> ticker_set;
    QDate earliest;
    for (const auto& t : summary_.recent_trades) {
        if (t.asset_type != AssetType::Stock && t.asset_type != AssetType::ETF)
            continue;
        if (t.ticker.isEmpty())
            continue;
        ticker_set.insert(t.ticker);
        if (t.transaction_date.isValid() && (!earliest.isValid() || t.transaction_date < earliest))
            earliest = t.transaction_date;
    }
    if (ticker_set.isEmpty()) {
        // parse_summary() already zeroed every member's return/alpha and set
        // return_priced=false, and this branch returns without emitting, so
        // there is nothing to clear and nobody to tell.
        return;
    }

    QJsonArray syms;
    for (const auto& s : ticker_set) syms.append(s);
    const QString start = (earliest.isValid() ? earliest.addDays(-7) : QDate::currentDate().addYears(-2))
                              .toString(QStringLiteral("yyyy-MM-dd"));
    const QString end   = QDate::currentDate().addDays(1).toString(QStringLiteral("yyyy-MM-dd"));

    const qint64 epoch = ++price_epoch_;
    close_history_.clear();

    // A single daemon call fetches the daily close history for every ticker.
    // The current price is the most recent real close (close_on_or_before(today)),
    // so we don't need the live-quote action — which is deduped across the app
    // and would be dropped if another subsystem had one in flight. batch_closes
    // is PowerTrader-only, so there's no cross-subsystem collision.
    QPointer<PowerTraderService> self = this;
    QJsonObject payload;
    payload[QStringLiteral("symbols")] = syms;
    payload[QStringLiteral("start")]   = start;
    payload[QStringLiteral("end")]     = end;
    python::PythonWorker::instance().submit(
        QStringLiteral("batch_closes"), payload,
        [self, epoch](bool ok, QJsonObject result, QString) {
            if (!self || epoch != self->price_epoch_) return;
            if (ok) {
                const QJsonObject closes = result[QStringLiteral("closes")].toObject();
                for (auto it = closes.constBegin(); it != closes.constEnd(); ++it) {
                    QMap<QDate, double> series;
                    for (const auto& pv : it.value().toArray()) {
                        const auto pair = pv.toArray();
                        if (pair.size() < 2) continue;
                        const QDate d = QDate::fromString(pair[0].toString(), QStringLiteral("yyyy-MM-dd"));
                        const double px = pair[1].toDouble();
                        if (d.isValid() && px > 0.0)
                            series.insert(d, px);
                    }
                    if (!series.isEmpty())
                        self->close_history_.insert(it.key(), series);
                }
            }
            self->recompute_member_returns();
            emit self->data_loaded(self->summary_);
        },
        // A multi-ticker, multi-year close download is large on a cold cache —
        // give it room (it's async/non-blocking and the daemon caches it 1h).
        60'000);
}

void PowerTraderService::recompute_member_returns() {
    for (auto& m : summary_.members) {
        // Both bases. The disclosure-date one is what the screen reports as
        // "return" because it is the only entry a follower could have taken;
        // the trade-date one is the member's own, and the gap between them is
        // what the STOCK Act's 45-day window is worth.
        // Two full replays per member, ~535 members, synchronously on the
        // callback thread. The trade-date pass exists only for the spread, so
        // it is skipped entirely when there is nothing to compare — a member
        // whose filings are all same-day (no lag) has an identical result by
        // construction, and one with no priceable book has neither.
        const auto disc = compute_portfolio(m.id, PriceBasis::DisclosureDate);
        bool any_lag = false;
        for (const auto& t : summary_.recent_trades) {
            // Key on the dates entry_date() ACTUALLY branches on, not on the
            // payload's lag: that lag is computed once per FILING from its
            // first transaction and stamped on every trade in it, so a filing
            // whose first row is same-day stamps 0 onto rows filed 45 days
            // late. Measured on a live snapshot, 533 of 942 trades carry a lag
            // that disagrees with their own dates.
            if (t.member_id == m.id && !t.placeholder &&
                t.disclosure_date.isValid() &&
                t.disclosure_date != t.transaction_date) {
                any_lag = true;
                break;
            }
        }
        const auto trade = (disc.priced && any_lag)
                               ? compute_portfolio(m.id, PriceBasis::TradeDate)
                               : disc;
        if (disc.priced) {
            m.portfolio_return_ytd = disc.est_total_pnl_pct;
            m.return_trade_basis   = trade.priced ? trade.est_total_pnl_pct
                                                  : disc.est_total_pnl_pct;
            m.disclosure_cost_pct  = m.return_trade_basis - m.portfolio_return_ytd;
            m.return_priced        = true;
            // Alpha is only meaningful against a benchmark measured the same
            // way — same window, same total-return basis. spy_return_ytd is
            // absent (0) until that lands, and alpha stays absent with it
            // rather than silently reporting the raw return as alpha.
            m.alpha_ytd = summary_.benchmark_available
                              ? (m.portfolio_return_ytd - m.spy_return_ytd)
                              : 0;
        } else {
            m.return_priced        = false;
            m.portfolio_return_ytd = 0;
            m.return_trade_basis   = 0;
            m.disclosure_cost_pct  = 0;
            m.alpha_ytd            = 0;
        }
    }
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

MemberPortfolio PowerTraderService::compute_portfolio(const QString& member_id,
                                                      PriceBasis basis) const {
    const auto trades = trades_for_member(member_id);
    MemberPortfolio portfolio;
    portfolio.member_id = member_id;
    portfolio.basis     = basis;

    if (trades.isEmpty())
        return portfolio;

    // The entry date for every position below. DisclosureDate is the day the
    // filing became public — the earliest a follower could have acted — while
    // TradeDate is the member's own execution, up to 45 days earlier under the
    // STOCK Act. Falling back to the transaction date when a disclosure date
    // is missing keeps a row priceable; it is the same date in the worst case,
    // not a fabricated one.
    const auto entry_date = [basis](const PoliticalTrade& t) {
        if (basis == PriceBasis::TradeDate)
            return t.transaction_date;
        return t.disclosure_date.isValid() ? t.disclosure_date : t.transaction_date;
    };

    // Aggregate net position per ticker (using midpoints of STOCK/ETF trades only)
    QHash<QString, MemberHolding> positions;
    const auto& sec_map  = ticker_sector_map();

    // Work through trades chronologically for NAV series
    auto sorted = trades;
    std::sort(sorted.begin(), sorted.end(),
              [&entry_date](const PoliticalTrade& a, const PoliticalTrade& b) {
                  return entry_date(a) < entry_date(b);
              });

    QMap<QDate, double> cumulative_nav;  // date → total cost basis at that point

    // Real-price valuation accumulators (per ticker), built from the daemon's
    // trade-date closes. net_shares = Σ (buy $-midpoint / close) − Σ (sell …);
    // priced_gap marks a ticker whose some trade had no real close, so it can't
    // be fully valued (rendered "—" rather than guessed).
    QHash<QString, double> net_shares;
    QHash<QString, bool>   priced_gap;

    // Resolve each trade to its entry-date close, then replay with the pure
    // average-cost engine (PositionReplay). Keeping the arithmetic out of the
    // service is what makes it testable — see tests/screens/test_position_replay.
    QVector<ReplayTrade> replay;
    QVector<QDate>       entry_dates;   // parallel to `replay`, for the NAV series
    replay.reserve(sorted.size());
    entry_dates.reserve(sorted.size());
    for (const auto& t : sorted) {
        if (t.asset_type != AssetType::Stock && t.asset_type != AssetType::ETF)
            continue;
        if (t.placeholder || t.ticker.isEmpty())
            continue;

        const double midpoint = (t.amount_low + t.amount_high) / 2.0;
        auto& h = positions[t.ticker];
        h.ticker     = t.ticker;
        h.asset_name = t.asset_name;
        h.sector     = sec_map.value(t.ticker, QStringLiteral("Other"));
        if (!t.committee_relevance.isEmpty()) {
            h.committee_overlap = true;
            h.committee_name    = t.committee_relevance;
        }

        const QDate on = entry_date(t);
        replay.append({t.ticker, t.direction, midpoint,
                       close_on_or_before(t.ticker, on)});

        entry_dates.append(on);
    }

    // NAV comes from the replay's own cost total, not a parallel running sum.
    // The two disagreed the moment a sell happened: subtracting raw proceeds
    // portfolio-wide drove the series to zero while other tickers were still
    // open, so the chart showed the portfolio collapsing beside a holdings
    // table that still listed positions.
    QVector<double> cost_after;
    const auto replayed = replay_positions(replay, &cost_after);
    for (int i = 0; i < entry_dates.size() && i < cost_after.size(); ++i)
        cumulative_nav[entry_dates.at(i)] = qMax(0.0, cost_after.at(i));
    for (auto it = replayed.cbegin(); it != replayed.cend(); ++it) {
        auto& h = positions[it.key()];
        h.ticker          = it.key();
        h.est_cost_basis  = it.value().cost_basis;
        h.realized_pnl    = it.value().realized_pnl;
        h.buy_count       = it.value().buy_count;
        h.sell_count      = it.value().sell_count;
        net_shares[it.key()] = it.value().shares;
        priced_gap[it.key()] = it.value().priced_gap;
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

    // Value each position with REAL prices: market value = estimated net shares
    // × current price (both from the yfinance daemon). A holding is only valued
    // when it has a current price, positive net shares, and no missing trade-date
    // close (priced_gap) — otherwise est_market_value stays 0 and the UI shows
    // "—". Nothing is fabricated. (Prices arrive async; until then everything is
    // unpriced and shows "—", then re-emits when fetch_real_prices() completes.)
    double priced_value = 0, priced_pnl = 0, priced_cost = 0;
    for (auto& h : portfolio.holdings) {
        h.est_weight = total_cost > 0 ? (h.est_cost_basis / total_cost) * 100.0 : 0;

        // Current price = the most recent real close we have for this ticker.
        const double cur = close_on_or_before(h.ticker, QDate::currentDate());
        const double sh  = net_shares.value(h.ticker, 0.0);
        const bool   priced = cur > 0.0 && sh > 0.0 && !priced_gap.value(h.ticker, false);
        if (priced) {
            h.est_market_value = sh * cur;
            h.est_pnl          = h.est_market_value - h.est_cost_basis;
            h.est_pnl_pct      = h.est_cost_basis > 0 ? (h.est_pnl / h.est_cost_basis) * 100.0 : 0;
            priced_value += h.est_market_value;
            priced_pnl   += h.est_pnl;
            priced_cost  += h.est_cost_basis;
        } else {
            h.est_market_value = 0;  // unpriced → UI shows "—"
            h.est_pnl          = 0;
            h.est_pnl_pct      = 0;
        }
    }

    // Sort by estimated cost basis (the real disclosure-derived magnitude).
    std::sort(portfolio.holdings.begin(), portfolio.holdings.end(),
              [](const MemberHolding& a, const MemberHolding& b) {
                  return a.est_cost_basis > b.est_cost_basis;
              });

    portfolio.est_total_value   = priced_value;
    portfolio.est_total_pnl     = priced_pnl;
    // Realized P&L and the cost that produced it, over every priceable
    // position — including ones closed out entirely, which have no holdings
    // row. Dropping those is what made the leaderboard a ranking of trades
    // that had not worked yet.
    double realized = 0, realized_cost = 0;
    for (auto it = replayed.cbegin(); it != replayed.cend(); ++it) {
        // Skip positions with a price gap, matching the unrealized side.
        // Without this the two halves disagreed: a ticker whose earliest buy
        // predates the fetched history contributes its DOLLARS but no shares,
        // so the average cost it feeds into realized_pnl is inflated — and
        // because realized was unfiltered, that produced priced == true with
        // an empty holdings table, a $0 portfolio value, and a large
        // fabricated return that ranked on the leaderboard.
        if (it.value().priced_gap)
            continue;
        realized      += it.value().realized_pnl;
        realized_cost += it.value().realized_cost;
    }
    portfolio.est_realized_pnl  = realized;
    portfolio.est_realized_cost = realized_cost;
    // Return counts realized and unrealized together, over the cost actually
    // put at risk. Unrealized-only understates any member who takes profits.
    // Denominator is the cost ACTUALLY PUT AT RISK: what is still held plus
    // what has been sold. Dividing realized gains by surviving cost alone gave
    // nonsense — a member who bought $10k, sold at $30k, and holds one $600
    // position elsewhere scored (0 + 20000) / 600 = +3333%, rendered beside a
    // $600 portfolio value. The correct figure is 20000 / 10600 = +188.7%.
    const double invested_cost = priced_cost + realized_cost;
    portfolio.est_total_pnl_pct =
        invested_cost > 0 ? ((priced_pnl + realized) / invested_cost) * 100.0 : 0;
    // "Priced" must include a member whose book is entirely CLOSED: they have
    // no surviving holdings, so gating on priced_cost alone dropped them off
    // the leaderboard with a "—" — the exact behaviour the realized-P&L work
    // exists to end.
    portfolio.priced            = invested_cost > 0;

    // Derived stats
    portfolio.net_buyer_90d     = net_buyer_amount(member_id, 90);
    portfolio.avg_signal_score  = avg_signal_score(member_id);
    portfolio.avg_lag_days      = avg_disclosure_lag(member_id);

    // Best pick = the priced holding with the highest REAL return. Computed
    // inline (not via best_pick(), which would recurse into compute_portfolio).
    {
        QString best_ticker;
        double  best_pct = 0;
        bool    have     = false;
        for (const auto& h : portfolio.holdings) {
            if (h.est_market_value > 0.0 && (!have || h.est_pnl_pct > best_pct)) {
                best_pct    = h.est_pnl_pct;
                best_ticker = h.ticker;
                have        = true;
            }
        }
        portfolio.best_pick_ticker  = best_ticker;
        portfolio.best_pick_pnl_pct = best_pct;
    }

    return portfolio;
}

// ── Sector breakdown ──────────────────────────────────────────────────────────

QVector<SectorExposure> PowerTraderService::sector_breakdown() const {
    const auto& sec_map = ticker_sector_map();
    QHash<QString, SectorExposure> sectors;

    for (const auto& t : summary_.recent_trades) {
        if (!in_active_body(t.chamber)) continue;
        // A filing stub has an empty ticker and $0, so it landed in "Other"
        // and inflated that row's trade/member counts against no dollars.
        if (t.placeholder) continue;
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

    // Total trades per member. Filing stubs are excluded — they can never
    // match a committee, so counting them only deflates the overlap share.
    QHash<QString, int> total_trades;
    for (const auto& t : summary_.recent_trades)
        if (!t.placeholder && in_active_body(t.chamber)) total_trades[t.member_id]++;

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
            // Real alpha when the member's portfolio is priced, else "—".
            if (m.return_priced) {
                r.rank_value = m.alpha_ytd;
                r.rank_label = (m.alpha_ytd >= 0 ? QStringLiteral("+") : QString())
                               + QString::number(m.alpha_ytd, 'f', 1) + QStringLiteral("%");
            } else {
                r.rank_value = 0;
                r.rank_label = QString(QChar(0x2014));
            }
            break;
        case RankingDimension::Return:
            if (m.return_priced) {
                r.rank_value = m.portfolio_return_ytd;
                r.rank_label = (m.portfolio_return_ytd >= 0 ? QStringLiteral("+") : QString())
                               + QString::number(m.portfolio_return_ytd, 'f', 1) + QStringLiteral("%");
            } else {
                r.rank_value = 0;
                r.rank_label = QString(QChar(0x2014));
            }
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
            if (l < 0) {
                // No measurable lag (filing stubs only). Sort to the BOTTOM
                // and dash it — never to the top as a model filer.
                r.rank_value = -std::numeric_limits<double>::max();
                r.rank_label = QStringLiteral("—");
            } else {
                r.rank_value = -l;  // negate: higher lag = worse rank
                r.rank_label = QString::number(l, 'f', 1) + QStringLiteral("d avg");
            }
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
    // Placeholders are skipped here and in avg_disclosure_lag: a House filing
    // stub carries a forced signal_score of 0 and a fabricated 0-day lag, so
    // including them drags the average of a real Senate filer toward zero and
    // hands House members a perfect disclosure-lag rank they did not earn.
    double sum = 0; int n = 0;
    for (const auto& t : summary_.recent_trades)
        if (t.member_id == mid && !t.placeholder) { sum += t.signal_score; ++n; }
    return n > 0 ? sum / n : 0;
}

// Returns -1 when the member has no REAL trade to measure. That distinction is
// load-bearing: every House row is a filing stub, so a House member has no
// measurable lag at all — and returning 0.0 put them at the very top of the
// "fastest discloser" ranking, presenting "no data" as "filed same day". Same
// absent-not-zero rule the Return/Alpha columns already follow.
double PowerTraderService::avg_disclosure_lag(const QString& mid) const {
    double sum = 0; int n = 0;
    for (const auto& t : summary_.recent_trades)
        if (t.member_id == mid && !t.placeholder) { sum += t.disclosure_lag_days; ++n; }
    return n > 0 ? sum / n : -1.0;
}

QPair<QString, double> PowerTraderService::best_pick(const QString& mid) const {
    // Real best pick comes from compute_portfolio's price-based per-holding
    // returns. (compute_portfolio computes it inline and no longer calls back
    // here, so this is not recursive.) Empty ticker until prices are available.
    const auto p = compute_portfolio(mid);
    return {p.best_pick_ticker, p.best_pick_pnl_pct};
}

// ── Signal Builder ────────────────────────────────────────────────────────────

QVector<TradeFactorScores> PowerTraderService::compute_trade_base_scores() const {
    // Build herd map: ticker → list of (member_id, date) for cluster detection
    QHash<QString, QVector<QPair<QString, QDate>>> herd_map;
    for (const auto& t : summary_.recent_trades)
        if (!t.ticker.isEmpty())
            herd_map[t.ticker].append({t.member_id, t.transaction_date});

    // Member avg signal (0-100) for history factor
    // Placeholders excluded, matching avg_signal_score(). This is a second,
    // parallel computation of the same quantity; fixing only the other one
    // left the history factor still dragged toward 0 for House members.
    QHash<QString, double> member_avg_sig;
    QHash<QString, int>    member_trade_cnt;
    for (const auto& t : summary_.recent_trades) {
        if (t.placeholder) continue;
        member_avg_sig[t.member_id] += t.signal_score;
        member_trade_cnt[t.member_id]++;
    }

    QVector<TradeFactorScores> result;
    result.reserve(summary_.recent_trades.size());

    for (const auto& t : summary_.recent_trades) {
        TradeFactorScores s;
        s.trade_id = t.id;

        // A filing stub carries no ticker, no amount and no real trade date,
        // so every factor below would score it on fabricated inputs. Leave all
        // factors at 0 and mark it unscoreable; the panel dashes it.
        if (t.placeholder) {
            s.unscoreable = true;
            result.append(s);
            continue;
        }

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

    // Share of these members' trades that touch a ticker this committee
    // oversees. A proportion — see the field comment for why it is not called
    // a correlation.
    // Stubs excluded: they can never carry a committee_relevance, so counting
    // them only deflates the share. Same fix as committee_insider_signals().
    QHash<QString, int> member_total;
    for (const auto& t : summary_.recent_trades)
        if (!t.placeholder && in_active_body(t.chamber)) member_total[t.member_id]++;

    QVector<CommitteeGroup> result;
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        auto& g = it.value();
        if (g.trade_count > 0)
            g.avg_signal_score /= g.trade_count;
        // share: trades-with-overlap / (total trades of members on this committee)
        int total_for_members = 0;
        for (const auto& mid : g.member_ids)
            total_for_members += member_total.value(mid, 0);
        g.committee_share_pct = total_for_members > 0
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
        // Stubs contribute a 0 signal score, a fabricated 0-day lag and $0,
        // then divide into the averages below — dragging BOTH parties' figures
        // toward zero in proportion to House filing volume. Their empty ticker
        // also accumulated a blank key that could surface in top_tickers.
        if (t.placeholder) continue;
        if (!in_active_body(t.chamber)) continue;
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

void PowerTraderService::set_body_filter(BodyFilter body) {
    body_filter_ = body;
}

bool PowerTraderService::in_active_body(MemberChamber chamber) const {
    switch (body_filter_) {
        case BodyFilter::Senate: return chamber == MemberChamber::Senate;
        case BodyFilter::House:  return chamber == MemberChamber::House;
        case BodyFilter::All:
            break;
    }
    return true;
}

PowerTraderSummary PowerTraderService::filtered_summary(BodyFilter body) const {
    if (body == BodyFilter::All) return summary_;
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
    // Peer average trade size, over REAL trades only: a filing stub reports a
    // $0 amount, so including stubs dragged the peer mean down and inflated
    // every member's size_score against it.
    double peer_avg_trade_size  = 0;
    int    total_trades_all     = 0;
    for (const auto& t : summary_.recent_trades) {
        if (t.placeholder || !in_active_body(t.chamber)) continue;
        peer_avg_trade_size += (t.amount_low + t.amount_high) / 2.0;
        ++total_trades_all;
    }
    if (total_trades_all > 0) peer_avg_trade_size /= total_trades_all;

    // Cluster detection: ticker → list of (member_id, trade_date) within 14-day window
    QHash<QString, QVector<QPair<QString, QDate>>> ticker_cluster;
    for (const auto& t : summary_.recent_trades)
        if (!t.ticker.isEmpty())
            ticker_cluster[t.ticker].append({t.member_id, t.transaction_date});

    // Build one entry per member
    QHash<QString, InsiderWatchEntry> entries;
    for (const auto& m : summary_.members) {
        if (!in_active_body(m.chamber)) continue;
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
        // A House filing stub has no ticker, no amount and no real trade date.
        // Counting it inflated total_trades — the denominator of
        // cmte_overlap_pct — so a member's committee-overlap share was diluted
        // by rows that could never match a committee, and its fabricated
        // 0-day lag pulled avg_disclosure_lag toward "suspiciously fast".
        if (t.placeholder) continue;
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
