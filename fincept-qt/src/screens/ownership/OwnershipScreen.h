#pragma once
#include <QWidget>

#include "screens/IStatefulScreen.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QTabWidget;
class QTimer;
class QListWidget;

namespace fincept::screens {

class FirmDetailPanel;
class InsiderBuysPanel;
class MoversPanel;
class LargestFundsPanel;
class ShortRankPanel;

/// OWNERSHIP — where the informed parties are acting, market-wide.
///
/// Three scans, each a list of STOCKS, each row opening Equity Research:
/// insiders buying (Form 4, daily), the short side constrained (FINRA joined
/// to 13F), and holder bases moving (13F breadth). The reader arrives without
/// a ticker and leaves with one.
///
/// The per-security register is not here — it is the Ownership tab in Equity
/// Research, where a security is already on screen. A filer's book is a
/// drill, reached from the search box at the top right or from a holder row
/// on the stock page, never a landing page: nobody arrives with "show me
/// Capital World Investors" as the question.
class OwnershipScreen : public QWidget, public IStatefulScreen {
    Q_OBJECT
  public:
    explicit OwnershipScreen(QWidget* parent = nullptr);

    // IStatefulScreen
    void restore_state(const QVariantMap& state) override;
    QVariantMap save_state() const override;
    QString state_key() const override { return QStringLiteral("ownership"); }
    int state_version() const override { return 3; }

  signals:
    /// Ask the shell to open another screen for @p ticker (Equity Research).
    void navigate_to_screen(const QString& screen_id, const QString& ticker);

  protected:
    void showEvent(QShowEvent* e) override;

  private:
    void build_ui();
    void refresh_index_ui(const QString& msg);
    void load_tab(int index);
    void show_filer(const QString& cik, const QString& name, const QString& quarter = {});

    QLabel*      index_lbl_ = nullptr;
    QPushButton* index_btn_ = nullptr;
    QLineEdit*   filer_search_ = nullptr;
    QListWidget* filer_results_ = nullptr;
    QTimer*      filer_debounce_ = nullptr;

    QStackedWidget* stack_ = nullptr;
    QTabWidget*     tabs_ = nullptr;
    InsiderBuysPanel* insider_buys_ = nullptr;
    ShortRankPanel*   short_rank_ = nullptr;
    MoversPanel*      movers_ = nullptr;
    LargestFundsPanel* largest_funds_ = nullptr;
    QWidget*          empty_page_ = nullptr;

    QWidget*         filer_page_ = nullptr;
    QPushButton*     back_btn_ = nullptr;
    QLabel*          filer_title_ = nullptr;
    FirmDetailPanel* filer_ = nullptr;

    bool loaded_[4] = {false, false, false, false};
    bool shown_once_ = false;
};

} // namespace fincept::screens
