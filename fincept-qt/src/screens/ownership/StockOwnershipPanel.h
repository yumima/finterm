#pragma once
#include "screens/ownership/OwnershipTypes.h"

#include <QHideEvent>
#include <QShowEvent>
#include <QWidget>

class QLabel;
class QComboBox;
class QStackedWidget;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QVBoxLayout;

namespace fincept::screens {
class SmartMoneyPanel;
class RankedBarChart;
class EventTimeline;
}

namespace fincept::screens {

/// OWNERSHIP — who owns a security, who is trading it, and what that implies.
///
/// The Bloomberg analogues are HDS (holders), OWN (ownership breakdown) and SI
/// (short interest), with the addition Bloomberg splits across other functions:
/// Form 4 insider transactions, which is the only part of this data with a
/// durable evidence base behind it.
///
/// Layout puts the interpretation FIRST. A holder table answers "who owns
/// this"; the question a trader actually has is "so what does that mean", and
/// a screen that only ever renders the table leaves that work undone. The
/// READ-THROUGH panel at the top states what the register implies for the
/// stock and for how it trades, each line carrying the number that produced it
/// and the rule that was applied. The tables below are the evidence for those
/// lines, in the order a reader would check them.
class StockOwnershipPanel : public QWidget {
    Q_OBJECT
  public:
    explicit StockOwnershipPanel(QWidget* parent = nullptr);

    /// Point the panel at a security. Equity Research already has a symbol box
    /// at the top of the screen, so when embedded there the panel's own symbol
    /// controls are hidden and this is how the ticker arrives.
    void set_symbol(const QString& symbol);

    /// Hide the panel's own symbol box and portfolio picker. Two symbol boxes
    /// on one screen is a question about which one is in charge.
    void set_chrome_visible(bool on);


  signals:
    /// Ask the shell to open another screen for @p ticker (Equity Research).
    void navigate_to_screen(const QString& screen_id, const QString& ticker);

  protected:
    void showEvent(QShowEvent* e) override;

  private:
    void build_ui();
    void apply_theme();
    void load(const QString& symbol);
    void render();
    void render_reads(const ownership::OwnershipSnapshot& s);
    void render_insiders(const ownership::OwnershipSnapshot& s);
    void render_stakes(const ownership::OwnershipSnapshot& s);
    void render_holders(const ownership::OwnershipSnapshot& s);
    void render_short(const ownership::OwnershipSnapshot& s);
    void reload_portfolio();
    void refresh_index_ui(const QString& msg);

    QString symbol_;

    QLineEdit*   search_       = nullptr;
    QComboBox*   portfolio_    = nullptr;
    QPushButton* index_btn_    = nullptr;
    QLabel*      index_lbl_    = nullptr;
    QStackedWidget* body_ = nullptr;
    QWidget*     empty_page_ = nullptr;
    QPushButton* refresh_btn_  = nullptr;
    QLabel*      title_        = nullptr;
    QPushButton* er_btn_       = nullptr;
    /// False when embedded (Equity Research, or the ownership detail pane),
    /// where the host already names the symbol and owns the index controls.
    bool         chrome_       = true;
    QLabel*      status_       = nullptr;
    QLabel*      coverage_     = nullptr;

    class ReadThroughStrip* reads_strip_ = nullptr;

    QTableWidget* insiders_tbl_ = nullptr;
    QTableWidget* stakes_tbl_   = nullptr;
    QLabel*       short_lbl_    = nullptr;
    SmartMoneyPanel* smart_money_ = nullptr;
    RankedBarChart*  ownership_mix_ = nullptr;
    EventTimeline*   insider_timeline_ = nullptr;
};

} // namespace fincept::screens
