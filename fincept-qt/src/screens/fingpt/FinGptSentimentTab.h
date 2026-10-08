// src/screens/fingpt/FinGptSentimentTab.h
#pragma once
// FinGPT sentiment sub-tab — the FinGPT_Benchmark sentiment task
// ("Instruction:/Input:/Answer: " frame, {negative/neutral/positive}) run
// zero-shot on the "fingpt"-role model.
//
// Two surfaces:
//   - a free-text box, optionally classified with FinGPT's 5-template
//     majority vote (one call per template);
//   - a batch table of the symbol's current headlines, classified one call
//     per headline with the canonical instruction (voting a 20-row batch
//     would be 100 calls on a local model — the batch stays single-template).
//
// Calls run strictly one at a time, chained from each completion: the local
// daemon gates concurrent yfinance access and a local model serves one
// request well, several badly.

#include "services/equity/EquityResearchModels.h"

#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVariantMap>
#include <QWidget>

namespace fincept::screens::fingpt {

class FinGptSentimentTab : public QWidget {
    Q_OBJECT
  public:
    explicit FinGptSentimentTab(QWidget* parent = nullptr);

    QVariantMap save_state() const;
    void restore_state(const QVariantMap& state);

    /// Set the ticker field (symbol-group link / restore). Does not auto-fetch.
    void set_symbol(const QString& symbol);
    QString symbol() const;

  private slots:
    void on_fetch_headlines();
    void on_classify_all();
    void on_classify_text();

  private:
    void build_ui();
    void set_headlines(const QVector<services::equity::NewsArticle>& articles);
    /// Classify the next still-unlabelled row, chaining from completion.
    void classify_next_row();
    /// Run template `step` of the single-text vote, chaining from completion.
    void run_text_step(int step);
    void finish_text(const QStringList& labels);
    void update_summary();
    void set_cell_label(int row, const QString& label);
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
    QLineEdit* symbol_edit_ = nullptr;
    QPushButton* fetch_btn_ = nullptr;
    QPushButton* classify_all_btn_ = nullptr;
    QLabel* status_lbl_ = nullptr;

    QPlainTextEdit* text_edit_ = nullptr;
    QCheckBox* vote_check_ = nullptr;
    QPushButton* classify_text_btn_ = nullptr;
    QLabel* text_result_lbl_ = nullptr;

    QTableWidget* table_ = nullptr;
    QLabel* summary_lbl_ = nullptr;

    // ── Run state ────────────────────────────────────────────────────────
    quint64 epoch_ = 0;    // bumps on fetch/classify start; strands old chains
    bool busy_ = false;    // an LLM chain is in flight
    QVector<services::equity::NewsArticle> articles_;
    QStringList row_labels_;      // "" until classified
    QStringList text_votes_;      // per-template labels of the running text vote
};

} // namespace fincept::screens::fingpt
