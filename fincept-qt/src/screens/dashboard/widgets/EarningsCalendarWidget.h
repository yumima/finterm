// src/screens/dashboard/widgets/EarningsCalendarWidget.h
#pragma once
#include "screens/dashboard/widgets/BaseWidget.h"
#include "screens/portfolio/PortfolioTypes.h"

#include <QComboBox>
#include <QDate>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QVector>

namespace fincept::screens::widgets {

/// Earnings Calendar Widget — two views over upcoming earnings prints:
///
///   THIS WEEK  every company reporting between today and Friday, from the
///              Nasdaq public earnings calendar (no API key), ranked by market
///              cap so the names that move the tape are at the top.
///   PORTFOLIO  the next print for each holding of the selected portfolio,
///              from yfinance's per-ticker earnings dates, so a report three
///              weeks out still shows up (the week view would miss it).
///
/// The chosen view is per-instance config, so two tiles can sit side by side
/// showing both.
class EarningsCalendarWidget : public BaseWidget {
    Q_OBJECT
  public:
    explicit EarningsCalendarWidget(const QJsonObject& cfg = {}, QWidget* parent = nullptr);

    QJsonObject config() const override;
    void apply_config(const QJsonObject& cfg) override;

  protected:
    void on_theme_changed() override;
    void showEvent(QShowEvent* e) override;

  private:
    enum class Mode { ThisWeek, Portfolio };

    struct Entry {
        QDate date;
        QString symbol;
        QString company;
        QString when;    // "BMO" / "AMC" (week view) or "in 5d" (portfolio view)
        QString eps_est; // consensus estimate, already formatted
        double rank = 0; // market cap — orders the week view within a day
    };

    void build_body();
    void apply_styles();
    void set_mode(Mode m);
    void refresh_data();
    void populate();
    void show_status(const QString& text);

    // ── This week (Nasdaq calendar) ──────────────────────────────────────────
    void load_week();
    void fetch_day(const QDate& date, qint64 epoch);

    // ── Portfolio (yfinance per-symbol earnings dates) ───────────────────────
    void load_portfolio();
    void on_portfolios_loaded(QVector<portfolio::Portfolio> portfolios);
    void on_summary_loaded(const portfolio::PortfolioSummary& summary);
    void fetch_symbol_earnings(const QStringList& symbols, qint64 epoch);
    void select_portfolio(const QString& id);

    Mode mode_ = Mode::ThisWeek;

    QPushButton* week_btn_ = nullptr;
    QPushButton* portfolio_btn_ = nullptr;
    QComboBox* portfolio_combo_ = nullptr;
    QWidget* header_widget_ = nullptr;
    QFrame* header_sep_ = nullptr;
    QScrollArea* scroll_area_ = nullptr;
    QVBoxLayout* list_layout_ = nullptr;
    QLabel* status_label_ = nullptr;
    QVector<QLabel*> header_labels_;

    QVector<Entry> entries_;
    QString note_; // e.g. "top 120 by market cap" — appended under the list

    // Fetch epoch: bumped on every (re)load so a fan-out still in flight when
    // the user flips tabs or portfolios can't paint over the newer request.
    qint64 epoch_ = 0;
    int pending_ = 0;
    int ok_fetches_ = 0;

    QVector<portfolio::Portfolio> portfolios_;
    QString selected_id_;
    bool portfolios_loaded_ = false;
    bool suppress_combo_signal_ = false;
    bool loaded_once_ = false;

    // Nasdaq returns ~300 rows a day, nearly all micro-caps. Keep the biggest
    // names — a dashboard tile can't show 1500 rows and the user wouldn't read
    // them; populate() says so rather than silently truncating.
    static constexpr int kMaxWeekRows = 120;
    // One yfinance call per holding — bound the fan-out on large portfolios.
    static constexpr int kMaxPortfolioSymbols = 50;
};

} // namespace fincept::screens::widgets
