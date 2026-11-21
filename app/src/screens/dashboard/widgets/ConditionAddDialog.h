#pragma once
// ConditionAddDialog — the "+" of a market-condition list: pick what to add.
//
//   Symbol  Yahoo symbol search (EquityResearchService, the search every
//           symbol box in the app uses); Enter adds what was typed, so an
//           index / future the search does not list (^MOVE, QO=F) can still
//           be added.
//   FRED    a FRED series id (fred.stlouisfed.org), as published or as a
//           year-on-year percent change.
//   Kalshi  a Kalshi series ticker (KXFEDDECISION, KXCPIYOY …).
//
// The result is a row {symbol, label}; the list validates nothing here — a
// symbol with no data shows a dashed row saying why, like every quote list.

#include <QDialog>
#include <QString>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QTimer;

namespace finterm::screens::widgets {

class ConditionAddDialog : public QDialog {
    Q_OBJECT
  public:
    enum class Kind { Symbol, Fred, Kalshi };
    ConditionAddDialog(Kind kind, QWidget* parent = nullptr);

    QString symbol() const { return symbol_; }
    QString label() const { return label_; }

  private:
    void fire_search();
    void accept_symbol(const QString& sym, const QString& name);

    Kind kind_;
    QString symbol_;
    QString label_;
    QLineEdit* input_ = nullptr;
    QLineEdit* label_edit_ = nullptr;
    QCheckBox* yoy_ = nullptr;
    QListWidget* results_ = nullptr;
    QLabel* hint_ = nullptr;
    QTimer* debounce_ = nullptr;
    QString pending_;
};

} // namespace finterm::screens::widgets
