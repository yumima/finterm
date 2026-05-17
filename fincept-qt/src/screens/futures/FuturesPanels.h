#pragma once
#include "screens/futures/FuturesDataService.h"

#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QWidget>

class QChartView;

namespace fincept::screens::futures {

/// Common panel chrome (title strip + status text) and theme refresh.
/// Two flavours of panel exist:
///   1. Cache-driven    — read synchronously from FuturesQuoteCache; no async.
///   2. Symbol-driven   — fire async fetches per (symbol, period). Use a
///                        generation token to drop stale callbacks.
class FuturesPanelBase : public QWidget {
    Q_OBJECT
  public:
    explicit FuturesPanelBase(const QString& title, QWidget* parent = nullptr);

  public slots:
    /// Override in panels that change behaviour per asset class.
    // User-triggered changes always reset the in-flight guard so the new
    // symbol/class gets a fresh fetch even if a previous one is running.
    virtual void set_asset_class(const QString& cls) { active_class_ = cls; fetch_in_flight_ = false; last_failed_ms_ = 0; refresh(); }
    virtual void set_symbol(const QString& sym)      { active_symbol_ = sym; fetch_in_flight_ = false; last_failed_ms_ = 0; refresh(); }
    virtual void refresh() {}

  protected:
    void apply_theme();
    void set_status(const QString& s, const QString& color = QString());

    /// Insert a widget into the panel's title bar between the stretch and the
    /// status label, so it floats on the right side of the header. Used for
    /// inline call-to-action chips like SET DATABENTO KEY that should be
    /// prominent in the title row when the panel needs user action to fetch
    /// data. Subclasses call this from their constructor.
    void add_header_action(QWidget* w);

    /// Bump on every state change that could trigger a stale async response.
    /// Symbol-driven panels capture the post-bump value in their callback and
    /// drop the result if the value has moved on.
    quint64 bump_gen() { return ++gen_; }
    bool    is_current(quint64 g) const { return g == gen_; }

    QString  active_class_;
    QString  active_symbol_;
    quint64  gen_ = 0;
    // Set true while an async fetch is in flight; cleared in the callback.
    // Prevents the 20s refresh timer from stacking a new request before the
    // previous one completes (or times out), which would always invalidate the
    // pending callback's generation and leave the panel stuck on "Loading…".
    bool     fetch_in_flight_ = false;
    // After a failed fetch, back off for kFailureBackoffMs before retrying.
    // Prevents the 20s timer from cycling "Loading → error → Loading" when
    // the data source is unavailable (e.g. CME API blocked, no Databento key).
    qint64   last_failed_ms_ = 0;
    static constexpr qint64 kFailureBackoffMs = 5 * 60 * 1000; // 5 minutes
    QLabel*  title_label_  = nullptr;
    QLabel*  status_label_ = nullptr;
    // Header HBox stored as a member so add_header_action() can insert chips
    // (SET DATABENTO KEY, etc.) into it from subclass constructors. The base
    // constructor lays out the row as [title, stretch, status]; actions land
    // at index count()-1 so they sit just left of the status label.
    QHBoxLayout* header_layout_ = nullptr;
};

// ── 1. Watchlist quote board (cache-driven) ──────────────────────────────────
class FuturesWatchlistPanel : public FuturesPanelBase {
    Q_OBJECT
  public:
    explicit FuturesWatchlistPanel(QWidget* parent = nullptr);
    void set_asset_class(const QString& cls) override;
    void refresh() override;

  signals:
    void symbol_clicked(const QString& sym);

  private:
    void render_from_cache();
    QTableWidget* table_ = nullptr;
};

// ── 2. Term structure (symbol-driven, async) ─────────────────────────────────
class FuturesTermStructurePanel : public FuturesPanelBase {
    Q_OBJECT
  public:
    explicit FuturesTermStructurePanel(QWidget* parent = nullptr);
    void set_asset_class(const QString& cls) override;
    void set_symbol(const QString& sym) override;
    void refresh() override;

  private:
    void render(const QVector<TermStructurePoint>& pts);
    void show_placeholder(const QString& message);
    QLabel*       contango_label_ = nullptr;
    QChartView*   chart_view_     = nullptr;
    QLabel*       placeholder_    = nullptr;
    QComboBox*    symbol_combo_   = nullptr;
    QPushButton*  key_btn_        = nullptr; // shown when DATABENTO_API_KEY is unset
};

// ── 3. Spread monitor (cache-driven) ─────────────────────────────────────────
class FuturesSpreadPanel : public FuturesPanelBase {
    Q_OBJECT
  public:
    explicit FuturesSpreadPanel(QWidget* parent = nullptr);
    void set_asset_class(const QString& cls) override;
    void refresh() override;

