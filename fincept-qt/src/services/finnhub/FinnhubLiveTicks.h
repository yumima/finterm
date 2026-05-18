// src/services/finnhub/FinnhubLiveTicks.h
#pragma once
#include "network/websocket/WebSocketClient.h"

#include <QObject>
#include <QSet>
#include <QString>

namespace fincept::services::finnhub {

/// Streams live trade ticks from Finnhub's free WebSocket
/// (wss://ws.finnhub.io?token=KEY). Used by ER to push price updates in
/// under 200ms instead of the 20s REST refresh poll. Free-tier coverage is
/// US equities; non-US tickers subscribe successfully but never emit ticks
/// — callers should keep the REST refresh as the fallback path.
///
/// Connection model: single shared QWebSocket, one symbol subscription at
/// a time. ER calls `subscribe(symbol)` whenever its primary symbol
/// changes; the prior symbol is unsubscribed transparently. When no
/// FINNHUB_API_KEY is configured, all calls are no-ops.
class FinnhubLiveTicks : public QObject {
    Q_OBJECT
  public:
    static FinnhubLiveTicks& instance();

    /// Switch the active symbol. Connects + sends the subscribe frame when
    /// needed; unsubscribes the previous symbol. Call with empty string
    /// to stop (will keep the socket warm for the next subscribe).
    void subscribe(const QString& symbol);

  signals:
    /// Fires for every trade tick on the currently subscribed symbol.
    /// `price` is the last-trade price; `size` is the share volume of
    /// that single print; `ts_ms` is the exchange-side trade timestamp
    /// in milliseconds. Multiple ticks per second on liquid names —
    /// consumers should throttle UI updates if needed.
    void tick(QString symbol, double price, qint64 ts_ms, qint64 size);

  private:
    explicit FinnhubLiveTicks(QObject* parent = nullptr);
    Q_DISABLE_COPY(FinnhubLiveTicks)

    /// Ensure the websocket is open. No-op when already connected or when
    /// no API key is configured.
    void ensure_connected();
    void on_connected();
    void on_message(const QString& msg);
    void on_disconnected();

    /// Send a typed JSON frame to the server. {"type": op, "symbol": sym}.
    /// op is "subscribe" or "unsubscribe". Drops on the floor when the
    /// socket isn't yet open — the on_connected handler will (re)send the
    /// current_symbol_ subscription on connect, so an unsent unsubscribe
    /// for a previous symbol is harmless.
    void send_op(const QString& op, const QString& symbol);

    WebSocketClient socket_;
    QString  current_symbol_;
    QString  prev_symbol_;
    bool     want_open_ = false;
};

} // namespace fincept::services::finnhub
