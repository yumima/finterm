#pragma once
#include "screens/IStatefulScreen.h"

#include <QHideEvent>
#include <QLabel>
#include <QPointer>
#include <QShowEvent>
#include <QStackedWidget>
#include <QTimer>
#include <QVariantMap>
#include <QVector>
#include <QWidget>

class QPushButton;

namespace finterm::screens {

namespace world {
class WorldMarketsView;
}
class MarketsOverview;
class FuturesScreen;

/// MARKETS — one tab for every market, three pages (the last one used is
/// remembered):
///  - OVERVIEW: a configurable canvas (the dashboard's grid and widgets) of
///    quote lists, market-condition widgets and a chart linked to them —
///    MarketsOverview.
///  - FUTURES: one futures market at a time — board, chart, forward curve,
///    contract months, spreads, positioning, ticket (FuturesScreen, which was
///    its own tab; every route to "futures" now lands here).
///  - WORLD: the fixed one-screen overview of global markets.
class MarketsScreen : public QWidget, public IStatefulScreen {
    Q_OBJECT
  public:
    explicit MarketsScreen(QWidget* parent = nullptr);

    enum class Page { Overview, Futures, World };
    void set_page(Page p, bool persist = true);
    /// MARKETS ▸ FUTURES on `root` ("ES", "CL" …; empty = as left).
    void open_futures(const QString& root);
    /// From anywhere (a chart's CURVE ▸, the old "futures" route): bring
    /// MARKETS forward on FUTURES — the open screen, or the one about to be
    /// built for the request.
    static void request_futures(const QString& root = {});
    /// request_futures() without the navigation (the router navigates itself).
    static void prepare_futures(const QString& root = {});

    void restore_state(const QVariantMap& state) override;
    QVariantMap save_state() const override;
    QString state_key() const override { return QStringLiteral("markets"); }
    int state_version() const override { return 2; } // 2: pages + the futures page's own state

  protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

  private:
    QWidget* build_header_bar();
    void ensure_futures();
    void update_session_status();
    void update_clocks();
    void update_page_buttons();
    void refresh_theme();
    void refresh_all();

    QStackedWidget* stack_ = nullptr;
    MarketsOverview* overview_ = nullptr;
    FuturesScreen* futures_ = nullptr; ///< built on first use
    world::WorldMarketsView* world_ = nullptr;
    QWidget* futures_slot_ = nullptr;   ///< placeholder page until FUTURES is built
    Page page_ = Page::Overview;
    QVariantMap pending_futures_state_; ///< restored state for a FUTURES page not built yet
    static QPointer<MarketsScreen> live_; ///< the open MARKETS screen (one at a time)
    static bool pending_request_;
    static QString pending_root_;

    QWidget* header_bar_ = nullptr;
    QPushButton* overview_btn_ = nullptr;
    QPushButton* futures_btn_ = nullptr;
    QPushButton* world_btn_ = nullptr;
    QVector<QWidget*> overview_only_; ///< + WIDGET, RESET
    QWidget* clocks_box_ = nullptr;   ///< hidden on WORLD (it has its own sessions strip)
    QLabel* session_label_ = nullptr;
    QLabel* ny_label_ = nullptr;
    QLabel* lon_label_ = nullptr;
    QLabel* tok_label_ = nullptr;
    QTimer* session_timer_ = nullptr;
    QTimer* clock_timer_ = nullptr;
};

} // namespace finterm::screens
