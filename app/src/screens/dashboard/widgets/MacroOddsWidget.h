#pragma once
// MacroOddsWidget — prediction-market odds on macro events: Kalshi's open
// events for a list of series (by default the next Fed decisions, a US
// recession, the next CPI print), soonest first.
//
// Data: ConditionService's cond:kalshi:<series> (Kalshi's public API through
// the app's Kalshi client — no account needed). Each outcome's probability is
// the book mid, else the last trade (kalshi_implied_yes_probability); an
// outcome with neither shows "—". A ladder of "Above X" strikes is shown as the
// two strikes either side of 50% — the market's median — not the whole ladder.
//
// Edited in place: "+ ADD" adds a series ticker, the "−" on a series' header
// row removes it. Config {"rows": [{"symbol": series, "label": ...}]}.
// Click an outcome: the event on kalshi.com.

#include "screens/dashboard/widgets/BaseWidget.h"
#include "screens/dashboard/widgets/ConditionTableWidget.h"
#include "ui/RenderCoalescer.h"

#include <QHash>
#include <QJsonObject>
#include <QSet>

namespace finterm::ui {
class DataTable;
}

namespace finterm::screens::widgets {

class MacroOddsWidget : public BaseWidget {
    Q_OBJECT
  public:
    explicit MacroOddsWidget(const QJsonObject& cfg = {}, QWidget* parent = nullptr);
    QJsonObject config() const override;
    void apply_config(const QJsonObject& cfg) override;

  protected:
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;
    void on_theme_changed() override;

  private:
    void subscribe_all();
    void unsubscribe_all();
    void render();
    void add_row();

    struct Line {
        enum Kind { Header, Outcome, Note, Add } kind = Note;
        int series = -1;
        QString url;
    };
    QVector<ConditionRowDef> rows_;
    QHash<QString, QJsonObject> data_;
    QSet<QString> fresh_;
    QHash<QString, QString> error_;
    QVector<Line> lines_;
    bool subscribed_ = false;
    ui::DataTable* table_ = nullptr;
    ui::RenderCoalescer render_{this, [this]() { render(); }};
};

} // namespace finterm::screens::widgets