  private:
    void render_from_cache();
    QComboBox* leg1_combo_ = nullptr;
    QComboBox* leg2_combo_ = nullptr;
    QLabel*    spread_lbl_ = nullptr;
    QLabel*    pct_lbl_    = nullptr;
};

// ── 4. Settlements + OI table (symbol-driven, async) ─────────────────────────
class FuturesSettlementsPanel : public FuturesPanelBase {
    Q_OBJECT
  public:
    explicit FuturesSettlementsPanel(QWidget* parent = nullptr);
    void set_asset_class(const QString& cls) override;
    void set_symbol(const QString& sym) override;
    void refresh() override;

  private:
    void populate(const QVector<TermStructurePoint>& pts);
    void show_placeholder(const QString& message);
    QTableWidget* table_         = nullptr;
    QLabel*       placeholder_   = nullptr;
    QComboBox*    symbol_combo_  = nullptr;
    QPushButton*  key_btn_       = nullptr; // shown when DATABENTO_API_KEY is unset
};

// ── 5. Heatmap (cache-driven, all classes) ───────────────────────────────────
class FuturesHeatmapPanel : public FuturesPanelBase {
    Q_OBJECT
  public:
    explicit FuturesHeatmapPanel(QWidget* parent = nullptr);
    void refresh() override;

  private:
    void render_from_cache();
    void clear_grid();
    QGridLayout* grid_   = nullptr;
    QWidget*     canvas_ = nullptr;
};

// ── 6. Continuous chart (symbol-driven, async) ───────────────────────────────
class FuturesChartPanel : public FuturesPanelBase {
    Q_OBJECT
  public:
    explicit FuturesChartPanel(QWidget* parent = nullptr);
    void set_asset_class(const QString& cls) override;
    void set_symbol(const QString& sym) override;
    void refresh() override;

  private:
    void render(const QVector<HistoryPoint>& pts);
    QChartView* chart_view_   = nullptr;
    QComboBox*  period_combo_ = nullptr;
    QComboBox*  symbol_combo_ = nullptr;
};

// ── 7. China sub-tab (akshare main contracts) ────────────────────────────────
class FuturesChinaPanel : public FuturesPanelBase {
    Q_OBJECT
  public:
    explicit FuturesChinaPanel(QWidget* parent = nullptr);
    void refresh() override;

  private:
    void populate(const QJsonArray& rows);
    QTableWidget* table_           = nullptr;
    QComboBox*    exchange_combo_  = nullptr;
};

// ── 8. Expiry / first-notice calendar (computed from contract conventions) ───
// CME publishes contract expiry rules that are deterministic (3rd Friday of
// quarter for ES/NQ, etc.). We compute them client-side so no network is
// required, then surface next-expiry + days-to-expiry per active-class
// contract. Days-to-expiry < 30 highlights amber, < 7 highlights red so
// users notice rolling pressure before it bites.
class FuturesExpiryPanel : public FuturesPanelBase {
    Q_OBJECT
  public:
    explicit FuturesExpiryPanel(QWidget* parent = nullptr);
    void set_asset_class(const QString& cls) override;
    void refresh() override;

  private:
    void rebuild();
    QTableWidget* table_ = nullptr;
};

// ── 9. COT (CFTC Commitments of Traders) positioning ─────────────────────────
// Surfaces commercial / large-spec / small-spec longs, shorts, nets, and the
// week-over-week change for the active futures product. Powered by the
// existing scripts/cftc_data.py wrapper around publicreporting.cftc.gov.
class FuturesCotPanel : public FuturesPanelBase {
    Q_OBJECT
  public:
    explicit FuturesCotPanel(QWidget* parent = nullptr);
    void set_asset_class(const QString& cls) override;
    void set_symbol(const QString& sym) override;
    void refresh() override;

  private:
    void render(const QJsonArray& rows);
    void show_placeholder(const QString& message);

    /// Map a futures root ("ES","CL","GC", …) to a CFTC identifier the
    /// cftc_data.py wrapper accepts ("sp500","crude","gold").
    static QString cftc_identifier_for(const QString& symbol);

    QTableWidget* table_         = nullptr;
    QLabel*       date_lbl_      = nullptr;
    QLabel*       sentiment_lbl_ = nullptr;
    QLabel*       placeholder_   = nullptr;
    QComboBox*    symbol_combo_  = nullptr;
};

} // namespace fincept::screens::futures
