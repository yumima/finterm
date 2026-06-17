// src/screens/power_trader/TradeActionCard.h
#pragma once
#include "screens/power_trader/PowerTraderTypes.h"

#include <QFrame>

class QLabel;
class QPushButton;
class QTextEdit;

namespace fincept::screens {

/// Per-trade drill-down popup for the power-trader member profile.
///
/// Answers "I want to buy the same stock this trader did, and I want to know
/// the reason." Clicking any row in a member's RECENT TRADES table pops this
/// card, which surfaces the full disclosed detail (transaction vs disclosure
/// date, lag, amount range, signal score with a plain-language reading,
/// committee overlap, asset type, and the source filing link) plus four
/// "follow this trade" actions.
///
/// Frameless Qt::Popup — dismisses on outside click or Esc. Created once per
/// MemberProfilePanel and reused via show_for(); never modal.
class TradeActionCard : public QFrame {
    Q_OBJECT
  public:
    explicit TradeActionCard(QWidget* parent = nullptr);

    /// Populate for @p trade and pop the card with its top-left near
    /// @p global_pos (clamped to the screen).
    void show_for(const power_trader::PoliticalTrade& trade, const QPoint& global_pos);

  signals:
    void open_equity_research(QString ticker);
    void add_to_watchlist(QString ticker);
    void paper_buy(QString ticker);

  protected:
    void keyPressEvent(QKeyEvent* e) override;

  private:
    void build_ui();
    void start_explain_stream();

    power_trader::PoliticalTrade trade_;

    QLabel*      title_lbl_    = nullptr;
    QLabel*      detail_lbl_   = nullptr;  // rich-text field grid, rebuilt per show
    QPushButton* source_btn_   = nullptr;
    QPushButton* er_btn_       = nullptr;
    QPushButton* watch_btn_    = nullptr;
    QPushButton* buy_btn_      = nullptr;
    QPushButton* explain_btn_  = nullptr;
    QTextEdit*   explain_view_ = nullptr;

    // Streaming lifetime guard — mirrors MemberProfilePanel::start_explain_stream.
    // Bumped on every show_for() so chunks from a previous trade's stream are
    // dropped instead of bleeding into the freshly-shown card.
    quint64 explain_epoch_     = 0;
    bool    explain_in_flight_ = false;
};

} // namespace fincept::screens
