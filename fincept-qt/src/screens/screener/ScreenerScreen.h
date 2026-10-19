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
class QTimer;

namespace fincept::screens {

class ScreenerChart;

/// SCREENER — rule-exact stock screens from investing-competition winners
/// (Minervini, CAN SLIM, Kullamägi, Martin Luk) and the academic literature
/// (momentum, Magic Formula, Piotroski, Novy-Marx, Trending Value, low vol).
///
///   rail    the strategies, with today's counts; OVERVIEW first (names on
///           two or more screens)
///   band    the selected strategy at a glance: what it is, its measured
///           record in one line and as a growth-of-$1 curve against the index,
///           "Rules & evidence" to expand the full card
///   table   the names it selects today, ranked by its own rule, 6-month trend
///           lines, filter; the Leader Pullback adds a trade plan sized to the
///           reader's account
///
/// Clicking a name opens Equity Research to the RIGHT of the screener — the
/// list stays in view, the details sit beside it, as in Portfolio. Arrowing
/// down the list follows in ER once it is open.
///
/// The record sits on every strategy because the measurement found NO screen
/// beating the equal-weighted S&P 500 with significance (2010-2026): a list is
/// where a method would look, not a forecast. Opening never shows an empty
/// screen: `screener_data.py meta` paints the strategies and records at once.
class ScreenerScreen : public QWidget, public IStatefulScreen {
    Q_OBJECT
  public:
    explicit ScreenerScreen(QWidget* parent = nullptr);

    void restore_state(const QVariantMap& state) override;
    QVariantMap save_state() const override;
    QString state_key() const override { return QStringLiteral("screener"); }
    int state_version() const override { return 2; } // 'details' is additive: no bump (a bump discards saved state)

  signals:
    /// The reader picked `ticker`. open = a click (show Equity Research beside
    /// the screener); !open = keyboard browsing (follow only if ER is open).
    void research_requested(const QString& ticker, bool open);

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
    void fill_band();
    void fill_equity_chart();
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
    // the strategy band
    QLabel*       band_title_ = nullptr;
    QLabel*       band_sub_ = nullptr;
    QLabel*       band_record_ = nullptr;
    QPushButton*  details_btn_ = nullptr;
    QTextBrowser* details_ = nullptr; // rules + the full record, collapsed by default
    ScreenerChart* equity_chart_ = nullptr;
    bool          details_open_ = false;
    bool          filling_ = false;     // programmatic selection: not the reader's pick
    QTimer* follow_timer_ = nullptr; // keyboard browsing -> ER, debounced
    QString       last_research_;          // last symbol sent to ER: a re-sort moving rows is not a pick

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
