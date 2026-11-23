#pragma once
// ConditionTableWidget — a user-edited list of instruments: the Quote List of
// Markets ▸ OVERVIEW (what the old PANELS tiles were), the market-condition
// lists, and the Macro list. One class, so a symbol reads the same wherever it
// is listed and every list is edited the same way.
//
//   MARKET mode   columns chosen from a catalogue (right-click ▸ Columns):
//                 NAME LAST CHG CHG% HIGH LOW OPEN PREV VOL — the app's live
//                 quote (market:quote:<sym>) — and vs200D 1Y-PCTL 5Y-PCTL RSI
//                 TREND — ConditionService readings (cond:stats:<sym>; fetched
//                 only while such a column is shown). A FRED row ("FRED:…")
//                 shows its latest value, its move in bp and its 1-year rank.
//   MACRO mode    NAME · LATEST · CHG · AS OF · 10Y PCTL — FRED rows only.
//
// Editing in place: the last row "+ ADD" (ConditionAddDialog), the "−" ending
// each row, right-click ▸ move / remove / columns / rename / reset. Every edit
// emits config_changed (the canvas persists it).
// Config: {"title"?, "rows": [{"symbol", "label"}], "columns"?: [key, …]}.
//
// Cache first: rows paint from the last known quote and reading at once,
// readings dimmed until this session's answer lands. Click a row: its chart
// (the canvas's linked chart when there is one — MarketChartDialog::show_for).

#include "screens/dashboard/widgets/BaseWidget.h"
#include "services/markets/MarketDataService.h"
#include "ui/RenderCoalescer.h"

#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QStringList>
#include <QVector>

#include <functional>

class QLabel;
class QTimer;

namespace finterm::ui {
class DataTable;
}

namespace finterm::screens::widgets {

struct ConditionRowDef {
    QString symbol;
    QString label;
};

class ConditionTableWidget : public BaseWidget {
    Q_OBJECT
  public:
    enum class Mode { Market, Macro };

    ConditionTableWidget(const QString& title, Mode mode, QVector<ConditionRowDef> defaults, const QJsonObject& cfg,
                         const QString& accent = {}, QWidget* parent = nullptr);

    QJsonObject config() const override;
    void apply_config(const QJsonObject& cfg) override;

    /// The columns a fresh tile shows (MARKET mode; keys of column_catalogue()).
    void set_default_columns(const QStringList& keys);
    /// Every MARKET-mode column: key → header.
    static const QVector<QPair<QString, QString>>& column_catalogue();
    static const QStringList& condition_columns();

    /// A line under the table computed from live quotes (e.g. the VIX term
    /// structure). `extra` are quote symbols it needs beyond the rows.
    using FooterFn = std::function<QString(const QHash<QString, services::QuoteData>&)>;
    void set_footer(const QStringList& extra, FooterFn fn);
    /// A fixed note under the table (sources, what is not shown and why).
    void set_note(const QString& note);
    /// What "+ ADD" asks for in MACRO mode (default: a FRED series).
    void set_add_sovereign(bool on) { add_sovereign_ = on; }

  protected:
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;
    void on_theme_changed() override;

  private:
    void rebuild_headers();
    void subscribe_all();
    void unsubscribe_all();
    void resubscribe();
    void render();
    void on_cell_clicked(int row, int col);
    void on_context_menu(const QPoint& pos);
    void add_row();
    void remove_row(int i);
    void move_row(int i, int delta);
    void rename();
    void edited();
    bool wants_conditions() const;
    QString topic_for(const QString& sym) const;
    QString row_label(const ConditionRowDef& r) const;
    void restyle_labels();
    QStringList columns() const; ///< the shown columns, "−" excluded

    Mode mode_;
    bool add_sovereign_ = false;
    QString default_title_;
    QString title_; ///< user's title; empty = the default
    QVector<ConditionRowDef> defaults_;
    QVector<ConditionRowDef> rows_;
    QStringList default_cols_;
    QStringList cols_; ///< MARKET mode: user's columns; empty = the defaults
    QStringList shown_headers_;
    ui::DataTable* table_ = nullptr;
    QLabel* footer_ = nullptr;
    QLabel* note_ = nullptr;
    QStringList footer_syms_;
    FooterFn footer_fn_;

    QHash<QString, services::QuoteData> quotes_;   ///< by symbol
    QHash<QString, QJsonObject> cond_;             ///< condition payload by topic
    QSet<QString> cond_fresh_;                     ///< topics answered this session
    QHash<QString, QString> cond_error_;           ///< last refresh error by topic
    QSet<QString> topics_;                         ///< condition topics subscribed now
    bool subscribed_ = false;
    QTimer* stale_timer_ = nullptr;
    ui::RenderCoalescer render_{this, [this]() { render(); }};
};

} // namespace finterm::screens::widgets
