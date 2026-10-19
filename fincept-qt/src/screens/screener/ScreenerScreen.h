#pragma once
#include <QWidget>

#include "screens/IStatefulScreen.h"
#include "screens/screener/ScreenerModel.h"

class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QTableWidget;
class QTextBrowser;

namespace fincept::screens {

/// SCREENER — rule-exact stock screens from investing-competition winners
/// (Minervini, CAN SLIM, Kullamägi) and the academic literature (momentum,
/// Magic Formula, Piotroski, Novy-Marx, Trending Value, low volatility).
///
/// Left: the strategies. Centre: the names each one selects today, ranked by
/// its own rule. Right: the strategy's rules and what our backtest measured —
/// shown beside every list because the measurement found NO screen beating
/// the equal-weighted S&P 500 with significance (2010-2026). The tab finds
/// the names a method would look at; it does not claim the method pays.
/// Double-click a row to open it in Equity Research.
///
/// Data: scripts/screener_data.py (TradingView fundamentals for the whole US
/// market in one request; Yahoo daily bars, cached per close).
class ScreenerScreen : public QWidget, public IStatefulScreen {
    Q_OBJECT
  public:
    explicit ScreenerScreen(QWidget* parent = nullptr);

    void restore_state(const QVariantMap& state) override;
    QVariantMap save_state() const override;
    QString state_key() const override { return QStringLiteral("screener"); }
    int state_version() const override { return 1; }

  signals:
    void navigate_to_screen(const QString& screen_id, const QString& ticker);

  protected:
    void showEvent(QShowEvent* e) override;

  private:
    void build_ui();
    void run_scan();
    void apply_result(const screener::ScanResult& r, bool from_cache);
    void populate_strategies();
    void show_strategy(const QString& id);
    void fill_table(const screener::Strategy& s);
    void fill_card(const screener::Strategy& s);
    void set_status(const QString& text, bool error = false);
    double min_mcap() const;
    QString cache_name() const;

    QComboBox*    universe_ = nullptr;
    QPushButton*  refresh_btn_ = nullptr;
    QLabel*       status_ = nullptr;
    QLabel*       market_ = nullptr;
    QListWidget*  list_ = nullptr;
    QLabel*       table_title_ = nullptr;
    QTableWidget* table_ = nullptr;
    QTextBrowser* card_ = nullptr;

    screener::ScanResult result_;
    QString current_id_ = QStringLiteral("trend_template");
    bool running_ = false;
    bool shown_once_ = false;
    int  scan_gen_ = 0; // a reply for a universe no longer selected is dropped
    // Newest scan generation saved per cache file: a superseded scan that
    // finishes late must not overwrite a newer result for its universe.
    QHash<QString, int> saved_gen_;
    // Generation of the result on screen (-1 = a disk cache), so a superseded
    // scan for the universe selected NOW can still replace an older view.
    int displayed_gen_ = -1;
};

} // namespace fincept::screens
