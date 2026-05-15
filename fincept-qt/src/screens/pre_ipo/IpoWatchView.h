#pragma once
// IpoWatchView — research-grade IPO calendar.
//
// Pulls the Nasdaq public calendar (api.nasdaq.com/api/ipo/calendar) for a
// 12-month forward window plus a 6-month back window, then buckets entries
// for fast time-based scanning. Dense table layout designed for wide screens;
// click a row to see expanded details + an SEC EDGAR link in the side panel.
//
// Data source rationale:
//   - Nasdaq's calendar is the canonical free feed for US IPOs (it powers
//     the dashboard's IpoCalendarWidget already, so we know it works).
//   - The previous PreIpoService Python pipelines (Form D / S-1) never
//     produced data even after restart — see the recon report in commit
//     history; that's why this view side-steps them entirely.
//   - SEC EDGAR provides per-filing pages for deeper research; we link
//     out rather than scrape, to keep the view fast.

#include <QDate>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QFrame;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QSplitter;
class QTableWidget;
class QTableWidgetItem;
class QVBoxLayout;

namespace fincept::screens::widgets {

class IpoWatchView : public QWidget {
    Q_OBJECT
  public:
    explicit IpoWatchView(QWidget* parent = nullptr);

  public slots:
    /// Triggered by the screen-level refresh button. Drops cached entries
    /// and re-fetches the full window from Nasdaq.
    void refresh();

  private:
    /// One IPO event — either upcoming or already priced.
    struct Entry {
        QString company;
        QString ticker;
        QString exchange;
        QDate   date;         // expectedPriceDate for upcoming, pricedDate for priced
        QString date_raw;     // M/D/YYYY as Nasdaq returned it
        QString price_range;  // "$15.00-$17.00" for upcoming, "$16.50" for priced
        QString shares_raw;   // "10,000,000"
        QString deal_size;    // "$170M" computed when both fields exist
        QString status;       // "upcoming" | "priced" | "filing"
    };

    enum Bucket {
        BucketThisWeek = 0,
        BucketThisMonth,
        BucketNext6Months,
        BucketNext12Months,
        BucketPriced,
        BucketCount
    };
    static const char* bucket_title(Bucket b);

    void build_ui();
    void apply_styles();
    void fetch_month(const QString& yyyymm);
    void on_fetch_done();
    void populate();                                  // re-renders all buckets from entries_
    Bucket bucket_for(const Entry& e) const;
    void fill_table(QTableWidget* table, const QVector<int>& indices);
    void on_row_selected(const Entry& e);
    QString detail_html(const Entry& e) const;
    static QString format_deal_size(const QString& price_range, const QString& shares);

    // Top filter row
    QWidget*     filter_bar_   = nullptr;
    QLineEdit*   search_       = nullptr;
    QPushButton* refresh_btn_  = nullptr;
    QLabel*      status_lbl_   = nullptr;

    // Layout: splitter [scrollable bucket column | detail panel]
    QSplitter*   splitter_    = nullptr;
    QScrollArea* scroll_      = nullptr;
    QWidget*     bucket_host_ = nullptr;
    QVBoxLayout* bucket_col_  = nullptr;

    // Detail side panel
    QWidget* detail_pane_ = nullptr;
    QLabel*  detail_html_lbl_ = nullptr;

    // Per-bucket tables, in display order
    QTableWidget* tables_[BucketCount] = {nullptr};
    QLabel*       table_headers_[BucketCount] = {nullptr};

    QVector<Entry> entries_;
    int pending_fetches_ = 0;
    QString last_query_;
};

} // namespace fincept::screens::widgets
