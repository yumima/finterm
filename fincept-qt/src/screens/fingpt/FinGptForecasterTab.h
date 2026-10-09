// src/screens/fingpt/FinGptForecasterTab.h
#pragma once
// FinGPT-Forecaster sub-tab — the AI4Finance FinGPT weekly forecaster pipeline
// run against finterm's own data layer and the "fingpt"-role model.
//
// Pipeline per run (see FinGptPrompts.h for the ported algorithm):
//   candles (3mo) + company info + recent news  →  weekly slices with news
//   →  FinGPT prompt (verbatim system + assembled user block)
//   →  [Positive Developments] / [Potential Concerns] / [Prediction & Analysis]
//
// The assembled data block is shown next to the answer (upstream's Gradio demo
// does the same) so the user can see exactly which evidence produced which
// factor — that transparency, not the prediction, is the feature.

#include "services/equity/EquityResearchModels.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QVariantMap>
#include <QWidget>

namespace fincept::screens::fingpt {

class FinGptForecasterTab : public QWidget {
    Q_OBJECT
  public:
    explicit FinGptForecasterTab(QWidget* parent = nullptr);

    QVariantMap save_state() const;
    void restore_state(const QVariantMap& state);

    /// Set the ticker field (symbol-group link / restore). Does not auto-run.
    void set_symbol(const QString& symbol);
    QString symbol() const;

    /// First-open seeding: when the tab has no ticker and no restored answer,
    /// adopt `symbol` and assemble the evidence block (prices + news +
    /// profile) WITHOUT asking the model — the tab shows real data the moment
    /// it opens, and the LLM spend still waits for the user's click.
    void seed(const QString& symbol);

  private slots:
    void on_run();

  private:
    void build_ui();
    /// Gather feeds and assemble the prompt; `ask_model` false stops after
    /// the evidence pane is filled (the seed path).
    void start_run(bool ask_model);
    void unsubscribe_feeds();
    void fail(const QString& message);
    /// All three feeds resolved → assemble the prompt and call the model.
    void try_assemble();
    void run_llm(const QString& user_prompt);
    void render_answer(const QString& raw);

    // ── Controls ─────────────────────────────────────────────────────────
    QLineEdit* symbol_edit_ = nullptr;
    QComboBox* weeks_combo_ = nullptr;
    QCheckBox* basics_check_ = nullptr;
    QPushButton* run_btn_ = nullptr;
    QLabel* status_lbl_ = nullptr;

    // ── Panes ────────────────────────────────────────────────────────────
    QTextEdit* info_view_ = nullptr;    // the assembled FinGPT data block
    QTextEdit* result_view_ = nullptr;  // the model's structured answer

    // ── Run state ────────────────────────────────────────────────────────
    // Epoch guards every async delivery (feed callbacks and LLM chunks):
    // a new run or symbol change bumps it and strands the old deliveries.
    quint64 epoch_ = 0;
    bool running_ = false;
    bool ask_model_ = true;  // false = seed run: assemble evidence, skip the LLM
    QString run_symbol_;
    int run_weeks_ = 3;
    bool run_basics_ = true;

    // Feed collection for the current run.
    bool candles_done_ = false, info_done_ = false, news_done_ = false;
    bool assembled_ = false;  // one LLM call per run, however often feeds re-deliver
    QVector<services::equity::Candle> candles_;
    services::equity::StockInfo info_;
    QVector<services::equity::NewsArticle> articles_;

    QString last_raw_answer_;   // for save_state
    QString last_info_block_;
    // The market day the last answer was produced on. The rendered header
    // names the predicted week from THIS, never from "today": a restored
    // answer re-dated to the restore day would claim a week the model never
    // analyzed.
    QDate last_run_day_;
};

} // namespace fincept::screens::fingpt
