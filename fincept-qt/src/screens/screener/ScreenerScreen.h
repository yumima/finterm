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
class QButtonGroup;
class QUrl;
class QTimer;

namespace fincept::screens {

/// SCREENER — rule-exact stock screens from investing-competition winners
/// (Minervini, CAN SLIM, Kullamägi, Martin Luk) and the academic literature
/// (momentum, Magic Formula, Piotroski, Novy-Marx, Trending Value, low vol).
///
///   rail    the strategies, with today's counts; OVERVIEW first (names on
///           two or more screens)
///   card    the selected strategy's summary above its list, record in one line
///   write-up  the right half, by default: the strategy in full — idea, rules,
///           how its author uses it, the columns, the measured record, how it
///           was tested, further reading (Overview: how the records were made)
///   table   the names it selects today, ranked by its own rule, 6-month trend
///           lines, filter; the Leader Pullback adds a trade plan sized to the
///           reader's account
///
/// Clicking a name opens Equity Research in the right half instead — the list
/// stays in view, the details sit beside it, as in Portfolio; clicking a
/// strategy puts ER away and brings the write-up back. Arrowing
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
    int state_version() const override { return 2; } // a bump discards saved state

  signals:
    /// The reader picked `ticker`. open = a click (show Equity Research beside
    /// the screener); !open = keyboard browsing (follow only if ER is open).
    void research_requested(const QString& ticker, bool open);
    /// The reader picked a strategy: show its write-up on the right, putting
    /// away an Equity Research shown beside the screener.
    void details_requested();

  public:
    /// No room for the right-hand pane: show the write-up in place of the
    /// list, with a link back.
    void show_details_inline();

  protected:
    void showEvent(QShowEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    bool eventFilter(QObject* o, QEvent* e) override;

  private:
    void build_ui();
    void load_meta();
    void run_scan();
    void apply_result(const screener::ScanResult& r, bool provisional);
    void populate_strategies();
    void show_strategy(const QString& id);
    void fill_table();
    void fill_band();
    void render_band(bool force = false);
    void apply_filter();
    void set_status(const QString& text, bool error = false);
    void set_progress(int done, int total, const QString& text);
    void even_out_panes();
    void update_details_pane();
    void render_details(bool force = false);
    int details_columns(int width) const;  // 1-3 tiles for the layout choice and the width
    QString compose_details(int n) const;  // the write-up's head, parts and tail in n columns
    bool open_reading(const QUrl& u);       // a further-reading link: https only
    static int layout_width(const QTextBrowser* b); // its width as if no scroll bar (tiles don't flip-flop)
    QString inline_html() const;
    bool setting_up() const; // the setting-up view, on a screen that has one
    void update_view_toggle();
    void render_inline();
    void leave_inline();
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
    QWidget*      view_box_ = nullptr; // Passing | Setting up
    QPushButton*  passing_btn_ = nullptr;
    QPushButton*  setup_btn_ = nullptr;
    bool          setting_up_ = false;
    QDoubleSpinBox* account_ = nullptr;
    QComboBox*    risk_ = nullptr;
    QTableWidget* table_ = nullptr;
    // the strategy card: every word shown, nothing behind a toggle
    // The summary card: one document sized to its laid-out height (a
    // word-wrapped QLabel's height-for-width runs short on rich text).
    QTextBrowser* band_ = nullptr;
    QString       band_html_;       // the summary, from fill_band
    QString       band_shown_;      // the document on screen (summary + pointer)
    // The right half: the full write-up, while Equity Research is not beside.
    QWidget*      details_pane_ = nullptr; // the layout row + details_
    QTextBrowser* details_ = nullptr;
    QButtonGroup* layout_group_ = nullptr;
    int           details_layout_ = 0;     // 0 Auto, 1 Tiles, 2 Column
    QString       details_head_, details_tail_;
    QStringList   details_parts_;          // 1 how it works · 2 what it did · 3 today
    QString       details_html_;
    QString       details_shown_;
    QTextBrowser* inline_ = nullptr;  // centre page 2: the write-up in place of the list
    bool          inline_on_ = false;
    QString       inline_shown_, inline_id_;
    bool          details_pending_ = false; // a click + activation, one request
    bool          relayout_pending_ = false; // a write-up re-render after a resize, coalesced
    QString       details_id_;      // whose write-up is on screen (keeps the scroll on a refresh)
    int           band_width_ = -1; // the width the document was last laid out at
    int           band_cap_ = -1;   // the most height it may take (the pane's 75%)
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
