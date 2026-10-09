// src/screens/fingpt/FinGptAnalysisTab.h
#pragma once
// FinGPT analysis sub-tab — the FinGPT-Forecaster weekly pipeline and the
// FinGPT_Benchmark sentiment task on ONE page, so the forecast and the news
// read sit side by side for a single decision-making view.
//
// Layout (wide-screen first):
//   controls:  [ticker picker ▾] [lookback] [basics] [RUN ANALYSIS]  status
//   verdicts:  [FORECAST chip]  [NEWS SENTIMENT chip]
//   splitter:  EVIDENCE (collapsible) | FORECAST | NEWS SENTIMENT
//
// The ticker picker is an editable combo whose dropdown lists the active
// portfolio's holdings (largest cost basis first): picking one assembles the
// evidence and headlines immediately (no LLM call); RUN ANALYSIS asks the
// model — one forecast call, then one sentiment call per headline.
//
// The two verdict chips are deliberately NOT merged into one score: the
// forecast is the model's reading of the weekly windows, the sentiment is a
// per-headline tally of today's news. They are different claims about
// different things; the page puts them side by side and lets the user weigh
// them.
//
// Pipeline per run (FinGptPrompts.h holds the ported algorithm):
//   candles (3mo) + company info + recent news → weekly slices with news
//   → FinGPT prompt → [Positive Developments]/[Potential Concerns]/
//     [Prediction & Analysis]; then each headline is classified
//     {negative/neutral/positive} with the canonical FinGPT instruction,
//     strictly one call at a time (local backends serve one request well).

#include "services/equity/EquityResearchModels.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTextEdit>
#include <QVariantMap>
#include <QWidget>

#include <functional>

class QVBoxLayout;

namespace fincept::screens::fingpt {

class FinGptAnalysisTab : public QWidget {
    Q_OBJECT
  public:
    explicit FinGptAnalysisTab(QWidget* parent = nullptr);

    QVariantMap save_state() const;
    void restore_state(const QVariantMap& state);

    /// Holdings of the active portfolio (else the first one), largest
    /// |cost basis| first. Synchronous SQLite — callers cache (the ticker
    /// dropdown refreshes at most every 15 s; FinGptScreen memoizes its seed).
    static QStringList portfolio_symbols();

    /// Set the ticker field (symbol-group link / restore). Does not auto-run.
    void set_symbol(const QString& symbol);
    QString symbol() const;

    /// First-open seeding: when the tab has no ticker and no restored answer,
    /// adopt `symbol` and assemble the evidence block and headline table
    /// WITHOUT asking the model — the tab shows real data the moment it
    /// opens, and the LLM spend still waits for the user's click.
    void seed(const QString& symbol);

  private slots:
    void on_run();
    void on_symbol_picked();
    void on_classify_text();
    void on_headline_activated(int row, int column);

  private:
    void build_ui();
    QWidget* build_pane(const QString& title, QWidget* content, QVBoxLayout** out_lay = nullptr);
    /// Gather feeds, assemble the prompt and fill the headline table;
    /// `ask_model` false stops before any LLM call (the seed path).
    void start_run(bool ask_model);
    void unsubscribe_feeds();
    void fail(const QString& message);
    /// All three feeds resolved → assemble the prompt; then the model.
    void try_assemble();
    void run_llm(const QString& user_prompt);
    void render_answer(const QString& raw);
    /// After the forecast (or a data-side forecast failure with a healthy
    /// backend): start the per-headline sentiment chain, or finish the run.
    void begin_classification();
    void classify_next_row();
    void finish_run(const QString& message);
    void set_headlines(const QVector<services::equity::NewsArticle>& articles);
    void run_text_step(int step);
    void finish_text(const QStringList& labels);
    void update_summary();
    void set_cell_label(int row, const QString& label);
    void update_forecast_chip(const QString& call, int direction, const QDate& curday);
    void reset_forecast_chip();
    void update_sentiment_chip();
    /// Stop a running chain and restore the controls, with `message` in the
    /// status line. Used for backend failures — a failed call must become an
    /// error the user sees, never a label.
    void abort_run(const QString& message);
    /// One sentiment call: frame FinGPT's Instruction/Input/Answer prompt and
    /// deliver the parsed label to `done` on the UI thread (epoch-guarded).
    /// On failure `label` is empty and `error` says why — an LLM error or an
    /// empty answer is NOT a neutral classification.
    void classify(const QString& instruction, const QString& input,
                  std::function<void(QString label, QString error)> done);

    // ── Controls ─────────────────────────────────────────────────────────
    QComboBox* symbol_combo_ = nullptr;  // editable; dropdown = portfolio holdings
    QComboBox* weeks_combo_ = nullptr;
    QCheckBox* basics_check_ = nullptr;
    QPushButton* run_btn_ = nullptr;
    QLabel* status_lbl_ = nullptr;

    // ── Verdict strip ────────────────────────────────────────────────────
    QLabel* forecast_chip_ = nullptr;
    QLabel* sentiment_chip_ = nullptr;

    // ── Panes ────────────────────────────────────────────────────────────
    QTextEdit* info_view_ = nullptr;    // the assembled FinGPT data block
    QTextEdit* result_view_ = nullptr;  // the model's structured answer
    QTableWidget* table_ = nullptr;     // headlines + per-row sentiment
    QLabel* summary_lbl_ = nullptr;

    QPlainTextEdit* text_edit_ = nullptr;  // free-text classification
    QCheckBox* vote_check_ = nullptr;
    QPushButton* classify_text_btn_ = nullptr;
    QLabel* text_result_lbl_ = nullptr;

    // ── Run state ────────────────────────────────────────────────────────
    // Epoch guards every async delivery (feed callbacks, LLM chunks and the
    // classification chain): a new run bumps it and strands old deliveries.
    quint64 epoch_ = 0;
    bool running_ = false;   // the main pipeline (gather → forecast → classify)
    bool ask_model_ = true;  // false = seed run: assemble evidence, skip the LLM
    bool text_busy_ = false; // the free-text vote chain
    QString run_symbol_;
    int run_weeks_ = 3;
    bool run_basics_ = true;

    // Feed collection for the current run.
    bool candles_done_ = false, info_done_ = false, news_done_ = false;
    bool assembled_ = false;  // one forecast call per run, however often feeds re-deliver
    QVector<services::equity::Candle> candles_;
    services::equity::StockInfo info_;
    QVector<services::equity::NewsArticle> articles_;
    QStringList row_labels_;  // "" until classified

    QStringList text_votes_;   // per-template labels of the running text vote
    QString text_vote_input_;  // the text the running vote was started on

    QString last_raw_answer_;  // for save_state
    QString last_info_block_;
    /// Ticker the stored answer/evidence was produced FOR. The field can be
    /// edited mid-run (typing, link traffic), so save/restore pairs the
    /// answer with this, never with whatever the field says at save time —
    /// a restored MSFT field must not present AAPL's forecast as its own.
    QString answer_symbol_;
    /// The news feed's error for the current run, when it delivered nothing.
    /// An empty feed with an error is NOT "a ticker with no news": the model
    /// is not asked over it, and the status says why.
    QString news_error_;
    /// Whether the current run actually produced a forecast ask — the
    /// completion message must not claim one on the no-price-window paths.
    bool forecast_made_ = false;
    // The market day the last answer was produced on. The rendered header and
    // chip name the predicted week from THIS, never from "today": a restored
    // answer re-dated to the restore day would claim a week the model never
    // analyzed.
    QDate last_run_day_;
};

} // namespace fincept::screens::fingpt
