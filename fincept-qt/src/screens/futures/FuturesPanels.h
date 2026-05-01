#pragma once
#include "screens/futures/FuturesDataService.h"

#include <QComboBox>
#include <QGridLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
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
    virtual void set_asset_class(const QString& cls) { active_class_ = cls; refresh(); }
    virtual void set_symbol(const QString& sym) { active_symbol_ = sym; refresh(); }
    virtual void refresh() {}

  protected:
    void apply_theme();
    void set_status(const QString& s, const QString& color = QString());

    /// Bump on every state change that could trigger a stale async response.
    /// Symbol-driven panels capture the post-bump value in their callback and
    /// drop the result if the value has moved on.
    quint64 bump_gen() { return ++gen_; }
    bool    is_current(quint64 g) const { return g == gen_; }

    QString  active_class_;
    QString  active_symbol_;
    quint64  gen_ = 0;
    QLabel*  title_label_  = nullptr;
    QLabel*  status_label_ = nullptr;
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

} // namespace fincept::screens::futures
