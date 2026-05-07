// src/screens/power_trader/PowerTraderService.cpp
#include "screens/power_trader/PowerTraderService.h"

#include "core/logging/Logger.h"
#include "python/PythonWorker.h"

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
    LOG_INFO("PowerTrader", "Loading congressional trade data (stub: mock generator)");

    // ── STUB: generate mock data synchronously ────────────────────────────────
    // When the real Python script `senate_disclosures` is ready, replace this
    // block with:
    //
    //   QPointer<PowerTraderService> self = this;
    //   QJsonObject payload;
    //   payload["limit"] = 100;
    //   python::PythonWorker::instance().submit(
    //       "senate_disclosures", payload,
    //       [self](bool ok, QJsonObject result, QString err) {
    //           if (!self) return;
    //           self->on_daemon_response(ok, result, err);
    //       },
    //       python::PythonWorker::kNetworkActionTimeoutMs);
    //
    // The rest of this file (parse_summary, signals) will work unchanged.

    generate_mock_data();
    loading_ = false;

    if (!refresh_timer_->isActive())
        refresh_timer_->start();
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
    LOG_INFO("PowerTrader", QString("Loaded %1 members, %2 trades")
                 .arg(s.members.size()).arg(s.recent_trades.size()));
    emit data_loaded(summary_);
}

// ── Mock data generator ───────────────────────────────────────────────────────

CongressMember PowerTraderService::make_member(const QString& id,
                                               const QString& name,
                                               const QString& party,
                                               MemberChamber chamber,
                                               const QString& state,
                                               const QStringList& committees,
                                               double net_worth,
                                               int trades_ytd,
                                               double portfolio_ret,
                                               double spy_ret) {
    CongressMember m;
    m.id                  = id;
    m.full_name           = name;
    m.party               = party;
    m.chamber             = chamber;
    m.state               = state;
    m.committees          = committees;
    m.term_start          = QDate(2021, 1, 3);
    m.estimated_net_worth = net_worth;
    m.trade_count_ytd     = trades_ytd;
    m.portfolio_return_ytd= portfolio_ret;
    m.spy_return_ytd      = spy_ret;
    m.alpha_ytd           = portfolio_ret - spy_ret;
    return m;
}

PoliticalTrade PowerTraderService::make_trade(int idx,
                                               const CongressMember& member,
                                               const QString& ticker,
                                               const QString& asset_name,
                                               TradeDirection direction,
                                               double amount_low,
                                               double amount_high,
                                               int txn_days_ago,
                                               int lag_days,
                                               double signal_score,
                                               AssetType asset_type) {
    PoliticalTrade t;
    t.id               = QStringLiteral("trade-%1").arg(idx, 4, 10, QChar('0'));
    t.member_id        = member.id;
    t.member_name      = member.full_name;
    t.party            = member.party;
    t.chamber          = member.chamber;
    t.transaction_date = QDate::currentDate().addDays(-txn_days_ago);
    t.disclosure_date  = t.transaction_date.addDays(lag_days);
    t.disclosure_lag_days = lag_days;
    t.ticker           = ticker;
    t.asset_name       = asset_name;
    t.asset_type       = asset_type;
    t.direction        = direction;
    t.amount_low       = amount_low;
    t.amount_high      = amount_high;

    // Build human-readable range label
    auto fmt = [](double v) -> QString {
        if (v >= 1'000'000) return QStringLiteral("$%1M").arg(v / 1'000'000, 0, 'f', 1);
        if (v >= 1'000)     return QStringLiteral("$%1K").arg(v / 1'000, 0, 'f', 0);
        return QStringLiteral("$%1").arg(v, 0, 'f', 0);
    };
    t.amount_range_label = QStringLiteral("%1 – %2").arg(fmt(amount_low), fmt(amount_high));
    t.signal_score = signal_score;
    t.source_url   = QStringLiteral("https://efts.congress.gov/EFTS/API/financial-disclosure-search/%1")
                         .arg(t.id);
    return t;
}

