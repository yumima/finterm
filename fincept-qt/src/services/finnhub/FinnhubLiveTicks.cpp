// src/services/finnhub/FinnhubLiveTicks.cpp
#include "services/finnhub/FinnhubLiveTicks.h"

#include "core/logging/Logger.h"
#include "storage/secure/SecureStorage.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace fincept::services::finnhub {

FinnhubLiveTicks& FinnhubLiveTicks::instance() {
    static FinnhubLiveTicks inst;
    return inst;
}

FinnhubLiveTicks::FinnhubLiveTicks(QObject* parent) : QObject(parent) {
    connect(&socket_, &WebSocketClient::connected,
            this, &FinnhubLiveTicks::on_connected);
    connect(&socket_, &WebSocketClient::message_received,
            this, &FinnhubLiveTicks::on_message);
    connect(&socket_, &WebSocketClient::disconnected,
            this, &FinnhubLiveTicks::on_disconnected);
    connect(&socket_, &WebSocketClient::error_occurred,
            this, [](const QString& err) {
                LOG_WARN("Finnhub", "WebSocket error: " + err);
            });
}

void FinnhubLiveTicks::subscribe(const QString& symbol) {
    const QString sym = symbol.trimmed().toUpper();
    if (sym == current_symbol_) return;

    prev_symbol_   = current_symbol_;
    current_symbol_ = sym;

    // Unsubscribe previous symbol if the socket is open (server-side
    // bookkeeping; harmless if it isn't and on_connected will only re-send
    // the current_symbol_ subscribe).
    if (!prev_symbol_.isEmpty() && socket_.is_connected())
        send_op("unsubscribe", prev_symbol_);

    if (sym.isEmpty()) {
        // Nothing left to listen to — keep the socket warm for the next
        // subscribe rather than closing/reopening with each idle period.
        return;
    }

    ensure_connected();
    if (socket_.is_connected())
        send_op("subscribe", sym);
}

void FinnhubLiveTicks::ensure_connected() {
    if (want_open_ && socket_.is_connected()) return;

    auto r = fincept::SecureStorage::instance().retrieve(
        QStringLiteral("FINNHUB_API_KEY"));
    if (!r.is_ok() || r.value().isEmpty()) {
        // No key configured — silent no-op. ER's REST refresh remains the
        // primary path; this service exists purely to push faster updates
        // on top of it when a key is set.
        return;
    }
    want_open_ = true;
    const QString url = QStringLiteral("wss://ws.finnhub.io?token=") + r.value();
    socket_.connect_to(url);
}

void FinnhubLiveTicks::on_connected() {
    LOG_INFO("Finnhub", "Live-ticks WebSocket connected");
    // (Re)subscribe to whatever symbol is currently active. Reconnect-loop
    // also lands here, so this is the canonical place to (re)bind the
    // server-side subscription state.
    if (!current_symbol_.isEmpty())
        send_op("subscribe", current_symbol_);
}

void FinnhubLiveTicks::on_disconnected() {
    LOG_INFO("Finnhub", "Live-ticks WebSocket disconnected");
    // WebSocketClient already implements bounded auto-reconnect.
}

void FinnhubLiveTicks::on_message(const QString& msg) {
    // Source-side throttle: drop messages that arrive within 200ms of the
    // last accepted one BEFORE parsing JSON. Finnhub's free WS for liquid
    // US tickers (AAPL during market hours) easily streams 100+ trade
    // frames per second. Parsing every one on the GUI thread + emitting
    // a Qt signal for each was the dominant new CPU cost — most of those
    // ticks are discarded by the consumer-side 300ms throttle anyway, so
    // pay the JSON cost only for the ones that survive.
    //
    // Cheap byte-substring pre-check on the raw QString avoids the JSON
    // parse entirely for non-trade frames (ping, error). The full type
    // check still happens after parsing for safety.
    static qint64 s_last_accept_ms = 0;
    const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();
    if (now_ms - s_last_accept_ms < 200) return;
    // Cheap raw-string sniff before paying the JSON parse — ping frames
    // and connection-state messages aren't worth the cost.
    if (!msg.contains(QLatin1String("\"trade\""))) return;
    s_last_accept_ms = now_ms;

    // Finnhub free-tier trade message shape:
    //   {"type":"trade", "data":[{"p":<price>, "s":"<sym>", "t":<ms>, "v":<size>}, ...]}
    const auto doc = QJsonDocument::fromJson(msg.toUtf8());
    if (!doc.isObject()) return;
    const auto root = doc.object();
    if (root.value("type").toString() != "trade") return;

    const auto arr = root.value("data").toArray();
    // Single tick per batch — emit the last (most-recent) trade only. The
    // consumer is a single price display; intermediate prints within the
    // same batch are obsolete by the time the GUI repaints. A factor of
    // 10-50x reduction in signal-emission cost during heavy market.
    if (arr.isEmpty()) return;
    const auto o = arr.last().toObject();
    const QString sym = o.value("s").toString();
    if (sym.isEmpty()) return;
    emit tick(sym,
              o.value("p").toDouble(),
              static_cast<qint64>(o.value("t").toDouble()),
              static_cast<qint64>(o.value("v").toDouble()));
}

void FinnhubLiveTicks::send_op(const QString& op, const QString& symbol) {
    if (!socket_.is_connected()) return;
    QJsonObject frame;
    frame.insert("type",   op);
    frame.insert("symbol", symbol);
    socket_.send(QString::fromUtf8(QJsonDocument(frame).toJson(QJsonDocument::Compact)));
}

} // namespace fincept::services::finnhub
