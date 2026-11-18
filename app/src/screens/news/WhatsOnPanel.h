#pragma once
// WhatsOnPanel.h — the News screen's left drawer: what matters today, this
// week, this month and later (macro releases with their printed numbers, Fed,
// earnings, Treasury auctions, market holidays and expirations, IPO
// pricings, rates, top headlines). Replaced the INTELLIGENCE drawer's
// top-stories/categories view. Pure logic lives in WhatsOnModel.h.

#include "screens/news/WhatsOnModel.h"
#include "services/news/NewsService.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QSet>
#include <QVector>
#include <QWidget>

class QLabel;
class QNetworkAccessManager;
class QPushButton;
class QTimer;
class QVBoxLayout;

namespace finterm::services {
struct EconomicsResult;
}

namespace finterm::screens {

class WhatsOnPanel : public QWidget {
    Q_OBJECT
  public:
    explicit WhatsOnPanel(QWidget* parent = nullptr);

    /// Ranked feed stories (best first); promo/coupon posts are skipped and
    /// the first few kept as TOP HEADLINES.
    void set_headlines(const QVector<services::NewsArticle>& ranked);

  signals:
    void article_clicked(const services::NewsArticle& article);
    /// A row asks to open `screen_id` ("equity_research" {symbol}, "pre_ipo"
    /// {deal_id, key}, "social" {}) right beside the drawer — NewsScreen
    /// hosts it in its peek pane.
    void open_beside(const QString& screen_id, const QVariantMap& args);

  protected:
    void showEvent(QShowEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* e) override;

  private:
    void refresh(bool force = false);
    void fetch_macro(bool force);
    void fetch_fomc(bool force);
    void fetch_rates(bool force);
    void fetch_ipos(bool force);
    void fetch_events(bool force);
    void fetch_auctions(bool force);
    void fetch_fred(bool force);
    void load_user_symbols();
    void apply_fomc(const QVector<QDate>& dates);
    void apply_effr(const QJsonObject& r);
    bool apply_yields(const QJsonArray& rows);
    void apply_yields_result(const services::EconomicsResult& r);
    void apply_events(const QJsonObject& root);
    void apply_auctions();
    void render();
    void render_rates();
    void tick();
    QVector<whats_on::Item> collect_items(const QDateTime& now) const;
    QWidget* make_row(const whats_on::Row& row);
    QWidget* make_headline_row(const services::NewsArticle& a, int index);
    QWidget* make_brief_row();
    void open_symbol(const QString& symbol);
    void apply_styles();

    // Sources (each kept until its next good answer).
    QVector<macro_calendar::MacroEvent> macro_;
    QVector<QDate> fomc_;
    QJsonArray ipo_deals_;
    QJsonObject events_; ///< whats_on_calendar.py: earnings / dividends / insiders
    QJsonArray td_upcoming_, td_auctioned_;
    QVector<treasury_auctions::Auction> auctions_;
    whats_on::FredSeries fred_;
    QSet<QString> fred_inflight_;
    QVector<services::NewsArticle> headlines_;
    QStringList errors_; ///< per source, cleared on that source's next success
    void set_error(const QString& source, const QString& why);
    void clear_error(const QString& source);

    // Rates
    QString policy_range_, effr_, effr_date_, y2_, y10_, yields_date_;

    QSet<QString> mine_; ///< watchlist + portfolio symbols, normalised
    whats_on::Impact min_impact_ = whats_on::Impact::High; ///< impact selector; HIGH by default
    bool mine_only_ = false;
    QSet<int> collapsed_;    ///< whats_on::Section values folded by the user
    QSet<QString> expanded_; ///< Item::row_key of rows showing their detail
    QDateTime last_refresh_;
    QDate rendered_day_;
    QVector<QPair<QPointer<QLabel>, whats_on::Item>> when_labels_; ///< re-labelled each tick
    std::optional<whats_on::Item> next_up_;

    QPushButton* mine_btn_ = nullptr;
    QLabel* rates_label_ = nullptr;
    QLabel* next_label_ = nullptr;
    QLabel* status_label_ = nullptr;
    QVBoxLayout* body_ = nullptr; ///< sections + headlines, rebuilt by render()
    QNetworkAccessManager* nam_ = nullptr;
    QTimer* timer_ = nullptr;
    QTimer* tick_ = nullptr;
};

} // namespace finterm::screens
