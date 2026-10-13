// src/screens/equity_research/EquityAiTab.h
#pragma once
// The per-stock AI tab: every AI read on ONE stock, in one place.
//
// Two forecast engines run side by side on one click, and one immutable track
// record scores them both (AiPredictionRepository, source column):
//
//   quant  — fundamentals + price action → POSITION/WHY/RISKS prose and a
//            structured JSON forecast (direction, target, confidence).
//   fingpt — the AI4Finance FinGPT-Forecaster port (FinGptPrompts.h): weekly
//            price-move windows with the news that landed in each, answered in
//            [Positive Developments]/[Potential Concerns]/[Prediction & Analysis].
//
// The two chips are deliberately NOT merged into one score: a price-driven and
// a news-driven read agreeing (or not) is itself the signal, and the shared
// track record is what eventually says which engine deserves trust per stock.
//
// The left pane is one viewport with four lenses (QUANT / NEWS FORECAST / SENTIMENT /
// EVIDENCE) instead of nested tabs; the right pane is the stock chat — a real
// ChatPane: equity persona, curated tools, voice, and a thread persisted per
// symbol. This tab absorbed the former top-level FinGPT screen.

#include "ai_chat/ChatPane.h"
#include "screens/fingpt/FinGptParse.h"
#include "screens/fingpt/FinGptPrompts.h"
#include "services/equity/EquityResearchModels.h"
#include "storage/repositories/AiPredictionRepository.h"

#include <QElapsedTimer>
#include <QVector>
#include <QWidget>

class QCheckBox;
class QTimer;
class QComboBox;
class QLabel;
class QPushButton;
class QStackedWidget;
class QTableWidget;
class QTextEdit;

namespace fincept::screens {

/// Pure-paint overlay of the AI's predicted path against the real price line.
/// Each prediction is drawn as a segment from (made-on, price-then) to
/// (resolve-date, predicted-target); green = the directional call was right,
/// red = wrong, amber = not yet resolved. No Q_OBJECT — paint only.
class PredictionChart : public QWidget {
  public:
    explicit PredictionChart(QWidget* parent = nullptr);
    void set_data(const QVector<services::equity::Candle>& candles,
                  const QVector<AiPrediction>& predictions);

  protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;

  private:
    QVector<services::equity::Candle> candles_;
    QVector<AiPrediction>             preds_;
    QPoint hover_{-1, -1};   // last mouse position over the plot (-1 = none)
};

class EquityAiTab : public QWidget {
    Q_OBJECT
  public:
    explicit EquityAiTab(QWidget* parent = nullptr);

    void set_symbol(const QString& symbol);
    void set_info(const services::equity::StockInfo& info); // fundamentals (optional enrichment)

  private:
    enum class Lens : int { Quant = 0, News = 1, Headlines = 2, Evidence = 3 };

    void build_ui();
    QWidget* build_analysis_pane();

    // ── Pipeline: one Run drives quant → news → headline labels ──────────────
    void run_all(bool automatic);
    void on_candles(const QVector<services::equity::Candle>& candles);
    void run_quant_forecast(bool automatic);     // engine 1 (fundamentals + price)
    void run_news_forecast(bool automatic);      // engine 2 (FinGPT weekly windows)
    void begin_classification();                 // per-headline sentiment labels
    void classify_next_row();
    void finish_pipeline(const QString& message);
    void resolve_due();                          // fill realized outcomes from real closes
    void refresh_track_record();                 // reload the table + chart + hit-rate
    void maybe_auto_forecast();                  // at most one auto run per ticker per day
    void show_idle_analysis();                   // last stored quant thesis, or the idle prompt

    QString build_quant_prompt() const;          // assemble the quant forecast input
    QString stock_data_block() const;            // the real-data context (forecast + chat)
    int     horizon_days() const;                // from the horizon selector

    void record_news_prediction(const fingpt::ForecasterAnswer& a, const QString& model_used);
    void render_news_answer(const QString& raw, const QString& model_used);
    void set_headlines(const QVector<services::equity::NewsArticle>& articles);
    void set_cell_label(int row, const QString& label);
    void update_sentiment_summary();

    // Verdict chips.
    void reset_chips();
    void update_quant_chip(const AiPrediction& p);
    void update_news_chip(const QString& call, int direction);
    void update_sentiment_chip();

    void set_lens(Lens lens);
    void set_run_busy(bool busy);

    QString current_symbol_;
    services::equity::StockInfo info_;
    bool    info_ready_  = false;
    QVector<services::equity::Candle> candles_;
    bool    running_ = false;            // the whole pipeline
    bool    quant_populated_ = false;    // the quant pane shows real content (not "Loading…")
    bool    data_unavailable_ = false;   // no tradable price history → can't forecast
    quint64 epoch_ = 0;                  // strands stale async deliveries on symbol switch / rerun
    bool    run_automatic_ = false;      // current pipeline is the daily auto-run (skip labels)
    QTimer*        think_timer_ = nullptr; // ticks the "Thinking… Ns" status
    QElapsedTimer  think_start_;

    // News feed for the FinGPT engine (one fill per symbol; latch like the
    // historical feed).
    QVector<services::equity::NewsArticle> articles_;
    QStringList row_labels_;   // "" until classified
    QString     news_error_;   // feed failure ≠ "a ticker with no news"
    bool        news_done_ = false;
    bool        news_run_pending_ = false; // Run clicked before the feed delivered

    // UI — controls
    QComboBox*    horizon_sel_   = nullptr;
    QCheckBox*    auto_chk_      = nullptr;
    QPushButton*  run_btn_       = nullptr;
    QLabel*       status_lbl_    = nullptr;
    QLabel*       accuracy_lbl_  = nullptr;

    // UI — verdict strip
    QLabel* quant_chip_     = nullptr;
    QLabel* news_chip_      = nullptr;
    QLabel* sentiment_chip_ = nullptr;

    // UI — lensed analysis pane
    QVector<QPushButton*> lens_btns_;
    QStackedWidget* lens_stack_    = nullptr;
    QTextEdit*      analysis_view_ = nullptr;  // QUANT: prose + JSON forecast
    QTextEdit*      news_view_     = nullptr;  // NEWS: FinGPT structured answer
    QTableWidget*   table_         = nullptr;  // SENTIMENT: label / date / headline
    QLabel*         summary_lbl_   = nullptr;
    QTextEdit*      evidence_view_ = nullptr;  // EVIDENCE: the FinGPT prompt verbatim

    // UI — chat + record
    ai_chat::ChatPane* chat_pane_ = nullptr;
    QTableWidget* record_table_ = nullptr;
    PredictionChart* chart_     = nullptr;
};

} // namespace fincept::screens