void PowerTraderService::generate_mock_data() {
    PowerTraderSummary s;
    s.last_updated = QDateTime::currentDateTimeUtc();
    s.loaded = true;

    // ── Members ───────────────────────────────────────────────────────────────
    using C = MemberChamber;

    s.members = {
        make_member("nancy-pelosi",      "Nancy Pelosi",       "D", C::House,  "CA",
                    {"Financial Services", "Ways and Means"},
                    114'000'000, 42, 31.4, 12.2),
        make_member("tommy-tuberville",  "Tommy Tuberville",   "R", C::Senate, "AL",
                    {"Armed Services", "Agriculture"},
                    9'000'000, 18, 24.7, 12.2),
        make_member("susan-collins",     "Susan Collins",      "R", C::Senate, "ME",
                    {"Appropriations", "Finance"},
                    2'800'000, 6, 14.1, 12.2),
        make_member("jon-ossoff",        "Jon Ossoff",         "D", C::Senate, "GA",
                    {"Banking", "Intelligence"},
                    3'200'000, 9, 18.9, 12.2),
        make_member("ron-johnson",       "Ron Johnson",        "R", C::Senate, "WI",
                    {"Homeland Security", "Finance"},
                    35'000'000, 14, 9.3, 12.2),
        make_member("mark-kelly",        "Mark Kelly",         "D", C::Senate, "AZ",
                    {"Armed Services", "Commerce"},
                    8'400'000, 11, 16.2, 12.2),
        make_member("dan-crenshaw",      "Dan Crenshaw",       "R", C::House,  "TX",
                    {"Energy and Commerce", "Homeland Security"},
                    4'600'000, 22, 27.8, 12.2),
        make_member("gillibrand-kirsten","Kirsten Gillibrand",  "D", C::Senate, "NY",
                    {"Armed Services", "Finance"},
                    5'100'000, 7, 11.4, 12.2),
        make_member("michael-mccaul",    "Michael McCaul",     "R", C::House,  "TX",
                    {"Foreign Affairs", "Homeland Security"},
                    69'000'000, 31, 22.5, 12.2),
        make_member("virginia-foxx",     "Virginia Foxx",      "R", C::House,  "NC",
                    {"Education", "Rules"},
                    7'200'000, 8, 13.7, 12.2),
        make_member("josh-gottheimer",   "Josh Gottheimer",    "D", C::House,  "NJ",
                    {"Financial Services"},
                    18'000'000, 25, 29.3, 12.2),
        make_member("pete-sessions",     "Pete Sessions",      "R", C::House,  "TX",
                    {"Rules", "Homeland Security"},
                    4'800'000, 12, 8.6, 12.2),
        make_member("david-perdue",      "David Perdue",       "R", C::Senate, "GA",
                    {"Banking", "Budget"},
                    16'000'000, 19, 19.8, 12.2),
        make_member("shelley-capito",    "Shelley Moore Capito","R", C::Senate, "WV",
                    {"Appropriations", "Environment"},
                    6'500'000, 5, 7.4, 12.2),
        make_member("raul-grijalva",     "Raul Grijalva",      "D", C::House,  "AZ",
                    {"Natural Resources", "Education"},
                    1'200'000, 3, 6.1, 12.2),
        make_member("pat-toomey",        "Pat Toomey",         "R", C::Senate, "PA",
                    {"Banking", "Finance", "Budget"},
                    7'800'000, 17, 21.3, 12.2),
        make_member("maria-cantwell",    "Maria Cantwell",     "D", C::Senate, "WA",
                    {"Commerce", "Finance", "Energy"},
                    4'200'000, 13, 17.6, 12.2),
        make_member("bill-hagerty",      "Bill Hagerty",       "R", C::Senate, "TN",
                    {"Banking", "Foreign Relations", "Appropriations"},
                    22'000'000, 27, 26.1, 12.2),
        make_member("greg-meeks",        "Gregory Meeks",      "D", C::House,  "NY",
                    {"Financial Services", "Foreign Affairs"},
                    2'100'000, 10, 15.8, 12.2),
        make_member("rick-scott",        "Rick Scott",         "R", C::Senate, "FL",
                    {"Commerce", "Armed Services", "Budget"},
                    260'000'000, 34, 33.7, 12.2),
    };

    // ── Trades ────────────────────────────────────────────────────────────────
    // 100 trades spread across members, a realistic mix of tickers and sectors.

    struct RawTrade {
        const char* member_id;
        const char* ticker;
        const char* asset_name;
        TradeDirection dir;
        double lo, hi;
        int txn_ago, lag;
        double sig;
        AssetType at;
    };

    static const RawTrade raw[] = {
        {"nancy-pelosi",      "NVDA", "NVIDIA Corp",             TradeDirection::Buy,  15001,  50000,  8,  12, 87.3, AssetType::Stock},
        {"nancy-pelosi",      "MSFT", "Microsoft Corp",          TradeDirection::Buy,  50001, 100000, 15,  9, 76.1, AssetType::Stock},
        {"nancy-pelosi",      "TSLA", "Tesla Inc — Call Options", TradeDirection::Buy, 100001, 250000, 22, 18, 91.5, AssetType::Option},
        {"nancy-pelosi",      "GOOGL","Alphabet Inc",             TradeDirection::Sell, 50001, 100000, 35, 14, 62.4, AssetType::Stock},
        {"nancy-pelosi",      "AMZN", "Amazon.com Inc",           TradeDirection::Buy,  15001,  50000, 50, 11, 74.9, AssetType::Stock},
        {"tommy-tuberville",  "LMT",  "Lockheed Martin",          TradeDirection::Buy, 100001, 250000,  4,  8, 88.0, AssetType::Stock},
        {"tommy-tuberville",  "RTX",  "Raytheon Technologies",    TradeDirection::Buy,  15001,  50000,  9,  7, 81.2, AssetType::Stock},
        {"tommy-tuberville",  "NOC",  "Northrop Grumman",         TradeDirection::Buy,  50001, 100000, 18, 22, 84.5, AssetType::Stock},
        {"tommy-tuberville",  "GD",   "General Dynamics",         TradeDirection::Buy,  15001,  50000, 33, 15, 79.3, AssetType::Stock},
        {"tommy-tuberville",  "BA",   "Boeing Co",                TradeDirection::Sell, 50001, 100000, 45, 20, 45.1, AssetType::Stock},
        {"susan-collins",     "JNJ",  "Johnson & Johnson",        TradeDirection::Buy,   1001,  15000, 12, 18, 41.7, AssetType::Stock},
        {"susan-collins",     "PFE",  "Pfizer Inc",               TradeDirection::Sell,  1001,  15000, 28, 24, 38.2, AssetType::Stock},
        {"susan-collins",     "SPY",  "SPDR S&P 500 ETF",         TradeDirection::Buy,   1001,  15000, 60, 31, 29.4, AssetType::ETF},
        {"jon-ossoff",        "AAPL", "Apple Inc",                TradeDirection::Buy,  15001,  50000,  6, 10, 68.9, AssetType::Stock},
        {"jon-ossoff",        "META", "Meta Platforms",           TradeDirection::Buy,  50001, 100000, 14, 13, 72.3, AssetType::Stock},
        {"jon-ossoff",        "CRM",  "Salesforce Inc",           TradeDirection::Buy,  15001,  50000, 25, 16, 65.1, AssetType::Stock},
        {"ron-johnson",       "AMZN", "Amazon.com Inc",           TradeDirection::Sell, 50001, 100000, 11, 19, 55.8, AssetType::Stock},
        {"ron-johnson",       "MSFT", "Microsoft Corp",           TradeDirection::Buy,  15001,  50000, 20, 11, 61.2, AssetType::Stock},
        {"ron-johnson",       "XOM",  "Exxon Mobil Corp",         TradeDirection::Buy,  50001, 100000, 38, 14, 58.7, AssetType::Stock},
        {"mark-kelly",        "ASTR", "Astra Space Inc",          TradeDirection::Buy,   1001,  15000,  7, 11, 71.4, AssetType::Stock},
        {"mark-kelly",        "RKLB", "Rocket Lab USA",           TradeDirection::Buy,   1001,  15000, 16,  9, 77.6, AssetType::Stock},
        {"mark-kelly",        "BA",   "Boeing Co",                TradeDirection::Buy,  15001,  50000, 29, 23, 69.3, AssetType::Stock},
        {"mark-kelly",        "LMT",  "Lockheed Martin",          TradeDirection::Sell, 15001,  50000, 42, 28, 52.1, AssetType::Stock},
        {"dan-crenshaw",      "CVX",  "Chevron Corp",             TradeDirection::Buy,  50001, 100000,  3,  8, 80.2, AssetType::Stock},
        {"dan-crenshaw",      "XOM",  "Exxon Mobil Corp",         TradeDirection::Buy,  50001, 100000,  5,  6, 83.7, AssetType::Stock},
        {"dan-crenshaw",      "SLB",  "Schlumberger NV",          TradeDirection::Buy,  15001,  50000, 13,  9, 76.4, AssetType::Stock},
        {"dan-crenshaw",      "HAL",  "Halliburton Co",           TradeDirection::Buy,  15001,  50000, 21, 12, 74.9, AssetType::Stock},
        {"dan-crenshaw",      "PSX",  "Phillips 66",              TradeDirection::Sell, 50001, 100000, 39, 17, 48.3, AssetType::Stock},
        {"gillibrand-kirsten","ABBV", "AbbVie Inc",               TradeDirection::Buy,   1001,  15000, 17, 22, 44.6, AssetType::Stock},
        {"gillibrand-kirsten","MRK",  "Merck & Co",               TradeDirection::Buy,   1001,  15000, 44, 29, 39.8, AssetType::Stock},
        {"michael-mccaul",    "AAPL", "Apple Inc",                TradeDirection::Buy, 100001, 250000,  2,  7, 82.1, AssetType::Stock},
        {"michael-mccaul",    "MSFT", "Microsoft Corp",           TradeDirection::Buy, 250001, 500000,  8, 10, 85.3, AssetType::Stock},
        {"michael-mccaul",    "AMZN", "Amazon.com Inc",           TradeDirection::Buy, 100001, 250000, 19, 14, 80.7, AssetType::Stock},
        {"michael-mccaul",    "NVDA", "NVIDIA Corp",              TradeDirection::Buy,  50001, 100000, 26, 11, 89.2, AssetType::Stock},
        {"michael-mccaul",    "GOOGL","Alphabet Inc",             TradeDirection::Sell,100001, 250000, 47, 20, 60.4, AssetType::Stock},
        {"virginia-foxx",     "SPY",  "SPDR S&P 500 ETF",        TradeDirection::Buy,   1001,  15000, 30, 25, 32.1, AssetType::ETF},
        {"virginia-foxx",     "BRK-B","Berkshire Hathaway B",     TradeDirection::Buy,   1001,  15000, 55, 33, 35.6, AssetType::Stock},
        {"josh-gottheimer",   "V",    "Visa Inc",                 TradeDirection::Buy,  50001, 100000,  4, 10, 72.8, AssetType::Stock},
        {"josh-gottheimer",   "MA",   "Mastercard Inc",           TradeDirection::Buy,  50001, 100000,  7, 12, 76.5, AssetType::Stock},
        {"josh-gottheimer",   "JPM",  "JPMorgan Chase",           TradeDirection::Buy, 100001, 250000, 15, 16, 78.9, AssetType::Stock},
        {"josh-gottheimer",   "GS",   "Goldman Sachs",            TradeDirection::Buy,  50001, 100000, 23, 18, 74.3, AssetType::Stock},
        {"josh-gottheimer",   "MS",   "Morgan Stanley",           TradeDirection::Sell, 15001,  50000, 40, 22, 51.7, AssetType::Stock},
        {"pete-sessions",     "DIS",  "Walt Disney Co",           TradeDirection::Sell, 15001,  50000, 27, 14, 43.2, AssetType::Stock},
        {"pete-sessions",     "CMCSA","Comcast Corp",             TradeDirection::Sell, 15001,  50000, 36, 19, 41.8, AssetType::Stock},
        {"pete-sessions",     "NFLX", "Netflix Inc",              TradeDirection::Buy,  15001,  50000, 52, 27, 58.4, AssetType::Stock},
        {"david-perdue",      "AAPL", "Apple Inc",                TradeDirection::Buy,  50001, 100000,  9, 13, 67.9, AssetType::Stock},
        {"david-perdue",      "AMZN", "Amazon.com Inc",           TradeDirection::Buy, 100001, 250000, 17, 17, 71.2, AssetType::Stock},
        {"david-perdue",      "HD",   "Home Depot Inc",           TradeDirection::Buy,  15001,  50000, 28, 21, 64.7, AssetType::Stock},
        {"david-perdue",      "LOW",  "Lowe's Companies",         TradeDirection::Sell, 15001,  50000, 41, 30, 49.3, AssetType::Stock},
        {"shelley-capito",    "NEE",  "NextEra Energy",           TradeDirection::Buy,   1001,  15000, 34, 28, 38.9, AssetType::Stock},
        {"shelley-capito",    "DUK",  "Duke Energy Corp",         TradeDirection::Buy,   1001,  15000, 58, 35, 36.1, AssetType::Stock},
        {"raul-grijalva",     "SPY",  "SPDR S&P 500 ETF",        TradeDirection::Buy,   1001,   5000, 20, 18, 28.3, AssetType::ETF},
        {"raul-grijalva",     "VTI",  "Vanguard Total Mkt ETF",  TradeDirection::Buy,   1001,   5000, 48, 24, 26.7, AssetType::ETF},
        {"pat-toomey",        "JPM",  "JPMorgan Chase",           TradeDirection::Buy,  50001, 100000,  6, 11, 73.4, AssetType::Stock},
        {"pat-toomey",        "BAC",  "Bank of America Corp",     TradeDirection::Buy,  50001, 100000, 14, 15, 70.8, AssetType::Stock},
        {"pat-toomey",        "WFC",  "Wells Fargo & Co",         TradeDirection::Buy,  15001,  50000, 24, 19, 66.3, AssetType::Stock},
        {"pat-toomey",        "C",    "Citigroup Inc",            TradeDirection::Sell, 50001, 100000, 37, 23, 50.2, AssetType::Stock},
        {"pat-toomey",        "USB",  "U.S. Bancorp",             TradeDirection::Buy,  15001,  50000, 49, 26, 62.1, AssetType::Stock},
        {"maria-cantwell",    "AMZN", "Amazon.com Inc",           TradeDirection::Buy, 100001, 250000,  5, 10, 79.6, AssetType::Stock},
        {"maria-cantwell",    "MSFT", "Microsoft Corp",           TradeDirection::Buy,  50001, 100000, 11, 12, 77.3, AssetType::Stock},
        {"maria-cantwell",    "SBUX", "Starbucks Corp",           TradeDirection::Buy,   1001,  15000, 22, 16, 53.8, AssetType::Stock},
        {"maria-cantwell",    "COST", "Costco Wholesale",         TradeDirection::Sell, 15001,  50000, 43, 21, 47.5, AssetType::Stock},
        {"bill-hagerty",      "JPM",  "JPMorgan Chase",           TradeDirection::Buy, 100001, 250000,  3,  9, 81.4, AssetType::Stock},
        {"bill-hagerty",      "MS",   "Morgan Stanley",           TradeDirection::Buy,  50001, 100000,  8, 11, 78.7, AssetType::Stock},
        {"bill-hagerty",      "GS",   "Goldman Sachs",            TradeDirection::Buy, 100001, 250000, 16, 14, 82.3, AssetType::Stock},
        {"bill-hagerty",      "BLK",  "BlackRock Inc",            TradeDirection::Buy,  50001, 100000, 31, 18, 75.6, AssetType::Stock},
        {"bill-hagerty",      "SCHW", "Charles Schwab Corp",      TradeDirection::Sell, 15001,  50000, 46, 25, 53.4, AssetType::Stock},
        {"greg-meeks",        "V",    "Visa Inc",                 TradeDirection::Buy,  15001,  50000, 10, 14, 64.2, AssetType::Stock},
        {"greg-meeks",        "MA",   "Mastercard Inc",           TradeDirection::Buy,  15001,  50000, 19, 17, 62.9, AssetType::Stock},
        {"greg-meeks",        "WM",   "Waste Management Inc",     TradeDirection::Buy,   1001,  15000, 32, 22, 45.8, AssetType::Stock},
        {"rick-scott",        "AAPL", "Apple Inc",                TradeDirection::Buy, 250001, 500000,  2,  8, 85.7, AssetType::Stock},
        {"rick-scott",        "NVDA", "NVIDIA Corp",              TradeDirection::Buy, 500001,1000000,  4,  7, 92.4, AssetType::Stock},
        {"rick-scott",        "MSFT", "Microsoft Corp",           TradeDirection::Buy, 250001, 500000,  7, 10, 87.1, AssetType::Stock},
        {"rick-scott",        "AMZN", "Amazon.com Inc",           TradeDirection::Buy, 500001,1000000, 12,  9, 90.3, AssetType::Stock},
        {"rick-scott",        "GOOGL","Alphabet Inc",             TradeDirection::Buy, 250001, 500000, 18, 12, 86.8, AssetType::Stock},
        {"rick-scott",        "META", "Meta Platforms",           TradeDirection::Buy, 100001, 250000, 25, 15, 83.5, AssetType::Stock},
        {"rick-scott",        "TSLA", "Tesla Inc",                TradeDirection::Sell,250001, 500000, 38, 21, 58.9, AssetType::Stock},
        {"rick-scott",        "NFLX", "Netflix Inc",              TradeDirection::Buy, 100001, 250000, 55, 28, 70.4, AssetType::Stock},
        {"nancy-pelosi",      "AMD",  "Advanced Micro Devices",   TradeDirection::Buy,  50001, 100000, 62, 16, 78.5, AssetType::Stock},
        {"nancy-pelosi",      "CRM",  "Salesforce Inc",           TradeDirection::Sell, 50001, 100000, 74, 19, 61.3, AssetType::Stock},
        {"tommy-tuberville",  "LDOS", "Leidos Holdings",          TradeDirection::Buy,  50001, 100000, 56, 18, 82.9, AssetType::Stock},
        {"tommy-tuberville",  "SAIC", "Science Applications Intl",TradeDirection::Buy,  50001, 100000, 67, 23, 80.1, AssetType::Stock},
        {"dan-crenshaw",      "PXD",  "Pioneer Natural Resources", TradeDirection::Buy, 50001, 100000, 71, 14, 77.8, AssetType::Stock},
        {"dan-crenshaw",      "OXY",  "Occidental Petroleum",     TradeDirection::Buy,  50001, 100000, 83, 17, 75.3, AssetType::Stock},
        {"josh-gottheimer",   "BX",   "Blackstone Inc",           TradeDirection::Buy, 100001, 250000, 63, 20, 76.9, AssetType::Stock},
        {"josh-gottheimer",   "KKR",  "KKR & Co Inc",             TradeDirection::Buy,  50001, 100000, 77, 24, 74.2, AssetType::Stock},
        {"bill-hagerty",      "AXP",  "American Express Co",      TradeDirection::Buy,  50001, 100000, 68, 16, 72.6, AssetType::Stock},
        {"michael-mccaul",    "QCOM", "Qualcomm Inc",             TradeDirection::Buy,  50001, 100000, 59, 13, 79.4, AssetType::Stock},
        {"michael-mccaul",    "TSM",  "Taiwan Semiconductor ADR", TradeDirection::Buy, 100001, 250000, 70, 16, 83.1, AssetType::Stock},
        {"pat-toomey",        "SPGI", "S&P Global Inc",           TradeDirection::Buy,  50001, 100000, 82, 22, 68.7, AssetType::Stock},
        {"pat-toomey",        "ICE",  "Intercontinental Exchange",TradeDirection::Buy,  50001, 100000, 91, 27, 65.4, AssetType::Stock},
        {"rick-scott",        "UNH",  "UnitedHealth Group",       TradeDirection::Buy, 100001, 250000, 73, 14, 81.2, AssetType::Stock},
        {"rick-scott",        "CVS",  "CVS Health Corp",          TradeDirection::Sell, 50001, 100000, 86, 18, 54.3, AssetType::Stock},
        {"maria-cantwell",    "GOOG", "Alphabet Inc (C shares)",  TradeDirection::Buy,  50001, 100000, 79, 19, 71.8, AssetType::Stock},
        {"mark-kelly",        "SPCE", "Virgin Galactic Holdings", TradeDirection::Buy,   1001,  15000, 88, 22, 66.2, AssetType::Stock},
    };

    // Map member_id -> CongressMember for quick lookup
    QHash<QString, int> member_idx;
    for (int i = 0; i < s.members.size(); ++i)
        member_idx[s.members[i].id] = i;

    // Committee → trade relevance annotations
    static const QHash<QString, QString> ticker_committee = {
        {"LMT", "Armed Services"}, {"RTX", "Armed Services"}, {"NOC", "Armed Services"},
        {"GD",  "Armed Services"}, {"BA",  "Armed Services"}, {"LDOS","Armed Services"},
        {"SAIC","Armed Services"}, {"RKLB","Armed Services"}, {"ASTR","Armed Services"},
        {"CVX", "Energy"},         {"XOM", "Energy"},         {"SLB", "Energy"},
        {"HAL", "Energy"},         {"PSX", "Energy"},         {"PXD", "Energy"},
        {"OXY", "Energy"},         {"NEE", "Energy"},         {"DUK", "Energy"},
        {"JPM", "Banking"},        {"BAC", "Banking"},        {"WFC", "Banking"},
        {"C",   "Banking"},        {"GS",  "Banking"},        {"MS",  "Banking"},
        {"BX",  "Banking"},        {"KKR", "Banking"},        {"AXP", "Finance"},
        {"V",   "Financial Services"}, {"MA","Financial Services"},
        {"AAPL","Commerce"},       {"MSFT","Commerce"},       {"AMZN","Commerce"},
        {"GOOGL","Commerce"},      {"GOOG","Commerce"},       {"META","Commerce"},
        {"NVDA","Commerce"},       {"AMD", "Commerce"},       {"QCOM","Commerce"},
        {"TSM", "Commerce"},       {"TSLA","Commerce"},
        {"JNJ", "Health"},         {"PFE", "Health"},         {"ABBV","Health"},
        {"MRK", "Health"},         {"UNH", "Health"},         {"CVS", "Health"},
    };

    int idx = 1;
    for (const auto& r : raw) {
        const QString mid = QString::fromLatin1(r.member_id);
        if (!member_idx.contains(mid))
            continue;
        const auto& mem = s.members[member_idx[mid]];
        auto t = make_trade(idx++, mem,
                            QString::fromLatin1(r.ticker),
                            QString::fromLatin1(r.asset_name),
                            r.dir, r.lo, r.hi, r.txn_ago, r.lag, r.sig, r.at);

        // Annotate committee relevance
        const QString ticker_up = t.ticker.toUpper();
        if (ticker_committee.contains(ticker_up)) {
            const QString relevant = ticker_committee[ticker_up];
            for (const auto& c : mem.committees) {
                if (c.contains(relevant, Qt::CaseInsensitive)) {
                    t.committee_relevance = c;
                    break;
                }
            }
        }
        s.recent_trades.append(t);
    }

    // Sort trades by disclosure date descending (most recent first)
    std::stable_sort(s.recent_trades.begin(), s.recent_trades.end(),
                     [](const PoliticalTrade& a, const PoliticalTrade& b) {
                         return a.disclosure_date > b.disclosure_date;
                     });

    summary_ = s;
    LOG_INFO("PowerTrader",
             QString("Mock data generated: %1 members, %2 trades")
                 .arg(s.members.size()).arg(s.recent_trades.size()));
    emit data_loaded(summary_);
}

} // namespace fincept::power_trader
