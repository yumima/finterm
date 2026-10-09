#pragma once
// FinGptScreen — the FinGPT tab: finterm's port of the AI4Finance FinGPT task
// suite, plus the terminal's own AI chat as its first function.
//
// Three sub-tabs over a QStackedWidget (FnoScreen pattern):
//   Chat        — the full AiChatScreen, embedded, defaulting to the
//                 "FinGPT Analyst" persona (ChatPersonas.h).
//   Forecaster  — the FinGPT-Forecaster weekly pipeline (FinGptForecasterTab).
//   Sentiment   — FinGPT benchmark-style sentiment classification
//                 (FinGptSentimentTab).
//
// What was deliberately NOT ported from the FinGPT repo: the LoRA adapter
// weights (Llama-2-era, unusable by the configured backends without a GGUF
// merge), the FinNLP scrapers (finterm's news/data layer already covers it)
// and the multi-agent debate RAG. The algorithms and prompt formats are in
// FinGptPrompts.h / FinGptParse.h.

#include "core/symbol/IGroupLinked.h"
#include "screens/IStatefulScreen.h"

#include <QPointer>
#include <QPushButton>
#include <QStackedWidget>
#include <QString>
#include <QVariantMap>
#include <QWidget>

namespace fincept::screens { class AiChatScreen; }

namespace fincept::screens::fingpt {

class FinGptForecasterTab;
class FinGptSentimentTab;

class FinGptScreen : public QWidget,
                     public fincept::screens::IStatefulScreen,
                     public fincept::IGroupLinked {
    Q_OBJECT
    Q_INTERFACES(fincept::IGroupLinked)
  public:
    explicit FinGptScreen(QWidget* parent = nullptr);

    // ── IStatefulScreen ────────────────────────────────────────────────────
    QVariantMap save_state() const override;
    void restore_state(const QVariantMap& state) override;
    QString state_key() const override { return "fingpt"; }
    int state_version() const override { return 1; }

    // ── IGroupLinked — symbol-group sync ───────────────────────────────────
    void set_group(fincept::SymbolGroup g) override { link_group_ = g; }
    fincept::SymbolGroup group() const override { return link_group_; }
    void on_group_symbol_changed(const fincept::SymbolRef& ref) override;
    fincept::SymbolRef current_symbol() const override;

    enum SubTab : int {
        TabChat = 0,
        TabForecaster = 1,
        TabSentiment = 2,
        TabCount
    };

  signals:
    /// Forwarded from the embedded chat: ask MainWindow for another chat pane.
    void request_new_chat_pane();

  private slots:
    void on_tab_clicked(int index);

  private:
    void setup_ui();
    QWidget* build_tab_bar();
    QWidget* build_placeholder(const QString& tab_name, const QString& detail);
    void ensure_tab_built(SubTab which);
    void refresh_tab_button_styles();
    /// Ticker a freshly built task tab is seeded with: linked-group traffic,
    /// else the security in focus, else a portfolio holding (cached — it costs
    /// two synchronous SQLite reads), else AAPL.
    QString seed_symbol();

    SubTab active_tab_ = TabChat;
    QStackedWidget* stack_ = nullptr;
    QVector<QPushButton*> tab_btns_;

    QPointer<fincept::screens::AiChatScreen> chat_tab_;
    QPointer<FinGptForecasterTab> forecaster_tab_;
    QPointer<FinGptSentimentTab> sentiment_tab_;

    /// Saved sub-tab states for tabs not yet constructed, applied by
    /// ensure_tab_built. A layout restore must not build every widget tree at
    /// startup just to hand each a map — same laziness rule as pending_symbol_.
    QVariantMap pending_chat_state_;
    QVariantMap pending_forecaster_state_;
    QVariantMap pending_sentiment_state_;

    /// Last linked-group ticker, applied to a task tab when it is first built.
    /// Link traffic must not defeat the lazy construction — a FinGPT pane
    /// parked on Chat should not build two widget trees per symbol click
    /// elsewhere just to store a string.
    QString pending_symbol_;

    /// Memoized portfolio-derived seed, so building the second task tab does
    /// not re-run the SQLite lookups on the GUI thread.
    QString portfolio_seed_cache_;

    fincept::SymbolGroup link_group_ = fincept::SymbolGroup::None;
};

} // namespace fincept::screens::fingpt
