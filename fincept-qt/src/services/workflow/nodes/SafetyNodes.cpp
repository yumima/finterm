#include "services/workflow/nodes/SafetyNodes.h"

#include "services/workflow/NodeRegistry.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>

#include <QTimeZone>

#include <cmath>
#include <optional>

namespace fincept::workflow {

namespace {

// A numeric input field, or nullopt when absent / non-numeric / non-finite.
// Safety checks fail CLOSED on a missing field — never default it to 0.
std::optional<double> num_field(const QJsonObject& obj, const char* key) {
    const QJsonValue v = obj.value(QLatin1String(key));
    if (!v.isDouble())
        return std::nullopt;
    const double d = v.toDouble();
    if (!std::isfinite(d))
        return std::nullopt;
    return d;
}

// Route a check to its fail branch because required inputs are missing.
void fail_closed_missing(QJsonObject obj, const char* passed_key, const char* failures_key,
                         const QStringList& missing, const std::function<void(bool, QJsonValue, QString)>& cb) {
    obj[QLatin1String(passed_key)] = false;
    obj["_branch"] = "false";
    QJsonArray fa;
    for (const QString& m : missing)
        fa.append(QString("missing input: %1").arg(m));
    obj[QLatin1String(failures_key)] = fa;
    obj["missing_inputs"] = QJsonArray::fromStringList(missing);
    cb(true, obj, {});
}

} // namespace

void register_safety_nodes(NodeRegistry& registry) {

    // ── Risk Check ────────────────────────────────────────────────
    // Input: { symbol, quantity, price, portfolio_value, volatility }
    // Checks position size % and volatility against thresholds.
    registry.register_type({
        .type_id = "safety.risk_check",
        .display_name = "Risk Check",
        .category = "Safety",
        .description = "Validate trade against position size and volatility limits",
        .icon_text = "!",
        .accent_color = "#dc2626",
        .version = 2,
        .inputs = {{"input_0", "Trade In", PortDirection::Input, ConnectionType::Main}},
        .outputs =
            {
                {"output_pass", "Pass", PortDirection::Output, ConnectionType::Main},
                {"output_fail", "Fail", PortDirection::Output, ConnectionType::Main},
            },
        .parameters =
            {
                {"max_position_pct", "Max Position %", "number", 5.0, {}, "Max % of portfolio per position"},
                {"max_volatility", "Max Volatility", "number", 0.5, {}, "Max annualized volatility (0-1)"},
            },
        .execute =
            [](const QJsonObject& params, const QVector<QJsonValue>& inputs,
               std::function<void(bool, QJsonValue, QString)> cb) {
                auto data = inputs.isEmpty() ? QJsonValue{} : inputs[0];
                QJsonObject trade = data.isObject() ? data.toObject() : QJsonObject{};

                double max_pos_pct = params.value("max_position_pct").toDouble(5.0);
                double max_vol = params.value("max_volatility").toDouble(0.5);

                const auto quantity = num_field(trade, "quantity");
                const auto price = num_field(trade, "price");
                const auto portfolio_value = num_field(trade, "portfolio_value");
                // Volatility as a 0-1 fraction: `volatility`, or analytics.risk_analysis's
                // `annualized_volatility` (fraction), or `annualized_vol` (percent).
                std::optional<double> volatility = num_field(trade, "volatility");
                if (!volatility)
                    volatility = num_field(trade, "annualized_volatility");
                if (!volatility) {
                    if (auto pct = num_field(trade, "annualized_vol"))
                        volatility = *pct / 100.0;
                }

                QStringList missing;
                if (!quantity || *quantity <= 0)
                    missing << "quantity";
                if (!price || *price <= 0)
                    missing << "price";
                if (!portfolio_value || *portfolio_value <= 0)
                    missing << "portfolio_value";
                if (!volatility || *volatility < 0)
                    missing << "volatility";
                if (!missing.isEmpty()) {
                    fail_closed_missing(trade, "risk_check_passed", "risk_failures", missing, cb);
                    return;
                }

                QStringList failures;

                const double position_value = *quantity * *price;
                const double position_pct = (position_value / *portfolio_value) * 100.0;
                if (position_pct > max_pos_pct)
                    failures << QString("Position size %1% exceeds max %2%")
                                    .arg(position_pct, 0, 'f', 2)
                                    .arg(max_pos_pct, 0, 'f', 2);

                if (*volatility > max_vol)
                    failures
                        << QString("Volatility %1 exceeds max %2").arg(*volatility, 0, 'f', 3).arg(max_vol, 0, 'f', 3);

                QJsonObject out = trade;
                bool passed = failures.isEmpty();
                out["risk_check_passed"] = passed;
                out["_branch"] = passed ? "true" : "false";
                if (!passed) {
                    QJsonArray fa;
                    for (const QString& f : failures)
                        fa.append(f);
                    out["risk_failures"] = fa;
                }
                cb(true, out, {});
            },
    });

    // ── Loss Limit ────────────────────────────────────────────────
    // Input: { daily_pnl, weekly_pnl } — checks against configured limits.
    registry.register_type({
        .type_id = "safety.loss_limit",
        .display_name = "Loss Limit",
        .category = "Safety",
        .description = "Enforce daily/weekly loss limits",
        .icon_text = "!",
        .accent_color = "#dc2626",
        .version = 2,
        .inputs = {{"input_0", "Data In", PortDirection::Input, ConnectionType::Main}},
        .outputs =
            {
                {"output_pass", "Under Limit", PortDirection::Output, ConnectionType::Main},
                {"output_fail", "Over Limit", PortDirection::Output, ConnectionType::Main},
            },
        .parameters =
            {
                {"daily_limit", "Daily Loss Limit", "number", 1000, {}, "$ amount"},
                {"weekly_limit", "Weekly Loss Limit", "number", 5000, {}, "$ amount"},
            },
        .execute =
            [](const QJsonObject& params, const QVector<QJsonValue>& inputs,
               std::function<void(bool, QJsonValue, QString)> cb) {
                auto data = inputs.isEmpty() ? QJsonValue{} : inputs[0];
                QJsonObject obj = data.isObject() ? data.toObject() : QJsonObject{};

                double daily_limit = params.value("daily_limit").toDouble(1000);
                double weekly_limit = params.value("weekly_limit").toDouble(5000);
                const auto daily = num_field(obj, "daily_pnl");
                const auto weekly = num_field(obj, "weekly_pnl");
                QStringList missing;
                if (!daily)
                    missing << "daily_pnl";
                if (!weekly)
                    missing << "weekly_pnl";
                if (!missing.isEmpty()) {
                    fail_closed_missing(obj, "loss_limit_passed", "loss_failures", missing, cb);
                    return;
                }
                const double daily_pnl = *daily;
                const double weekly_pnl = *weekly;

                QStringList failures;
                if (-daily_pnl > daily_limit)
                    failures << QString("Daily loss $%1 exceeds limit $%2")
                                    .arg(-daily_pnl, 0, 'f', 2)
                                    .arg(daily_limit, 0, 'f', 2);
                if (-weekly_pnl > weekly_limit)
                    failures << QString("Weekly loss $%1 exceeds limit $%2")
                                    .arg(-weekly_pnl, 0, 'f', 2)
                                    .arg(weekly_limit, 0, 'f', 2);

                bool passed = failures.isEmpty();
                obj["loss_limit_passed"] = passed;
                obj["_branch"] = passed ? "true" : "false";
                if (!passed) {
                    QJsonArray fa;
                    for (const QString& f : failures)
                        fa.append(f);
                    obj["loss_failures"] = fa;
                }
                cb(true, obj, {});
            },
    });

    // ── Position Size Limit ───────────────────────────────────────
    // Input: { quantity, price }
    registry.register_type({
        .type_id = "safety.position_size_limit",
        .display_name = "Position Size Limit",
        .category = "Safety",
        .description = "Enforce maximum position sizing",
        .icon_text = "!",
        .accent_color = "#dc2626",
        .version = 2,
        .inputs = {{"input_0", "Data In", PortDirection::Input, ConnectionType::Main}},
        .outputs =
            {
                {"output_pass", "Pass", PortDirection::Output, ConnectionType::Main},
                {"output_fail", "Fail", PortDirection::Output, ConnectionType::Main},
            },
        .parameters =
            {
                {"max_shares", "Max Shares", "number", 1000, {}, ""},
                {"max_value", "Max Value ($)", "number", 50000, {}, ""},
            },
        .execute =
            [](const QJsonObject& params, const QVector<QJsonValue>& inputs,
               std::function<void(bool, QJsonValue, QString)> cb) {
                auto data = inputs.isEmpty() ? QJsonValue{} : inputs[0];
                QJsonObject obj = data.isObject() ? data.toObject() : QJsonObject{};

                double max_shares = params.value("max_shares").toDouble(1000);
                double max_value = params.value("max_value").toDouble(50000);
                const auto qty_in = num_field(obj, "quantity");
                const auto price_in = num_field(obj, "price");
                QStringList missing;
                if (!qty_in || *qty_in <= 0)
                    missing << "quantity";
                if (!price_in || *price_in <= 0)
                    missing << "price";
                if (!missing.isEmpty()) {
                    fail_closed_missing(obj, "size_limit_passed", "size_failures", missing, cb);
                    return;
                }
                const double quantity = *qty_in;
                const double price = *price_in;
                const double trade_val = quantity * price;

                QStringList failures;
                if (quantity > max_shares)
                    failures << QString("Quantity %1 exceeds max %2").arg(quantity).arg(max_shares);
                if (trade_val > max_value)
                    failures << QString("Trade value $%1 exceeds max $%2")
                                    .arg(trade_val, 0, 'f', 2)
                                    .arg(max_value, 0, 'f', 2);

                bool passed = failures.isEmpty();
                obj["size_limit_passed"] = passed;
                obj["_branch"] = passed ? "true" : "false";
                if (!passed) {
                    QJsonArray fa;
                    for (const QString& f : failures)
                        fa.append(f);
                    obj["size_failures"] = fa;
                }
                cb(true, obj, {});
            },
    });

    // ── Trading Hours Check ───────────────────────────────────────
    // Checks weekday session hours in the exchange's local time zone
    // (holiday calendars are not consulted; output says so).
    registry.register_type({
        .type_id = "safety.trading_hours",
        .display_name = "Trading Hours Check",
        .category = "Safety",
        .description = "Validate trade is within market hours",
        .icon_text = "!",
        .accent_color = "#dc2626",
        .version = 2,
        .inputs = {{"input_0", "Data In", PortDirection::Input, ConnectionType::Main}},
        .outputs =
            {
                {"output_open", "Market Open", PortDirection::Output, ConnectionType::Main},
                {"output_closed", "Market Closed", PortDirection::Output, ConnectionType::Main},
            },
        .parameters =
            {
                {"exchange", "Exchange", "select", "NYSE", {"NYSE", "NASDAQ", "LSE", "TSE", "NSE", "BSE"}, ""},
                {"allow_premarket", "Allow Pre-Market", "boolean", false, {}, ""},
            },
        .execute =
            [](const QJsonObject& params, const QVector<QJsonValue>& inputs,
               std::function<void(bool, QJsonValue, QString)> cb) {
                auto data = inputs.isEmpty() ? QJsonValue{} : inputs[0];
                QJsonObject obj = data.isObject() ? data.toObject() : QJsonObject{};

                QString exchange = params.value("exchange").toString("NYSE");
                bool allow_premarket = params.value("allow_premarket").toBool(false);

                // Evaluate in the exchange's own time zone so DST shifts are
                // honoured (fixed UTC windows were an hour off half the year).
                // Session times are exchange-local HHMM.
                struct Session {
                    const char* tz;
                    int open;
                    int close;
                    int pre_open;
                };
                Session session{"America/New_York", 930, 1600, 400};
                if (exchange == "LSE")
                    session = {"Europe/London", 800, 1630, 700};
                else if (exchange == "TSE")
                    session = {"Asia/Tokyo", 900, 1530, 800};
                else if (exchange == "NSE" || exchange == "BSE")
                    session = {"Asia/Kolkata", 915, 1530, 900};

                const QDateTime utc_now = QDateTime::currentDateTimeUtc();
                const QTimeZone tz(QByteArray(session.tz));
                if (!tz.isValid()) {
                    cb(false, {}, QString("Trading Hours: time zone %1 unavailable on this system").arg(session.tz));
                    return;
                }
                const QDateTime local_now = utc_now.toTimeZone(tz);
                const int day_of_week = local_now.date().dayOfWeek(); // 1=Mon, 7=Sun
                const int time_local = local_now.time().hour() * 100 + local_now.time().minute();
                const bool is_weekend = (day_of_week == 6 || day_of_week == 7);

                bool in_regular = !is_weekend && time_local >= session.open && time_local < session.close;
                // Tokyo has a lunch break 11:30-12:30.
                if (exchange == "TSE" && time_local >= 1130 && time_local < 1230)
                    in_regular = false;
                bool in_premarket = !is_weekend && time_local >= session.pre_open && time_local < session.open;
                bool is_open = in_regular || (allow_premarket && in_premarket);

                obj["market_open"] = is_open;
                obj["exchange"] = exchange;
                obj["utc_time"] = utc_now.toString("HH:mm");
                obj["exchange_local_time"] = local_now.toString("yyyy-MM-dd HH:mm");
                // Exchange holiday calendars are not consulted — weekday hours only.
                obj["holidays_checked"] = false;
                obj["session_type"] = in_regular ? "regular" : (in_premarket ? "pre_market" : "closed");
                // Route: output_open = true branch, output_closed = false branch
                obj["_branch"] = is_open ? "true" : "false";
                cb(true, obj, {});
            },
    });

    // ── Max Drawdown Check ────────────────────────────────────────
    // Input: { peak_value, current_value } or { drawdown_pct }
    registry.register_type({
        .type_id = "safety.max_drawdown_check",
        .display_name = "Max Drawdown Check",
        .category = "Safety",
        .description = "Halt trading if drawdown exceeds threshold",
        .icon_text = "!",
        .accent_color = "#dc2626",
        .version = 2,
        .inputs = {{"input_0", "Data In", PortDirection::Input, ConnectionType::Main}},
        .outputs =
            {
                {"output_pass", "Under Limit", PortDirection::Output, ConnectionType::Main},
                {"output_fail", "Exceeded", PortDirection::Output, ConnectionType::Main},
            },
        .parameters =
            {
                {"max_drawdown_pct", "Max Drawdown %", "number", 10.0, {}, ""},
                {"window", "Window", "select", "daily", {"daily", "weekly", "monthly", "ytd"}, ""},
            },
        .execute =
            [](const QJsonObject& params, const QVector<QJsonValue>& inputs,
               std::function<void(bool, QJsonValue, QString)> cb) {
                auto data = inputs.isEmpty() ? QJsonValue{} : inputs[0];
                QJsonObject obj = data.isObject() ? data.toObject() : QJsonObject{};

                double max_dd = params.value("max_drawdown_pct").toDouble(10.0);

                // Accept either drawdown_pct directly or peak/current values.
                // Neither present → fail closed (never assume a 0% drawdown).
                std::optional<double> dd = num_field(obj, "drawdown_pct");
                if (dd && *dd < 0)
                    dd = std::abs(*dd); // some sources report drawdown as a negative %
                if (!dd) {
                    const auto peak = num_field(obj, "peak_value");
                    const auto current = num_field(obj, "current_value");
                    if (peak && current && *peak > 0)
                        dd = (*peak - *current) / *peak * 100.0;
                }
                if (!dd) {
                    obj["max_drawdown_pct"] = max_dd;
                    fail_closed_missing(obj, "drawdown_check_passed", "drawdown_failures",
                                        {"drawdown_pct (or peak_value + current_value)"}, cb);
                    return;
                }
                const double drawdown_pct = *dd;

                bool passed = drawdown_pct <= max_dd;
                obj["drawdown_pct"] = drawdown_pct;
                obj["max_drawdown_pct"] = max_dd;
                obj["drawdown_check_passed"] = passed;
                obj["_branch"] = passed ? "true" : "false";
                cb(true, obj, {});
            },
    });

    // ── Correlation Check ─────────────────────────────────────────
    // Input: { correlation } — checks against threshold.
    registry.register_type({
        .type_id = "safety.correlation_check",
        .display_name = "Correlation Check",
        .category = "Safety",
        .description = "Block trade if too correlated with existing positions",
        .icon_text = "!",
        .accent_color = "#dc2626",
        .version = 2,
        .inputs = {{"input_0", "Data In", PortDirection::Input, ConnectionType::Main}},
        .outputs =
            {
                {"output_pass", "Low Correlation", PortDirection::Output, ConnectionType::Main},
                {"output_fail", "High Correlation", PortDirection::Output, ConnectionType::Main},
            },
        .parameters =
            {
                {"max_correlation", "Max Correlation", "number", 0.8, {}, "0-1 threshold"},
                {"lookback_days", "Lookback Days", "number", 90, {}, ""},
            },
        .execute =
            [](const QJsonObject& params, const QVector<QJsonValue>& inputs,
               std::function<void(bool, QJsonValue, QString)> cb) {
                auto data = inputs.isEmpty() ? QJsonValue{} : inputs[0];
                QJsonObject obj = data.isObject() ? data.toObject() : QJsonObject{};

                double max_corr = params.value("max_correlation").toDouble(0.8);
                const auto corr_in = num_field(obj, "correlation");
                if (!corr_in) {
                    fail_closed_missing(obj, "correlation_check_passed", "correlation_failures", {"correlation"}, cb);
                    return;
                }
                const double corr = std::abs(*corr_in);

                bool passed = corr <= max_corr;
                obj["correlation_check_passed"] = passed;
                obj["correlation_abs"] = corr;
                obj["_branch"] = passed ? "true" : "false";
                cb(true, obj, {});
            },
    });

    // ── Volatility Filter ─────────────────────────────────────────
    // Input: { annualized_vol } or { vix }
    registry.register_type({
        .type_id = "safety.volatility_filter",
        .display_name = "Volatility Filter",
        .category = "Safety",
        .description = "Skip trade if volatility is too high",
        .icon_text = "!",
        .accent_color = "#dc2626",
        .version = 2,
        .inputs = {{"input_0", "Data In", PortDirection::Input, ConnectionType::Main}},
        .outputs =
            {
                {"output_pass", "Normal Vol", PortDirection::Output, ConnectionType::Main},
                {"output_fail", "High Vol", PortDirection::Output, ConnectionType::Main},
            },
        .parameters =
            {
                {"max_annualized_vol", "Max Ann. Vol %", "number", 50.0, {}, ""},
                {"method", "Method", "select", "realized", {"realized", "implied", "vix_level"}, ""},
                {"vix_threshold", "VIX Threshold", "number", 30.0, {}, "For VIX method"},
            },
        .execute =
            [](const QJsonObject& params, const QVector<QJsonValue>& inputs,
               std::function<void(bool, QJsonValue, QString)> cb) {
                auto data = inputs.isEmpty() ? QJsonValue{} : inputs[0];
                QJsonObject obj = data.isObject() ? data.toObject() : QJsonObject{};

                QString method = params.value("method").toString("realized");
                double max_vol = params.value("max_annualized_vol").toDouble(50.0);
                double vix_threshold = params.value("vix_threshold").toDouble(30.0);

                bool exceeded = false;
                QString reason;

                if (method == "vix_level") {
                    const auto vix = num_field(obj, "vix");
                    if (!vix) {
                        fail_closed_missing(obj, "volatility_check_passed", "volatility_failures", {"vix"}, cb);
                        return;
                    }
                    exceeded = *vix > vix_threshold;
                    reason = QString("VIX %1 > threshold %2").arg(*vix).arg(vix_threshold);
                } else {
                    // realized or implied, compared as a 0-100 percentage. Accept
                    // `annualized_vol` (percent) or analytics.risk_analysis's
                    // `annualized_volatility` (0-1 fraction → ×100).
                    std::optional<double> vol_pct = num_field(obj, "annualized_vol");
                    if (!vol_pct) {
                        if (auto frac = num_field(obj, "annualized_volatility"))
                            vol_pct = *frac * 100.0;
                    }
                    if (!vol_pct) {
                        fail_closed_missing(obj, "volatility_check_passed", "volatility_failures",
                                            {"annualized_vol (%) or annualized_volatility (fraction)"}, cb);
                        return;
                    }
                    exceeded = *vol_pct > max_vol;
                    reason = QString("Ann. vol %1% > max %2%").arg(*vol_pct, 0, 'f', 1).arg(max_vol, 0, 'f', 1);
                }

                obj["volatility_check_passed"] = !exceeded;
                obj["_branch"] = exceeded ? "false" : "true";
                if (exceeded)
                    obj["volatility_failures"] = QJsonArray{reason};
                cb(true, obj, {});
            },
    });
}

} // namespace fincept::workflow
