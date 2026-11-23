#pragma once
// WorldMarketsView — the Markets screen's WORLD mode: a one-screen overview of
// global markets (sessions strip, regional equity indices, US futures, rates,
// FX, commodities, crypto, volatility) with a mechanical "overnight" line.
//
// Data: market:quote:* and market:sparkline:* topics on the DataHub (the
// yfinance daemon polls subscribed topics off the GUI thread; the push stream
// refines quotes live), index FUT% through services::fetch_index_futures, and
// the Treasury par curve through EconomicsService. Everything is
// subscribed only while the view is on screen.

#include "screens/markets/world/TreasuryCurve.h"
#include "screens/markets/world/WorldMarketsModel.h"
#include "services/markets/IndexFutures.h"
#include "services/markets/MarketDataService.h"
#include "ui/RenderCoalescer.h"

#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QVector>
#include <QWidget>

class QLabel;
class QTimer;

namespace finterm::screens::world {

class WorldMarketsView;

/// Painted strip of exchange clocks: state + time to the next open / close.
class SessionsStrip : public QWidget {
    Q_OBJECT
  public:
    explicit SessionsStrip(QWidget* parent = nullptr);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

  protected:
    void paintEvent(QPaintEvent* e) override;
    bool event(QEvent* e) override;

  private:
    QRect cell_rect(int i) const;
    QStringList ids_;
};

/// One titled block of rows (AMERICAS, FX, …), painted in place.
class WorldSectionWidget : public QWidget {
    Q_OBJECT
  public:
    WorldSectionWidget(const Section& section, const WorldMarketsView* view, QWidget* parent = nullptr);
    const Section& section() const { return section_; }
    bool contains_symbol(const QString& sym) const;
    void set_show_title(bool on) {
        show_title_ = on;
        updateGeometry();
        update();
    }
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

  signals:
    void activated(const QString& symbol, const QString& label);

  protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    bool event(QEvent* e) override;

  private:
    struct Cols {
        QRect name, last, chg, pct, spark, sess, fut;
    };
    Cols columns(int y) const;
    int row_at(int y) const;
    QString row_tooltip(int row, const QPoint& pos) const;
    bool has_term_row() const;
    int title_h() const;
    bool show_title_ = true;

    Section section_;
    const WorldMarketsView* view_;
};

class WorldMarketsView : public QWidget {
    Q_OBJECT
  public:
    explicit WorldMarketsView(QWidget* parent = nullptr);
    /// Only some of the board — for the MARKETS WORLD screen's tiles:
    /// kSessionsPart (the overnight line and the exchange clocks) and/or
    /// section titles ("AMERICAS", "FX" …). Empty = the whole board. A part
    /// subscribes to, and fetches, only what it shows.
    WorldMarketsView(const QStringList& parts, QWidget* parent);
    static inline const QString kSessionsPart = QStringLiteral("SESSIONS");

    /// User refresh (F5): re-ask every quote, futures and the curve.
    void refresh(bool force);

    // ── read access for the painters ─────────────────────────────────────
    const services::QuoteData* quote(const QString& sym) const;
    const QVector<double>* sparkline(const QString& sym) const;
    const services::IndexFutureQuote* future_for(const QString& index) const;
    const TreasuryCurve& treasury() const { return ust_; }
    RowStatus status_of(const QString& sym) const;
    /// The last refresh of `sym` failed and has not succeeded since.
    bool has_error(const QString& sym) const;
    QString error_of(const QString& sym) const { return has_error(sym) ? errors_.value(sym) : QString(); }
    QDateTime now_utc() const { return now_utc_; }

  signals:
    void symbol_activated(const QString& symbol, const QString& label);

  protected:
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;

  private:
    bool shows_header() const { return parts_.isEmpty() || parts_.contains(kSessionsPart); }
    bool shows_section(const QString& title) const { return parts_.isEmpty() || parts_.contains(title); }
    /// The symbols this view needs (the overnight line reads every region's).
    QStringList my_symbols() const;
    bool wants_futures() const;
    bool wants_treasury() const;
    void init();
    void build_ui();
    void refresh_theme();
    void subscribe_all();
    void unsubscribe_all();
    void fetch_futures();
    void sync_fut_stream();
    void fetch_treasury();
    void on_clock();
    void render();
    void mark_dirty(const QString& sym);
    void update_overnight();

    QStringList parts_;
    QLabel* overnight_label_ = nullptr;
    QLabel* asof_label_ = nullptr;
    QWidget* top_bar_ = nullptr;
    SessionsStrip* strip_ = nullptr;
    QVector<WorldSectionWidget*> sections_;

    QHash<QString, services::QuoteData> quotes_;
    QHash<QString, QVector<double>> sparks_;
    QHash<QString, QString> errors_;
    QHash<QString, services::IndexFutureQuote> fut_;
    TreasuryCurve ust_;
    QSet<QString> dirty_;
    bool all_dirty_ = false;

    QDateTime now_utc_;
    QDateTime last_quote_at_;
    int last_minute_ = -1;
    bool subscribed_ = false;
    bool shown_ = false; ///< between showEvent and hideEvent (isVisible() survives a minimise)
    bool fut_in_flight_ = false;
    bool ust_in_flight_ = false;
    QDateTime ust_fetched_at_;

    QTimer* clock_timer_ = nullptr;
    QTimer* fut_timer_ = nullptr;
    QTimer* ust_timer_ = nullptr;

    ui::RenderCoalescer render_{this, [this]() { render(); }};
};

} // namespace finterm::screens::world
