#pragma once
#include "screens/IStatefulScreen.h"
#include "screens/ownership/OwnershipTypes.h"

#include <QHideEvent>
#include <QShowEvent>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QVBoxLayout;

namespace fincept::screens { class SmartMoneyPanel; }

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
class OwnershipScreen : public QWidget, public IStatefulScreen {
    Q_OBJECT
  public:
    explicit OwnershipScreen(QWidget* parent = nullptr);

    // IStatefulScreen
    void restore_state(const QVariantMap& state) override;
    QVariantMap save_state() const override;
    QString state_key() const override { return QStringLiteral("ownership"); }
    int state_version() const override { return 1; }

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

    QString symbol_;

    QLineEdit*   search_       = nullptr;
    QPushButton* refresh_btn_  = nullptr;
    QLabel*      title_        = nullptr;
    QLabel*      status_       = nullptr;
    QLabel*      coverage_     = nullptr;

    QWidget*     reads_host_   = nullptr;
    QVBoxLayout* reads_layout_ = nullptr;

    QTableWidget* insiders_tbl_ = nullptr;
    QTableWidget* stakes_tbl_   = nullptr;
    QTableWidget* holders_tbl_  = nullptr;
    QLabel*       short_lbl_    = nullptr;
    SmartMoneyPanel* smart_money_ = nullptr;
};

} // namespace fincept::screens
