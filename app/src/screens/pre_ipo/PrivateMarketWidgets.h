// src/screens/pre_ipo/PrivateMarketWidgets.h
#pragma once
// The PRIVATE MARKET overview as dashboard widgets — one block per tile, so
// the MARKET lens is a canvas the user arranges (drag by the title, resize by
// the corner) instead of a page read top to bottom, and any block can also go
// on the main Dashboard ("Private Market" category in Add Widget).
//
//   pm_c50          C50 liquidity index: level, changes, chart
//   pm_upcoming     upcoming IPOs (what NPM/Hiive do not show)
//   pm_gainers      top private-company gainers, 6 months (model price)
//   pm_losers       top losers, 6 months
//   pm_valuable     most valuable private companies (implied / last round)
//   pm_rounds       recent financing rounds and tenders
//
// Data: PreIpoService (companies, joined to Clarity) and IpoDealsFeed (the
// IPO calendar). A row click opens the company (or deal) in the PRIVATE
// MARKET screen — beside the list when the tile lives there, by switching to
// the screen when it lives on the Dashboard.

#include "screens/dashboard/widgets/BaseWidget.h"
#include "screens/pre_ipo/PreIpoTypes.h"

#include <QDate>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

class QTimer;

namespace finterm::ui {
class DataTable;
}
namespace finterm::services {
class IpoDealIndex;
}

namespace finterm::screens::widgets {

/// "PRICES Nov 19" / "EST. ~Dec 2026" / "S-1 ON FILE" / "PRIVATE"… for a
/// company's IPO status, from the calendar join; with a tooltip, a colour and
/// a sort key (days to pricing; non-dated after). `idx` = the deals to join
/// against (the screen passes its own calendar rows); nullptr = the feed's.
/// The ONE implementation — the screen's lists and these tiles must agree.
QString pm_ipo_cell(const pre_ipo::PrivateCompany& c, QString* tip, QString* color, qint64* sort_key,
                    const services::IpoDealIndex* idx = nullptr);

/// Trading for more than a week — by SEC's listing check, the marketplace's
/// ticker, or a strongly-matched priced calendar deal.
bool pm_is_public(const pre_ipo::PrivateCompany& c, const services::IpoDealIndex* idx = nullptr);

/// A row click on the Dashboard switches to the PRIVATE MARKET screen, which
/// may not exist yet (built lazily) or be hidden: the request is parked here
/// and taken by the screen when it shows.
QVariantMap pm_take_pending_open();

/// Open a company / deal in the PRIVATE MARKET screen (see header comment).
void pm_open_company(QWidget* from, const QString& company_id);
void pm_open_deal(QWidget* from, const QString& deal_id);

/// Shared shell: a DataTable fed by rebuild(), repainted (coalesced) when any
/// source changes; rows carry a key opened on click.
class PmTableWidget : public BaseWidget {
    Q_OBJECT
  public:
    PmTableWidget(const QString& title, const QStringList& headers, QWidget* parent = nullptr);

  protected:
    struct Row {
        QStringList cells;
        QVector<QPair<int, QString>> colors; // column → colour
        QString company_id, deal_id;         // what a click opens
    };
    virtual QVector<Row> rows() const = 0;
    /// Shown when rows() is empty.
    virtual QString empty_text() const { return QStringLiteral("Loading…"); }
    void showEvent(QShowEvent* e) override;
    void schedule();

  private:
    void rebuild();
    ui::DataTable* table_ = nullptr;
    class QLabel* empty_ = nullptr;
    QTimer* coalesce_ = nullptr;
    QVector<Row> shown_;
};

class PmUpcomingIposWidget : public PmTableWidget {
    Q_OBJECT
  public:
    explicit PmUpcomingIposWidget(QWidget* parent = nullptr);

  protected:
    QVector<Row> rows() const override;
    QString empty_text() const override;
};

class PmMoversWidget : public PmTableWidget {
    Q_OBJECT
  public:
    PmMoversWidget(bool gainers, QWidget* parent = nullptr);

  protected:
    QVector<Row> rows() const override;
    QString empty_text() const override;

  private:
    bool gainers_;
};

class PmMostValuableWidget : public PmTableWidget {
    Q_OBJECT
  public:
    explicit PmMostValuableWidget(QWidget* parent = nullptr);

  protected:
    QVector<Row> rows() const override;
};

class PmRecentRoundsWidget : public PmTableWidget {
    Q_OBJECT
  public:
    explicit PmRecentRoundsWidget(QWidget* parent = nullptr);

  protected:
    QVector<Row> rows() const override;
    QString empty_text() const override;
};

/// C50 index: level, changes and a range chart.
class PmC50Widget : public BaseWidget {
    Q_OBJECT
  public:
    explicit PmC50Widget(QWidget* parent = nullptr);

  protected:
    void showEvent(QShowEvent* e) override;

  private:
    void rebuild();
    class QLabel* head_ = nullptr;
    QWidget* chart_host_ = nullptr;
    int chart_points_ = -1;
    QDate chart_last_;
};

} // namespace finterm::screens::widgets
