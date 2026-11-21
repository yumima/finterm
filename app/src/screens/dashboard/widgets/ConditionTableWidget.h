#pragma once
// ConditionTableWidget — a user-editable list of instruments with their
// market-condition readings. The dashboard's condition widgets and the
// Markets screen's CONDITIONS page are instances of this one class, so a
// symbol reads the same wherever it is listed.
//
//   MARKET mode   NAME · LAST · CHG% · vs200D · 1Y PCTL · TREND · −
//                 LAST / CHG% are the app's live quote (market:quote:<sym>);
//                 the rest are ConditionService readings of the symbol's daily
//                 bars (cond:stats:<sym>). A FRED row ("FRED:…") shows the
//                 latest observation, its change in bp and its 1-year rank.
//   MACRO mode    NAME · LATEST · CHG · AS OF · 10Y PCTL · −
//                 FRED rows only (cond:fred:<key>).
//
// Editing, the way the Markets panels and quote strips are edited but in
// place: the last row "+ ADD" opens ConditionAddDialog; the "−" at the end of
// a row removes it; right-click moves a row up / down or removes it. Every
// edit emits config_changed (the dashboard persists it; the CONDITIONS page
// persists its own). Config: {"rows": [{"symbol": "...", "label": "..."}]}.
//
// Cache first: on show every row is painted from the last answer known
// (ConditionService::last_known, the last quote) and dimmed until this
// session's answer replaces it. Click a row: its chart (MarketChartDialog).

#include "screens/dashboard/widgets/BaseWidget.h"
#include "services/markets/MarketDataService.h"
#include "ui/RenderCoalescer.h"

#include <QHash>
#include <QJsonObject>
#include <QSet>
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

    /// A line under the table computed from live quotes (e.g. the VIX term
    /// structure). `extra` are quote symbols it needs beyond the rows.
    using FooterFn = std::function<QString(const QHash<QString, services::QuoteData>&)>;
    void set_footer(const QStringList& extra, FooterFn fn);
    /// A fixed note under the table (sources, what is not shown and why).
    void set_note(const QString& note);

  protected:
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;
    void on_theme_changed() override;

  private:
    void subscribe_all();
    void unsubscribe_all();
    void render();
    void on_cell_clicked(int row, int col);
    void on_context_menu(const QPoint& pos);
    void add_row();
    void remove_row(int i);
    void move_row(int i, int delta);
    void edited();
    QString topic_for(const QString& sym) const;
    QString row_label(const ConditionRowDef& r) const;
    void restyle_labels();

    Mode mode_;
    QVector<ConditionRowDef> defaults_;
    QVector<ConditionRowDef> rows_;
    ui::DataTable* table_ = nullptr;
    QLabel* footer_ = nullptr;
    QLabel* note_ = nullptr;
    QStringList footer_syms_;
    FooterFn footer_fn_;

    QHash<QString, services::QuoteData> quotes_;   ///< by symbol
    QHash<QString, QJsonObject> cond_;             ///< condition payload by topic
    QSet<QString> cond_fresh_;                     ///< topics answered this session
    QHash<QString, QString> cond_error_;           ///< last refresh error by topic
    bool subscribed_ = false;
    QTimer* stale_timer_ = nullptr;
    ui::RenderCoalescer render_{this, [this]() { render(); }};
};

} // namespace finterm::screens::widgets
