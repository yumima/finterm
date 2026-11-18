// src/screens/equity_research/EquityReportTab.h
#pragma once
#include "screens/equity_research/ReportBuilder.h"
#include "services/query/QueryStore.h"

#include <QIcon>
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QTextBrowser>
#include <QWidget>

#include <functional>
#include <memory>

class QLabel;
class QPushButton;
class QTimer;

namespace finterm::screens {

/// A QTextBrowser whose images come from memory (the report's charts).
class ReportView : public QTextBrowser {
    Q_OBJECT
  public:
    using QTextBrowser::QTextBrowser;
    QHash<QString, QImage> images;

  signals:
    void resized();

  protected:
    QVariant loadResource(int type, const QUrl& name) override;
    void resizeEvent(QResizeEvent* e) override;
};

/// ER › REPORT — a quarterly investor report on one company, assembled from
/// everything the terminal holds on it.
///
/// Ten sections, each a few charts and a few sentences: at a glance (the
/// summary, what the numbers show for and against, the year against the
/// index), the business, what is new, financial performance, cash and where
/// it goes, earnings against expectations, valuation and peers, analysts and
/// ownership, advantages and risks, outlook — then the sources, with what
/// loaded, what did not, and why.
///
/// Figures are the vendor's or arithmetic on them (ReportModel.h). The prose
/// is a language model's, written from the company's 10-K / 10-Q and the
/// figures shown, with any number it did not find there removed
/// (NumberGrounding); it never gives a target or a probability. Without a
/// model configured the report still stands on its charts and rules.
///
/// Loads only when the tab is opened; the narrative is kept for a day per
/// prompt, so reopening a report does not write it again.
class EquityReportTab : public QWidget {
    Q_OBJECT
  public:
    explicit EquityReportTab(QWidget* parent = nullptr);
    void set_symbol(const QString& symbol);

  private:
    enum Part { Info, Financials, Extras, Earnings, Peers, News, Technicals, Prices, Benchmark, Filings, History, PartCount };

    void build_ui();
    void apply_theme();
    void reset(const QString& symbol);
    void mark(Part part, report::Source::State state, const QString& detail = {});
    bool settled(Part part) const;
    void schedule_render();
    void render();
    void update_status();
    void read_insiders();
    void read_outlook_record();
    void follow_links(const QString& symbol);
    void remember_and_compare();

    /// One written section: what to ask, and how to read the answer into the report.
    struct Section {
        QString key;  ///< cache key part: "summary", "business"
        QString name; ///< for messages
        QString prompt;
        int max_tokens = 0;
        std::function<bool(const QString& reply)> apply; ///< true when the reply could be read
    };
    void maybe_write_narrative(bool force = false);
    void write_sections(QVector<Section> todo, std::shared_ptr<QStringList> problems, quint64 epoch);
    void run_model(const QString& prompt, int max_tokens, std::function<void(bool ok, QString reply, QString model)> done);
    void record_direction(const QString& model);
    QString export_path(const QString& extension, const QString& filter);
    void export_pdf();
    void print_report_dialog();
    static QIcon printer_icon();
    void export_html();

    QString symbol_;
    quint64 epoch_ = 0;
    QElapsedTimer loaded_at_; ///< since this symbol's report was last (re)built
    report::ReportDoc doc_;
    QVector<report::Source> parts_;

    bool narrative_started_ = false;
    int narrative_pending_ = 0;
    QString narrative_model_;
    QDateTime narrative_written_at_;
    bool remembered_ = false; ///< today's snapshot stored and the earlier one read, for this build
    QHash<QString, QVector<services::equity::Candle>> link_candles_; ///< by futures symbol
    QVector<QPair<QString, QString>> link_defs_;                     ///< (symbol, label), in display order
    int figures_removed_ = 0; ///< by number grounding, across this report's sections
    QString narrative_problem_;

    ReportView* view_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* write_btn_ = nullptr;
    QPushButton* print_btn_ = nullptr;
    QPushButton* pdf_btn_ = nullptr;
    QPushButton* html_btn_ = nullptr;
    QTimer* render_timer_ = nullptr;
};

} // namespace finterm::screens
