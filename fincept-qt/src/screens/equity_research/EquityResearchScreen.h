// src/screens/equity_research/EquityResearchScreen.h
#pragma once
#include "core/symbol/IGroupLinked.h"
#include "screens/IStatefulScreen.h"
#include "services/equity/EquityResearchModels.h"

#include <QDateTime>
#include <QHash>
#include <QHideEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QShowEvent>
#include <QTabWidget>
#include <QTimer>
#include <QWidget>

namespace fincept::screens {

class EquityOverviewTab;
class EquityFinancialsTab;
class EquityAnalysisTab;
class EquityAiTab;
class EquityTechnicalsTab;
class EquityTalippTab;
class EquityPeersTab;
class EquityNewsTab;
class EquitySentimentTab;
class RelationshipMapScreen;

class EquityResearchScreen : public QWidget, public IStatefulScreen, public IGroupLinked {
    Q_OBJECT
    Q_INTERFACES(fincept::IGroupLinked)
  public:
    explicit EquityResearchScreen(QWidget* parent = nullptr);

    void restore_state(const QVariantMap& state) override;
    QVariantMap save_state() const override;
    QString state_key() const override { return "equity_research"; }

    // IGroupLinked — receives group broadcasts and calls load_symbol();
    // also publishes when load_symbol() is triggered internally via the
    // existing EventBus wiring (see .cpp).
    void set_group(SymbolGroup g) override { link_group_ = g; }
    SymbolGroup group() const override { return link_group_; }
    void on_group_symbol_changed(const SymbolRef& ref) override;
    SymbolRef current_symbol() const override;

  protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

  private slots:
    void on_quote_loaded(services::equity::QuoteData quote);
    void on_info_loaded(services::equity::StockInfo info);
    void on_tab_changed(int index);

  private:
    void build_ui();
    QWidget* build_title_bar();
    QWidget* build_quote_bar();
    void update_quote_bar(const services::equity::QuoteData& q);
    /// @param force  Bypass the same-symbol early-return. Lets the user
    ///               re-trigger a load by re-submitting the same ticker
    ///               (e.g. for a manual refresh on breaking news).
    void load_symbol(const QString& symbol, bool force = false);
    /// Clear the quote-bar values to dimmed placeholders while a new symbol
    /// is loading. Stops the header from showing the previous ticker's price
    /// next to the new ticker's name during the cold-load window.
    void clear_quote_bar();

    // Title bar
    QLabel* symbol_label_ = nullptr;
    // Inline symbol search — works like the top CommandBar's stock picker:
    // type → autocomplete dropdown of matching symbols → click or
    // Up/Down+Enter to load. Strips leading "/stock ", "/fund ", "/index "
    // prefixes if the user types them out of habit.
    QLineEdit*   inline_search_input_ = nullptr;
    QListWidget* inline_search_popup_ = nullptr;
    void position_inline_search_popup();
    QString strip_search_prefix(const QString& raw) const;

    // Quote bar
    QLabel* sym_label_ = nullptr;
    QLabel* mkt_status_label_ = nullptr;   // OPEN / PRE / AFTER / CLOSED chip
    QLabel* price_label_ = nullptr;
    QLabel* change_label_ = nullptr;
    QLabel* vol_label_ = nullptr;
    QLabel* hl_label_ = nullptr;
    QLabel* mktcap_label_ = nullptr;
    QLabel* rec_label_ = nullptr;

    // Last exchange code from StockInfo (e.g. "NYQ", "NMS"). Empty when
    // unknown — mkt_status badge stays hidden until info_loaded lands.
    QString current_exchange_;
    QTimer* mkt_status_timer_ = nullptr;   // 30s tick to re-evaluate the badge
    void update_market_status_badge();

    // Finnhub analyst-recommendation chip. Subscribed in load_symbol;
    // rendered into rec_label_ (the existing placeholder in the quote bar).
    void refresh_recommendation_chip();
    // Per-tab data freshness chip — shows "as of HH:mm:ss · 30s ago" for
    // whichever tab is active. Updated on service signals + 1-Hz ticker.
    QLabel* freshness_label_ = nullptr;
    QTimer* freshness_ticker_ = nullptr;
    QHash<int, QDateTime> tab_loaded_at_;

    void mark_tab_loaded(int tab_index);
    void update_freshness_chip();

    // Tabs
    QTabWidget* tab_widget_ = nullptr;
    EquityOverviewTab* overview_tab_ = nullptr;
    EquityFinancialsTab* financials_tab_ = nullptr;
    EquityAnalysisTab* analysis_tab_ = nullptr;
    EquityAiTab* ai_tab_ = nullptr;
    EquityTechnicalsTab* technicals_tab_ = nullptr;
    EquityTalippTab* talipp_tab_ = nullptr;
    EquityPeersTab* peers_tab_ = nullptr;
    EquityNewsTab* news_tab_ = nullptr;
    EquitySentimentTab* sentiment_tab_ = nullptr;
    RelationshipMapScreen* relationships_tab_ = nullptr;

    QTimer* refresh_timer_ = nullptr;
    QString current_symbol_;
    QString current_currency_;

    // Symbol group link — SymbolGroup::None when unlinked.
    SymbolGroup link_group_ = SymbolGroup::None;
};

} // namespace fincept::screens
