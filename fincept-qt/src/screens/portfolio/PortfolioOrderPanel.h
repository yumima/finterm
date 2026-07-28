// src/screens/portfolio/PortfolioOrderPanel.h
#pragma once
#include "screens/portfolio/PortfolioTypes.h"

#include <QLabel>
#include <QPushButton>
#include <QWidget>

namespace fincept::screens {

/// 200px order-entry sidebar for BUY/SELL, docked to the right of the heatmap.
class PortfolioOrderPanel : public QWidget {
    Q_OBJECT
  public:
    explicit PortfolioOrderPanel(QWidget* parent = nullptr);

    void set_holding(const portfolio::HoldingWithQuote* holding);
    void set_currency(const QString& currency);
    void set_side(const QString& side); // "BUY" or "SELL"

  signals:
    void buy_submitted();
    void sell_submitted();
    void close_requested();

  private:
    void build_ui();
    void update_display();
    /// Repaints the BUY|SELL segmented toggle (and the panel's left accent)
    /// from side_. Also forces the two checkable buttons back into agreement
    /// with side_, since a click toggles the button *before* the handler runs.
    void update_tabs();
    /// Repaints the submit button from side_. Split out of update_display()
    /// because that early-returns when no holding is selected — which used to
    /// leave a red "OPEN SELL ORDER" button sitting under BUY-coloured tabs.
    void update_submit();

    QPushButton* buy_tab_ = nullptr;
    QPushButton* sell_tab_ = nullptr;
    QPushButton* submit_btn_ = nullptr;
    QPushButton* close_btn_ = nullptr;

    QLabel* symbol_label_ = nullptr;
    QLabel* price_label_ = nullptr;
    QLabel* qty_label_ = nullptr;
    QLabel* mv_label_ = nullptr;

    QString side_ = "BUY";
    QString currency_ = "USD";
    const portfolio::HoldingWithQuote* holding_ = nullptr;
};

} // namespace fincept::screens
