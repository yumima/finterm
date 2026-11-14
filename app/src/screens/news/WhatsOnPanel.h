#pragma once
// WhatsOnPanel.h — the News screen's left drawer: what matters today, this
// week, this month and later (macro releases, Fed, IPO pricings, rates, top
// headlines). Replaced the INTELLIGENCE drawer's top-stories/categories view.
// Pure logic lives in WhatsOnModel.h.

#include "screens/news/WhatsOnModel.h"
#include "services/news/NewsService.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QVector>
#include <QWidget>

class QLabel;
class QNetworkAccessManager;
class QPushButton;
class QTimer;
class QVBoxLayout;

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

  protected:
    void showEvent(QShowEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* e) override;

  private:
    void refresh(bool force = false);
    void fetch_macro(bool force);
    void fetch_fomc(bool force);
    void fetch_rates(bool force);
    void fetch_ipos(bool force);
    void apply_fomc(const QVector<QDate>& dates);
    void apply_effr(const QJsonObject& r);
    bool apply_yields(const QJsonArray& rows);
    void render();
    void render_rates();
    QWidget* make_row(const whats_on::Item& item);
    QWidget* make_headline_row(const services::NewsArticle& a, int index);
    QWidget* make_brief_row();
    void apply_styles();

    // Sources (each kept until its next good answer).
    QVector<macro_calendar::MacroEvent> macro_;
    QVector<QDate> fomc_;
    QJsonArray ipo_deals_;
    QVector<services::NewsArticle> headlines_;
    QStringList errors_; ///< per source, cleared on that source's next success
    void set_error(const QString& source, const QString& why);
    void clear_error(const QString& source);

    // Rates
    QString policy_range_, effr_, effr_date_, y2_, y10_, yields_date_;

    bool top_tier_only_ = true;
    QDateTime last_refresh_;

    QPushButton* filter_btn_ = nullptr;
    QLabel* rates_label_ = nullptr;
    QLabel* status_label_ = nullptr;
    QVBoxLayout* body_ = nullptr; ///< bucket sections + headlines, rebuilt by render()
    QNetworkAccessManager* nam_ = nullptr;
    QTimer* timer_ = nullptr;
};

} // namespace finterm::screens
