#include "services/workflow/nodes/AnalyticsNodes.h"

#include "python/PythonRunner.h"
#include "services/workflow/NodeRegistry.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>

#include <algorithm>
#include <cmath>

namespace fincept::workflow {

using fincept::python::extract_json;
using fincept::python::PythonResult;
using fincept::python::PythonRunner;

namespace {

// Helper: run a Python script and parse the JSON result.
void analytics_run_python_json(const QString& script, const QStringList& args,
                               std::function<void(bool, QJsonValue, QString)> cb) {
    PythonRunner::instance().run(script, args, [cb](const PythonResult& res) {
        if (!res.success) {
            cb(false, {}, res.error);
            return;
        }
        QString json_str = extract_json(res.output).trimmed();
        auto doc = QJsonDocument::fromJson(json_str.toUtf8());
        if (doc.isNull()) {
            cb(false, {}, "Invalid JSON: " + res.output.left(200));
            return;
        }
        // Check for Python-level error: {"success": false, ...} or a bare
        // {"error": ...} (string, object, or true + "message"). Either is a failure.
        if (doc.isObject()) {
            auto obj = doc.object();
            const QJsonValue e = obj.value("error");
            const bool has_error = !(e.isUndefined() || e.isNull() || (e.isBool() && !e.toBool()) ||
                                     (e.isString() && e.toString().isEmpty()));
            const bool explicit_fail = obj.contains("success") && !obj.value("success").toBool(true);
            if (explicit_fail || (has_error && !obj.value("success").toBool(false))) {
                QString msg;
                if (e.isString())
                    msg = e.toString();
                else if (e.isObject())
                    msg = e.toObject().value("error").toString(e.toObject().value("message").toString());
                if (msg.isEmpty())
                    msg = obj.value("message").toString();
                cb(false, {}, msg.isEmpty() ? QString("Python script returned failure") : msg);
                return;
            }
        }
        cb(true, doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(doc.array()), {});
    });
}

// Nodes whose analysis has no backing implementation. compute_technicals.py
// ignores --indicator and always returns the standard technicals set, so
// routing these through it produced output unrelated to the node's label.
void analysis_not_supported(const QString& what, const std::function<void(bool, QJsonValue, QString)>& cb) {
    cb(false, {}, QString("%1 is not supported yet — no backing implementation is wired").arg(what));
}

// Inverse standard normal CDF (Acklam's rational approximation, |err| < 1.2e-9).
double inv_norm_cdf(double p) {
    static const double a[] = {-3.969683028665376e+01, 2.209460984245205e+02, -2.759285104469687e+02,
                               1.383577518672690e+02,  -3.066479806614716e+01, 2.506628277459239e+00};
    static const double b[] = {-5.447609879822406e+01, 1.615858368580409e+02, -1.556989798598866e+02,
                               6.680131188771972e+01,  -1.328068155288572e+01};
    static const double c[] = {-7.784894002430293e-03, -3.223964580411365e-01, -2.400758277161838e+00,
                               -2.549732539343734e+00, 4.374664141464968e+00,  2.938163982698783e+00};
    static const double d[] = {7.784695709041462e-03, 3.224671290700398e-01, 2.445134137142996e+00,
                               3.754408661907416e+00};
    const double plow = 0.02425, phigh = 1 - plow;
    if (p < plow) {
        const double q = std::sqrt(-2 * std::log(p));
        return (((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) /
               ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1);
    }
    if (p > phigh) {
        const double q = std::sqrt(-2 * std::log(1 - p));
        return -(((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) /
               ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1);
    }
    const double q = p - 0.5, r = q * q;
    return (((((a[0] * r + a[1]) * r + a[2]) * r + a[3]) * r + a[4]) * r + a[5]) * q /
           (((((b[0] * r + b[1]) * r + b[2]) * r + b[3]) * r + b[4]) * r + 1);
}

// A ratio that is undefined (zero denominator) is reported as null, not 0.
QJsonValue ratio_or_null(double num, double den) {
    return den > 0 ? QJsonValue(num / den) : QJsonValue(QJsonValue::Null);
}

} // anonymous namespace

void register_analytics_nodes(NodeRegistry& registry) {
    registry.register_type({
        .type_id = "analytics.technical_indicators",
        .display_name = "Technical Indicators",
        .category = "Analytics",
        .description = "Calculate SMA, RSI, MACD, Bollinger Bands, etc.",
        .icon_text = "A",
        .accent_color = "#7c3aed",
        .version = 1,
        .inputs = {{"input_0", "Price Data", PortDirection::Input, ConnectionType::PriceData}},
        .outputs = {{"output_main", "Main", PortDirection::Output, ConnectionType::TechnicalData}},
        .parameters =
            {
                {"indicator",
                 "Indicator",
                 "select",
                 "SMA",
                 {"SMA", "EMA", "RSI", "MACD", "BBANDS", "ATR", "STOCH", "ADX", "CCI", "WILLR", "OBV", "VWAP"},
                 "",
                 true},
                {"period", "Period", "number", 14, {}, "Lookback period"},
                {"symbol", "Symbol", "string", "", {}, "Ticker symbol"},
            },
        .execute =
            [](const QJsonObject& params, const QVector<QJsonValue>& inputs,
               std::function<void(bool, QJsonValue, QString)> cb) {
                // Serialise input price data to pass as a JSON string arg.
                QJsonValue input_val = inputs.isEmpty() ? QJsonValue{} : inputs[0];
                QJsonDocument input_doc;
                if (input_val.isArray())
                    input_doc = QJsonDocument(input_val.toArray());
                else if (input_val.isObject())
                    input_doc = QJsonDocument(input_val.toObject());

                QString json_data =
                    input_doc.isNull() ? "{}" : QString::fromUtf8(input_doc.toJson(QJsonDocument::Compact));

                QString indicator = params.value("indicator").toString("SMA");
                QString period = QString::number(static_cast<int>(params.value("period").toDouble(14)));
                QString symbol = params.value("symbol").toString();

                QStringList args = {"--data", json_data, "--indicator", indicator, "--period", period};
                if (!symbol.isEmpty())
                    args << "--symbol" << symbol;

                analytics_run_python_json("compute_technicals.py", args, cb);
            },
    });

    registry.register_type({
        .type_id = "analytics.backtest",
        .display_name = "Backtest Engine",
        .category = "Analytics",
        .description = "Run backtesting simulation on a trading strategy",
        .icon_text = "A",
        .accent_color = "#7c3aed",
        .version = 1,
        .inputs = {{"input_0", "Strategy", PortDirection::Input, ConnectionType::Main}},
        .outputs = {{"output_main", "Results", PortDirection::Output, ConnectionType::BacktestData}},
        .parameters =
            {
                {"start_date", "Start Date", "string", "2023-01-01", {}, "YYYY-MM-DD"},
                {"end_date", "End Date", "string", "2024-01-01", {}, "YYYY-MM-DD"},
                {"initial_capital", "Initial Capital", "number", 100000, {}, ""},
                {"commission", "Commission %", "number", 0.001, {}, ""},
            },
        .execute =
            [](const QJsonObject&, const QVector<QJsonValue>&, std::function<void(bool, QJsonValue, QString)> cb) {
                analysis_not_supported("Backtest Engine", cb);
            },
    });

    registry.register_type({
        .type_id = "analytics.portfolio_optimization",
        .display_name = "Portfolio Optimization",
        .category = "Analytics",
        .description = "Optimize portfolio allocation (mean-variance, Black-Litterman)",
        .icon_text = "A",
        .accent_color = "#7c3aed",
        .version = 1,
        .inputs = {{"input_0", "Holdings", PortDirection::Input, ConnectionType::PortfolioData}},
        .outputs = {{"output_main", "Optimized", PortDirection::Output, ConnectionType::PortfolioData}},
        .parameters =
            {
                {"method",
                 "Method",
                 "select",
                 "mean_variance",
                 {"mean_variance", "black_litterman", "risk_parity", "min_variance", "max_sharpe"},
                 ""},
                {"risk_free_rate", "Risk-Free Rate", "number", 0.05, {}, ""},
            },
        .execute =
            [](const QJsonObject& params, const QVector<QJsonValue>& inputs,
               std::function<void(bool, QJsonValue, QString)> cb) {
                // Build a JSON args object containing the input data and params.
                QJsonObject args_obj;
                args_obj["method"] = params.value("method").toString("mean_variance");
                args_obj["risk_free_rate"] = params.value("risk_free_rate").toDouble(0.05);
                if (!inputs.isEmpty())
                    args_obj["holdings"] = inputs[0];

                QString json_args = QString::fromUtf8(QJsonDocument(args_obj).toJson(QJsonDocument::Compact));

                analytics_run_python_json("optimize_portfolio_weights.py", {"--args", json_args}, cb);
            },
    });

    registry.register_type({
        .type_id = "analytics.performance_metrics",
        .display_name = "Performance Metrics",
        .category = "Analytics",
        .description = "Calculate returns, Sharpe, Sortino, max drawdown",
        .icon_text = "A",
        .accent_color = "#7c3aed",
        .version = 1,
        .inputs = {{"input_0", "Data In", PortDirection::Input, ConnectionType::Main}},
        .outputs = {{"output_main", "Main", PortDirection::Output, ConnectionType::Main}},
        .parameters =
            {
                {"benchmark", "Benchmark", "string", "SPY", {}, "Benchmark symbol"},
                {"risk_free_rate", "Risk-Free Rate", "number", 0.05, {}, ""},
            },
        .execute =
            [](const QJsonObject& params, const QVector<QJsonValue>& inputs,
               std::function<void(bool, QJsonValue, QString)> cb) {
                // Extract close prices from input data and compute basic metrics
                QVector<double> prices;
                if (!inputs.isEmpty()) {
                    if (inputs[0].isArray()) {
                        for (const QJsonValue& v : inputs[0].toArray()) {
                            if (v.isObject()) {
                                double p = v.toObject().value("Close").toDouble(
                                    v.toObject().value("close").toDouble(v.toObject().value("price").toDouble(0)));
                                if (p > 0)
                                    prices.append(p);
                            } else if (v.isDouble()) {
                                prices.append(v.toDouble());
                            }
                        }
                    }
                }

                if (prices.size() < 2) {
                    cb(false, {}, "Need at least 2 price points for performance metrics");
                    return;
                }

                // Compute daily returns
                QVector<double> returns;
                for (int i = 1; i < prices.size(); ++i)
                    returns.append((prices[i] - prices[i - 1]) / prices[i - 1]);

                double sum = 0, sum_sq = 0, max_dd = 0, peak = prices[0];
                double neg_sum_sq = 0;
                int neg_count = 0;
                for (double r : returns) {
                    sum += r;
                    sum_sq += r * r;
                    if (r < 0) {
                        neg_sum_sq += r * r;
                        ++neg_count;
                    }
                }
                for (double p : prices) {
                    if (p > peak)
                        peak = p;
                    double dd = (peak - p) / peak;
                    if (dd > max_dd)
                        max_dd = dd;
                }

                int n = returns.size();
                double mean_return = sum / n;
                double std_dev = std::sqrt(sum_sq / n - mean_return * mean_return);
                double rfr = params.value("risk_free_rate").toDouble(0.05) / 252.0;
                // Undefined ratios (zero volatility / no down days) are null, not 0.
                const QJsonValue sharpe = ratio_or_null((mean_return - rfr) * std::sqrt(252.0), std_dev);
                double downside_dev = neg_count > 0 ? std::sqrt(neg_sum_sq / neg_count) : 0;
                const QJsonValue sortino = ratio_or_null((mean_return - rfr) * std::sqrt(252.0), downside_dev);
                double total_return = (prices.back() - prices.front()) / prices.front();
                double annualized = std::pow(1.0 + total_return, 252.0 / n) - 1.0;

                QJsonObject out;
                out["total_return"] = total_return;
                out["annualized_return"] = annualized;
                out["sharpe_ratio"] = sharpe;
                out["sortino_ratio"] = sortino;
                out["max_drawdown"] = max_dd;
                out["volatility"] = std_dev * std::sqrt(252.0);
                out["mean_daily_return"] = mean_return;
                out["data_points"] = n;
                cb(true, out, {});
            },
    });

    registry.register_type({
        .type_id = "analytics.correlation_matrix",
        .display_name = "Correlation Matrix",
        .category = "Analytics",
        .description = "Calculate asset correlation matrix",
        .icon_text = "A",
        .accent_color = "#7c3aed",
        .version = 1,
        .inputs = {{"input_0", "Data In", PortDirection::Input, ConnectionType::PriceData}},
        .outputs = {{"output_main", "Main", PortDirection::Output, ConnectionType::Main}},
        .parameters =
            {
                {"method", "Method", "select", "pearson", {"pearson", "spearman", "kendall"}, ""},
                {"period", "Period", "string", "1y", {}, "Lookback period"},
            },
        .execute =
            [](const QJsonObject&, const QVector<QJsonValue>&, std::function<void(bool, QJsonValue, QString)> cb) {
                analysis_not_supported("Correlation Matrix", cb);
            },
    });

    registry.register_type({
        .type_id = "analytics.risk_analysis",
        .display_name = "Risk Analysis",
        .category = "Analytics",
        .description = "Historical or parametric (Gaussian) VaR and CVaR",
        .icon_text = "A",
        .accent_color = "#7c3aed",
        .version = 1,
        .inputs = {{"input_0", "Portfolio", PortDirection::Input, ConnectionType::PortfolioData}},
        .outputs = {{"output_main", "Main", PortDirection::Output, ConnectionType::RiskData}},
        .parameters =
            {
                {"method",
                 "Method",
                 "select",
                 "historical_var",
                 {"historical_var", "parametric_var"},
                 ""},
                {"confidence", "Confidence Level", "number", 0.95, {}, ""},
                {"horizon", "Horizon (days)", "number", 1, {}, ""},
            },
        .execute =
            [](const QJsonObject& params, const QVector<QJsonValue>& inputs,
               std::function<void(bool, QJsonValue, QString)> cb) {
                const QString method = params.value("method").toString("historical_var");
                if (method != "historical_var" && method != "parametric_var") {
                    cb(false, {}, QString("Risk Analysis: method '%1' is not implemented").arg(method));
                    return;
                }
                const double confidence = params.value("confidence").toDouble(0.95);
                if (!(confidence > 0.5 && confidence < 1.0)) {
                    cb(false, {}, "Risk Analysis: confidence must be between 0.5 and 1");
                    return;
                }
                const int horizon = static_cast<int>(params.value("horizon").toDouble(1));
                if (horizon < 1) {
                    cb(false, {}, "Risk Analysis: horizon must be >= 1 day");
                    return;
                }

                // Input: rows with a `return` field, or price rows (Close/close/price),
                // or a bare numeric array. Rows missing the field are skipped —
                // never counted as a 0 return.
                QVector<double> returns;
                QVector<double> prices;
                if (!inputs.isEmpty() && inputs[0].isArray()) {
                    for (const QJsonValue& v : inputs[0].toArray()) {
                        if (v.isObject()) {
                            const QJsonObject o = v.toObject();
                            if (o.value("return").isDouble()) {
                                returns.append(o.value("return").toDouble());
                            } else {
                                const QJsonValue pv = o.contains("Close")   ? o.value("Close")
                                                      : o.contains("close") ? o.value("close")
                                                                            : o.value("price");
                                if (pv.isDouble() && pv.toDouble() > 0)
                                    prices.append(pv.toDouble());
                            }
                        } else if (v.isDouble()) {
                            returns.append(v.toDouble());
                        }
                    }
                }
                // A bare numeric array of values > 1 in magnitude is a price series.
                if (prices.isEmpty() && !returns.isEmpty() && std::abs(returns[0]) > 1.0) {
                    prices = returns;
                    returns.clear();
                }
                if (returns.isEmpty() && prices.size() > 1) {
                    for (int i = 1; i < prices.size(); ++i)
                        returns.append((prices[i] - prices[i - 1]) / prices[i - 1]);
                }

                if (returns.size() < 10) {
                    cb(false, {}, "Need at least 10 data points for risk analysis");
                    return;
                }

                double sum = 0, sum_sq = 0;
                for (double r : returns) {
                    sum += r;
                    sum_sq += r * r;
                }
                const int n = returns.size();
                const double mean = sum / n;
                const double sd = std::sqrt(std::max(0.0, sum_sq / n - mean * mean));
                const double ann_vol = sd * std::sqrt(252.0);
                const double h_scale = std::sqrt(static_cast<double>(horizon));

                double var_1d = 0, cvar_1d = 0;
                if (method == "historical_var") {
                    QVector<double> sorted = returns;
                    std::sort(sorted.begin(), sorted.end());
                    const int var_idx = std::min(n - 1, static_cast<int>((1.0 - confidence) * n));
                    var_1d = -sorted[var_idx];
                    double tail = 0;
                    for (int i = 0; i <= var_idx; ++i)
                        tail += sorted[i];
                    cvar_1d = -(tail / (var_idx + 1));
                } else {
                    // Parametric (Gaussian) VaR/CVaR from the sample mean and sd.
                    const double z = inv_norm_cdf(1.0 - confidence); // negative
                    const double pdf = std::exp(-0.5 * z * z) / std::sqrt(2.0 * M_PI);
                    var_1d = -(mean + z * sd);
                    cvar_1d = -(mean - sd * pdf / (1.0 - confidence));
                }

                QJsonObject out;
                out["method"] = method;
                out["confidence"] = confidence;
                out["horizon_days"] = horizon;
                // Multi-day figures use square-root-of-time scaling of the 1-day estimate.
                out["horizon_scaling"] = horizon > 1 ? QJsonValue("sqrt_time") : QJsonValue("none");
                out["var_1d"] = var_1d;
                out["cvar_1d"] = cvar_1d;
                out["var"] = var_1d * h_scale;
                out["cvar"] = cvar_1d * h_scale;
                out["annualized_volatility"] = ann_vol;
                out["data_points"] = n;
                cb(true, out, {});
            },
    });

    // ── Tier 1 additions ───────────────────────────────────────────

    registry.register_type({
        .type_id = "analytics.sharpe_ratio",
        .display_name = "Sharpe / Sortino",
        .category = "Analytics",
        .description = "Calculate Sharpe, Sortino, Calmar ratios",
        .icon_text = "A",
        .accent_color = "#7c3aed",
        .version = 1,
        .inputs = {{"input_0", "Returns", PortDirection::Input, ConnectionType::Main}},
        .outputs = {{"output_main", "Main", PortDirection::Output, ConnectionType::Main}},
        .parameters =
            {
                {"ratio", "Ratio", "select", "sharpe", {"sharpe", "sortino", "calmar", "information"}, ""},
                {"risk_free_rate", "Risk-Free Rate", "number", 0.05, {}, ""},
                {"benchmark", "Benchmark", "string", "SPY", {}, ""},
            },
        .execute =
            [](const QJsonObject& params, const QVector<QJsonValue>& inputs,
               std::function<void(bool, QJsonValue, QString)> cb) {
                // Extract returns from input
                QVector<double> prices;
                if (!inputs.isEmpty() && inputs[0].isArray()) {
                    for (const QJsonValue& v : inputs[0].toArray()) {
                        if (v.isObject()) {
                            double p = v.toObject().value("Close").toDouble(
                                v.toObject().value("close").toDouble(v.toObject().value("price").toDouble(0)));
                            if (p > 0)
                                prices.append(p);
                        } else if (v.isDouble()) {
                            prices.append(v.toDouble());
                        }
                    }
                }

                if (prices.size() < 2) {
                    cb(false, {}, "Need at least 2 data points for ratio calculation");
                    return;
                }

                QVector<double> returns;
                for (int i = 1; i < prices.size(); ++i)
                    returns.append((prices[i] - prices[i - 1]) / prices[i - 1]);

                double rfr = params.value("risk_free_rate").toDouble(0.05) / 252.0;
                double sum = 0, sum_sq = 0, neg_sum_sq = 0;
                double max_dd = 0, peak = prices[0];
                int neg_count = 0;

                for (double r : returns) {
                    sum += r;
                    sum_sq += r * r;
                    if (r < 0) {
                        neg_sum_sq += r * r;
                        ++neg_count;
                    }
                }
                for (double p : prices) {
                    if (p > peak)
                        peak = p;
                    double dd = (peak - p) / peak;
                    if (dd > max_dd)
                        max_dd = dd;
                }

                int n = returns.size();
                double mean = sum / n;
                double std_dev = std::sqrt(sum_sq / n - mean * mean);
                double downside_dev = neg_count > 0 ? std::sqrt(neg_sum_sq / neg_count) : 0;
                double ann_return = mean * 252.0;

                QString ratio_type = params.value("ratio").toString("sharpe");

                QJsonObject out;
                out["ratio_type"] = ratio_type;
                // Undefined ratios (zero denominator) are null, not 0.
                if (ratio_type == "sharpe") {
                    out["value"] = ratio_or_null((mean - rfr) * std::sqrt(252.0), std_dev);
                } else if (ratio_type == "sortino") {
                    out["value"] = ratio_or_null((mean - rfr) * std::sqrt(252.0), downside_dev);
                } else if (ratio_type == "calmar") {
                    out["value"] = ratio_or_null(ann_return, max_dd);
                } else {
                    // Information ratio needs benchmark returns, which this node doesn't fetch.
                    cb(false, {}, QString("Ratio '%1' is not supported yet (needs benchmark returns)").arg(ratio_type));
                    return;
                }

                out["annualized_return"] = ann_return;
                out["annualized_volatility"] = std_dev * std::sqrt(252.0);
                out["max_drawdown"] = max_dd;
                out["data_points"] = n;
                cb(true, out, {});
            },
    });

    registry.register_type({
        .type_id = "analytics.ma_crossover",
        .display_name = "MA Crossover",
        .category = "Analytics",
        .description = "Detect moving average crossover signals (golden/death cross)",
        .icon_text = "A",
        .accent_color = "#7c3aed",
        .version = 1,
        .inputs = {{"input_0", "Price Data", PortDirection::Input, ConnectionType::PriceData}},
        .outputs =
            {
                {"output_signal", "Signal", PortDirection::Output, ConnectionType::SignalData},
                {"output_data", "Data", PortDirection::Output, ConnectionType::Main},
            },
        .parameters =
            {
                {"fast_period", "Fast MA Period", "number", 50, {}, ""},
                {"slow_period", "Slow MA Period", "number", 200, {}, ""},
                {"ma_type", "MA Type", "select", "SMA", {"SMA", "EMA", "WMA"}, ""},
            },
        .execute =
            [](const QJsonObject& params, const QVector<QJsonValue>& inputs,
               std::function<void(bool, QJsonValue, QString)> cb) {
                // Extract close prices from input
                QVector<double> prices;
                if (!inputs.isEmpty() && inputs[0].isArray()) {
                    for (const QJsonValue& v : inputs[0].toArray()) {
                        if (v.isObject()) {
                            double p = v.toObject().value("Close").toDouble(v.toObject().value("close").toDouble(0));
                            if (p > 0)
                                prices.append(p);
                        } else if (v.isDouble()) {
                            prices.append(v.toDouble());
                        }
                    }
                }

                int fast = static_cast<int>(params.value("fast_period").toDouble(50));
                int slow = static_cast<int>(params.value("slow_period").toDouble(200));
                const QString ma_type = params.value("ma_type").toString("SMA");
                if (ma_type != "SMA") {
                    cb(false, {}, QString("MA Crossover: %1 is not implemented (only SMA)").arg(ma_type));
                    return;
                }
                if (fast < 1 || slow < 1) {
                    cb(false, {}, "MA Crossover: periods must be >= 1");
                    return;
                }

                if (prices.size() < slow + 1) {
                    cb(false, {},
                       QString("Need at least %1 data points for %2/%3 crossover").arg(slow + 1).arg(fast).arg(slow));
                    return;
                }

                // Compute simple moving averages at last two points
                auto sma = [&](int period, int offset) {
                    double sum = 0;
                    for (int i = offset - period + 1; i <= offset; ++i)
                        sum += prices[i];
                    return sum / period;
                };

                int last = prices.size() - 1;
                double fast_now = sma(fast, last);
                double fast_prev = sma(fast, last - 1);
                double slow_now = sma(slow, last);
                double slow_prev = sma(slow, last - 1);

                QString signal = "hold";
                if (fast_prev <= slow_prev && fast_now > slow_now)
                    signal = "golden_cross"; // bullish
                else if (fast_prev >= slow_prev && fast_now < slow_now)
                    signal = "death_cross"; // bearish
                else if (fast_now > slow_now)
                    signal = "above";
                else
                    signal = "below";

                QJsonObject out;
                out["signal"] = signal;
                out["fast_ma"] = fast_now;
                out["slow_ma"] = slow_now;
                out["fast_period"] = fast;
                out["slow_period"] = slow;
                out["ma_type"] = "SMA";
                out["current_price"] = prices.last();
                out["data_points"] = prices.size();
                cb(true, out, {});
            },
    });

    registry.register_type({
        .type_id = "analytics.drawdown",
        .display_name = "Max Drawdown",
        .category = "Analytics",
        .description = "Calculate maximum drawdown, drawdown duration, recovery time",
        .icon_text = "A",
        .accent_color = "#7c3aed",
        .version = 1,
        .inputs = {{"input_0", "Returns", PortDirection::Input, ConnectionType::Main}},
        .outputs = {{"output_main", "Main", PortDirection::Output, ConnectionType::Main}},
        .parameters =
            {
                {"window", "Window", "string", "all", {}, "'all' or number of days"},
            },
        .execute =
            [](const QJsonObject& params, const QVector<QJsonValue>& inputs,
               std::function<void(bool, QJsonValue, QString)> cb) {
                QVector<double> prices;
                if (!inputs.isEmpty() && inputs[0].isArray()) {
                    for (const QJsonValue& v : inputs[0].toArray()) {
                        if (v.isObject()) {
                            double p = v.toObject().value("Close").toDouble(
                                v.toObject().value("close").toDouble(v.toObject().value("price").toDouble(0)));
                            if (p > 0)
                                prices.append(p);
                        } else if (v.isDouble()) {
                            prices.append(v.toDouble());
                        }
                    }
                }

                // Window: 'all' or the most recent N points.
                const QString window = params.value("window").toString("all").trimmed();
                if (!window.isEmpty() && window.compare("all", Qt::CaseInsensitive) != 0) {
                    bool ok = false;
                    const int w = window.toInt(&ok);
                    if (!ok || w < 2) {
                        cb(false, {}, "Max Drawdown: window must be 'all' or a number >= 2");
                        return;
                    }
                    if (prices.size() > w)
                        prices = prices.mid(prices.size() - w);
                }

                if (prices.size() < 2) {
                    cb(false, {}, "Need at least 2 data points for drawdown analysis");
                    return;
                }

                double peak = prices[0], max_dd = 0;
                int dd_start = 0, dd_end = 0;
                int current_dd_start = 0;
                int max_dd_duration = 0, current_duration = 0;

                for (int i = 0; i < prices.size(); ++i) {
                    if (prices[i] > peak) {
                        peak = prices[i];
                        current_dd_start = i;
                        current_duration = 0;
                    }
                    double dd = (peak - prices[i]) / peak;
                    if (dd > max_dd) {
                        max_dd = dd;
                        dd_start = current_dd_start;
                        dd_end = i;
                    }
                    if (dd > 0)
                        ++current_duration;
                    else
                        current_duration = 0;
                    if (current_duration > max_dd_duration)
                        max_dd_duration = current_duration;
                }

                // Current drawdown
                double current_dd = (peak - prices.last()) / peak;

                QJsonObject out;
                out["max_drawdown"] = max_dd;
                out["max_drawdown_pct"] = max_dd * 100.0;
                out["current_drawdown"] = current_dd;
                out["current_drawdown_pct"] = current_dd * 100.0;
                out["drawdown_start_idx"] = dd_start;
                out["drawdown_end_idx"] = dd_end;
                out["drawdown_duration"] = dd_end - dd_start;
                out["max_drawdown_duration_days"] = max_dd_duration;
                out["data_points"] = prices.size();
                cb(true, out, {});
            },
    });

    registry.register_type({
        .type_id = "analytics.monte_carlo",
        .display_name = "Monte Carlo Sim",
        .category = "Analytics",
        .description = "Monte Carlo simulation for portfolio returns",
        .icon_text = "A",
        .accent_color = "#7c3aed",
        .version = 1,
        .inputs = {{"input_0", "Data In", PortDirection::Input, ConnectionType::Main}},
        .outputs = {{"output_main", "Main", PortDirection::Output, ConnectionType::Main}},
        .parameters =
            {
                {"simulations", "Simulations", "number", 10000, {}, ""},
                {"horizon_days", "Horizon (days)", "number", 252, {}, ""},
                {"confidence", "Confidence Levels", "string", "0.95,0.99", {}, "Comma-separated"},
            },
        .execute =
            [](const QJsonObject& params, const QVector<QJsonValue>& inputs,
               std::function<void(bool, QJsonValue, QString)> cb) {
                // Extract prices and compute returns
                QVector<double> prices;
                if (!inputs.isEmpty() && inputs[0].isArray()) {
                    for (const QJsonValue& v : inputs[0].toArray()) {
                        if (v.isObject()) {
                            double p = v.toObject().value("Close").toDouble(v.toObject().value("close").toDouble(0));
                            if (p > 0)
                                prices.append(p);
                        } else if (v.isDouble()) {
                            prices.append(v.toDouble());
                        }
                    }
                }

                if (prices.size() < 10) {
                    cb(false, {}, "Need at least 10 data points for Monte Carlo simulation");
                    return;
                }

                QVector<double> returns;
                for (int i = 1; i < prices.size(); ++i)
                    returns.append((prices[i] - prices[i - 1]) / prices[i - 1]);

                double mean = 0, var = 0;
                for (double r : returns)
                    mean += r;
                mean /= returns.size();
                for (double r : returns)
                    var += (r - mean) * (r - mean);
                var /= returns.size();
                double std_dev = std::sqrt(var);

                // Confidence levels → two-sided intervals of the simulated final price.
                QVector<double> levels;
                for (const QString& tok :
                     params.value("confidence").toString("0.95,0.99").split(',', Qt::SkipEmptyParts)) {
                    bool ok = false;
                    const double c = tok.trimmed().toDouble(&ok);
                    if (!ok || !(c > 0.0 && c < 1.0)) {
                        cb(false, {}, QString("Monte Carlo: invalid confidence level '%1' (use 0-1)").arg(tok.trimmed()));
                        return;
                    }
                    levels.append(c);
                }

                int n_sims = static_cast<int>(params.value("simulations").toDouble(1000));
                int horizon = static_cast<int>(params.value("horizon_days").toDouble(252));
                // Cap simulations for performance
                if (n_sims > 10000)
                    n_sims = 10000;
                if (n_sims < 1 || horizon < 1) {
                    cb(false, {}, "Monte Carlo: simulations and horizon must be >= 1");
                    return;
                }

                double start_price = prices.last();
                QVector<double> final_prices;
                final_prices.reserve(n_sims);

                auto* rng = QRandomGenerator::global();
                for (int s = 0; s < n_sims; ++s) {
                    double price = start_price;
                    for (int d = 0; d < horizon; ++d) {
                        // Box-Muller transform for normal distribution
                        double u1 = rng->generateDouble();
                        double u2 = rng->generateDouble();
                        if (u1 < 1e-10)
                            u1 = 1e-10;
                        double z = std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * M_PI * u2);
                        double ret = mean + std_dev * z;
                        price *= (1.0 + ret);
                    }
                    final_prices.append(price);
                }

                std::sort(final_prices.begin(), final_prices.end());

                QJsonObject out;
                out["simulations"] = n_sims;
                out["horizon_days"] = horizon;
                out["start_price"] = start_price;
                out["mean_final"] = [&]() {
                    double s = 0;
                    for (double p : final_prices)
                        s += p;
                    return s / n_sims;
                }();
                out["median_final"] = final_prices[n_sims / 2];
                out["p5"] = final_prices[static_cast<int>(0.05 * n_sims)];
                out["p25"] = final_prices[static_cast<int>(0.25 * n_sims)];
                out["p75"] = final_prices[static_cast<int>(0.75 * n_sims)];
                out["p95"] = final_prices[static_cast<int>(0.95 * n_sims)];
                QJsonArray intervals;
                auto quantile = [&](double q) {
                    const int idx = std::clamp(static_cast<int>(q * n_sims), 0, n_sims - 1);
                    return final_prices[idx];
                };
                for (double c : levels) {
                    intervals.append(QJsonObject{{"confidence", c},
                                                 {"lower", quantile((1.0 - c) / 2.0)},
                                                 {"upper", quantile((1.0 + c) / 2.0)}});
                }
                out["confidence_intervals"] = intervals; // two-sided, on final price
                out["min"] = final_prices.first();
                out["max"] = final_prices.last();
                out["prob_profit"] = [&]() {
                    int c = 0;
                    for (double p : final_prices)
                        if (p > start_price)
                            ++c;
                    return (double)c / n_sims;
                }();
                cb(true, out, {});
            },
    });

    // ── Tier 3: Advanced Analytics ─────────────────────────────────

    registry.register_type({
        .type_id = "analytics.factor_model",
        .display_name = "Factor Model",
        .category = "Analytics",
        .description = "Fama-French factor decomposition (3/5 factor)",
        .icon_text = "A",
        .accent_color = "#7c3aed",
        .version = 1,
        .inputs = {{"input_0", "Returns", PortDirection::Input, ConnectionType::Main}},
        .outputs = {{"output_main", "Main", PortDirection::Output, ConnectionType::Main}},
        .parameters =
            {
                {"model", "Model", "select", "ff3", {"ff3", "ff5", "capm", "carhart4"}, ""},
                {"period", "Period", "string", "3y", {}, ""},
            },
        .execute =
            [](const QJsonObject&, const QVector<QJsonValue>&, std::function<void(bool, QJsonValue, QString)> cb) {
                analysis_not_supported("Factor Model", cb);
            },
    });

    registry.register_type({
        .type_id = "analytics.pairs_trading",
        .display_name = "Pairs Trading",
        .category = "Analytics",
        .description = "Cointegration test + pairs trading signal generation",
        .icon_text = "A",
        .accent_color = "#7c3aed",
        .version = 1,
        .inputs = {{"input_0", "Data In", PortDirection::Input, ConnectionType::PriceData}},
        .outputs =
            {
                {"output_signal", "Signal", PortDirection::Output, ConnectionType::SignalData},
                {"output_data", "Spread Data", PortDirection::Output, ConnectionType::Main},
            },
        .parameters =
            {
                {"symbol_a", "Symbol A", "string", "KO", {}, "", true},
                {"symbol_b", "Symbol B", "string", "PEP", {}, "", true},
                {"lookback", "Lookback Days", "number", 60, {}, ""},
                {"z_threshold", "Z-Score Threshold", "number", 2.0, {}, ""},
            },
        .execute =
            [](const QJsonObject&, const QVector<QJsonValue>&, std::function<void(bool, QJsonValue, QString)> cb) {
                analysis_not_supported("Pairs Trading", cb);
            },
    });

    registry.register_type({
        .type_id = "analytics.regime_detection",
        .display_name = "Regime Detection",
        .category = "Analytics",
        .description = "Classify market regime (bull/bear/sideways/high-vol) with a rolling-volatility + moving-average "
                       "heuristic",
        .icon_text = "A",
        .accent_color = "#7c3aed",
        .version = 1,
        .inputs = {{"input_0", "Price Data", PortDirection::Input, ConnectionType::PriceData}},
        .outputs = {{"output_main", "Main", PortDirection::Output, ConnectionType::Main}},
        .parameters =
            {
                {"method", "Method", "select", "vol_sma_heuristic", {"vol_sma_heuristic"}, ""},
            },
        .execute =
            [](const QJsonObject&, const QVector<QJsonValue>& inputs,
               std::function<void(bool, QJsonValue, QString)> cb) {
                // Rolling-volatility + SMA heuristic (not an HMM)
                QVector<double> prices;
                if (!inputs.isEmpty() && inputs[0].isArray()) {
                    for (const QJsonValue& v : inputs[0].toArray()) {
                        if (v.isObject()) {
                            double p = v.toObject().value("Close").toDouble(v.toObject().value("close").toDouble(0));
                            if (p > 0)
                                prices.append(p);
                        } else if (v.isDouble()) {
                            prices.append(v.toDouble());
                        }
                    }
                }

                if (prices.size() < 30) {
                    cb(false, {}, "Need at least 30 data points for regime detection");
                    return;
                }

                // Compute 20-day rolling volatility and trend
                int window = 20;
                QVector<double> returns;
                for (int i = 1; i < prices.size(); ++i)
                    returns.append((prices[i] - prices[i - 1]) / prices[i - 1]);

                // Latest window stats
                int n = returns.size();
                double sum = 0, sum_sq = 0;
                for (int i = n - window; i < n; ++i) {
                    sum += returns[i];
                    sum_sq += returns[i] * returns[i];
                }
                double mean = sum / window;
                double vol = std::sqrt(sum_sq / window - mean * mean);
                double ann_vol = vol * std::sqrt(252.0);

                // Trend: SMA50 vs SMA200 (or shorter if not enough data)
                int sma_short = std::min(50, static_cast<int>(prices.size()) / 3);
                int sma_long = std::min(200, static_cast<int>(prices.size()) - 1);
                double sma_s = 0, sma_l = 0;
                for (int i = prices.size() - sma_short; i < prices.size(); ++i)
                    sma_s += prices[i];
                sma_s /= sma_short;
                for (int i = prices.size() - sma_long; i < prices.size(); ++i)
                    sma_l += prices[i];
                sma_l /= sma_long;

                QString regime;
                if (ann_vol > 0.30)
                    regime = "high_volatility";
                else if (sma_s > sma_l && mean > 0)
                    regime = "bull";
                else if (sma_s < sma_l && mean < 0)
                    regime = "bear";
                else
                    regime = "sideways";

                QJsonObject out;
                out["regime"] = regime;
                // The method actually used — not an HMM, whatever the saved param says.
                out["method"] = "vol_sma_heuristic";
                out["annualized_volatility"] = ann_vol;
                out["short_ma"] = sma_s;
                out["long_ma"] = sma_l;
                out["avg_daily_return"] = mean;
                out["current_price"] = prices.last();
                out["data_points"] = prices.size();
                cb(true, out, {});
            },
    });
}

} // namespace fincept::workflow
