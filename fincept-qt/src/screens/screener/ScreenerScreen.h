#pragma once
#include <QHash>
#include <QWidget>

#include "screens/IStatefulScreen.h"
#include "screens/screener/ScreenerModel.h"

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QSplitter;
class QStackedWidget;
class QTableWidget;
class QTextBrowser;

namespace fincept::screens {

class ScreenerChart;

/// SCREENER — rule-exact stock screens from investing-competition winners
/// (Minervini, CAN SLIM, Kullamägi, Martin Luk) and the academic literature
/// (momentum, Magic Formula, Piotroski, Novy-Marx, Trending Value, low vol).
///
///   left    the strategies, with today's counts; OVERVIEW first (names on
///           two or more screens)
///   centre  the names a strategy selects today, ranked by its own rule, with
///           a 6-month trend line; filterable; the Leader Pullback adds a
///           trade plan sized to the reader's account
///   right   the rules and the measured record, the record as a growth-of-$1
///           curve against the index, and the selected stock's chart
///
/// The record sits beside every list because the measurement found NO screen
/// beating the equal-weighted S&P 500 with significance (2010-2026): a list is
/// where a method would look, not a forecast. Double-click opens Equity
/// Research.
///
/// Opening it never shows an empty screen: `screener_data.py meta` (well under
/// a second, no network) paints the strategies and records at once, and the
/// table area explains the first-run download with a progress bar.
class ScreenerScreen : public QWidget, public IStatefulScreen {
    Q_OBJECT
  public:
    explicit ScreenerScreen(QWidget* parent = nullptr);

    void restore_state(const QVariantMap& state) override;
    QVariantMap save_state() const override;
    QString state_key() const override { return QStringLiteral("screener"); }
    int state_version() const override { return 2; }

  signals:
    void navigate_to_screen(const QString& screen_id, const QString& ticker);

  protected:
    void showEvent(QShowEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

  private:
    void build_ui();
    void load_meta();
    void run_scan();
    void apply_result(const screener::ScanResult& r, bool provisional);
    void populate_strategies();
    void show_strategy(const QString& id);
    void fill_table();
    void fill_card();
    void fill_equity_chart();
    void show_stock(const QString& symbol);
    void apply_filter();
    void set_status(const QString& text, bool error = false);
    void set_progress(int done, int total, const QString& text);
    void even_out_panes();
    double min_mcap() const;
    QString cache_name() const;
    // A finished scan has lists even when every one is empty ("no stock passes
    // today"); only a strategies-only (meta) result is still waiting.
    bool has_lists() const { return !result_.meta_only; }
    void update_empty_label();
    void update_plan_cells();

    QComboBox*    universe_ = nullptr;
    QPushButton*  refresh_btn_ = nullptr;
    QLabel*       status_ = nullptr;
    QLabel*       market_ = nullptr;
    QWidget*      progress_row_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QLabel*       progress_lbl_ = nullptr;

    QSplitter*    split_ = nullptr;
    QListWidget*  list_ = nullptr;
    QStackedWidget* centre_ = nullptr; // 0 = first-run explanation, 1 = the table
    QLabel*       empty_lbl_ = nullptr;
    QLabel*       table_title_ = nullptr;
    QLineEdit*    filter_ = nullptr;
    QWidget*      plan_box_ = nullptr;
    QDoubleSpinBox* account_ = nullptr;
    QComboBox*    risk_ = nullptr;
    QTableWidget* table_ = nullptr;
    QTextBrowser* card_ = nullptr;
    ScreenerChart* equity_chart_ = nullptr;
    ScreenerChart* price_chart_ = nullptr;

    screener::ScanResult result_;
    QString current_id_ = QStringLiteral("__overview__");
    bool running_ = false;
    bool shown_once_ = false;
    bool panes_set_ = false;
    int  scan_gen_ = 0; // a reply for a universe no longer selected is dropped
    // Newest scan generation saved per cache file: a superseded scan that
    // finishes late must not overwrite a newer result for its universe.
    QHash<QString, int> saved_gen_;
    // Generation of the result on screen (-1 = a disk cache), so a superseded
    // scan for the universe selected NOW can still replace an older view.
    int displayed_gen_ = -1;
    int name_col_ = 3, sector_col_ = 4; // where fill_table put them (the filter reads both)
    int shares_col_ = -1, position_col_ = -1; // the pullback plan's sized columns, or -1
    double account_value_ = 100'000.0;
    double risk_frac_ = 0.005;
};

} // namespace fincept::screens
