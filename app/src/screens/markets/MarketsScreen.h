#pragma once
#include "screens/IStatefulScreen.h"
#include "screens/dashboard/canvas/GridLayout.h"

#include <QHideEvent>
#include <QLabel>
#include <QPointer>
#include <QShowEvent>
#include <QStackedWidget>
#include <QTimer>
#include <QVariantMap>
#include <QVector>
#include <QWidget>

class QHBoxLayout;
class QPushButton;
class QToolButton;

namespace finterm::screens {

class MarketScreenPage;

/// MARKETS — every market in one tab, every page in ONE header row:
///
///   FINTERM MARKETS  OVERVIEW: MARKETS | EQUITY | RATES & MACRO | +
///                    FUTURES: MAJORS | INDEX | RATES | ENERGY | … | CRYPTO
///                    WORLD
///
/// Every page is the same thing — a canvas of widgets the user arranges
/// (MarketScreenPage), fitted to the view, with its chart (and on FUTURES
/// pages the forward curve, months and detail) embedded and following the
/// clicks made on that page only. One click reaches any page.
///   OVERVIEW  the user's screens: "+" adds (blank / from a template / reset
///             all), right-click renames, duplicates, moves, resets, deletes
///   FUTURES   a page per group (the old FUTURES screen, as widgets)
///   WORLD     the global price board as tiles
/// "+ ADD" adds a widget to the page shown, a tile's ✕ removes it, RESET puts
/// the page back — as on the dashboard. Every route to the old FUTURES tab
/// opens the futures page of the market asked for.
class MarketsScreen : public QWidget, public IStatefulScreen {
    Q_OBJECT
  public:
    explicit MarketsScreen(QWidget* parent = nullptr);

    /// The FUTURES page of `root` ("ES", "CL" …), with it selected (the page's
    /// chart, curve, months and detail follow). Empty: MAJORS as left.
    void open_futures(const QString& root);
    /// From anywhere (a chart's CURVE ▸, the old "futures" route): bring
    /// MARKETS forward on that market — the open screen, or the one about to
    /// be built for the request.
    static void request_futures(const QString& root = {});
    /// request_futures() without the navigation (the router navigates itself).
    static void prepare_futures(const QString& root = {});

    void restore_state(const QVariantMap& state) override;
    QVariantMap save_state() const override;
    QString state_key() const override { return QStringLiteral("markets"); }
    int state_version() const override { return 4; } // 4: one-row pages

  protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

  private:
    struct PageDef {
        QString id;    ///< page id (also its layout's key)
        QString title; ///< button text
        QString tmpl;  ///< template
        enum Kind { Overview, Futures, World } kind = Overview;
    };
    QWidget* build_header_bar();
    void load_pages();
    void save_overview();
    MarketScreenPage* ensure_page(int index);
    QPushButton* make_page_button(int index);
    void rebuild_overview_buttons();
    void show_page(int index, bool persist = true);
    int index_of(const QString& id) const;
    void show_overview_menu(int index, const QPoint& global);
    void show_add_menu();
    void add_overview(const QString& title, const QString& tmpl, const GridLayout* layout = nullptr);
    void rename_screen(int index);
    void duplicate_screen(int index);
    void move_screen(int index, int delta);
    void delete_screen(int index);
    void reset_all();
    void update_session_status();
    void refresh_theme();
    void refresh_all();

    QVector<PageDef> pages_;              ///< overview screens first, then FUTURES groups, then WORLD
    QVector<MarketScreenPage*> widgets_;  ///< parallel to pages_ (nullptr until first shown)
    QVector<QPushButton*> buttons_;       ///< parallel to pages_
    int current_ = -1;
    bool answered_request_ = false; ///< a pending futures request was shown at construction
    QStackedWidget* stack_ = nullptr;
    QWidget* overview_box_ = nullptr; ///< the OVERVIEW buttons (rebuilt on edits)
    QHBoxLayout* overview_layout_ = nullptr;
    QToolButton* add_btn_ = nullptr;          ///< "+" — add an OVERVIEW screen
    QPushButton* add_widget_btn_ = nullptr;   ///< "+ ADD" — add a widget (as on the dashboard)
    QPushButton* reset_btn_ = nullptr;
    QWidget* header_bar_ = nullptr;
    QLabel* session_label_ = nullptr;
    QTimer* session_timer_ = nullptr;

    static QPointer<MarketsScreen> live_; ///< the open MARKETS screen (one at a time)
    static bool pending_request_;
    static QString pending_root_;
};

} // namespace finterm::screens
