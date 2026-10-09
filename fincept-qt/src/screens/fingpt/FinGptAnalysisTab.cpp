// src/screens/fingpt/FinGptAnalysisTab.cpp
#include "screens/fingpt/FinGptAnalysisTab.h"

#include "ai_chat/LlmService.h"
#include "core/util/BarTime.h"
#include "screens/fingpt/FinGptParse.h"
#include "screens/fingpt/FinGptPrompts.h"
#include "services/app_context/AppContextService.h"
#include "services/equity/EquityResearchService.h"
#include "services/query/QueryStore.h"
#include "storage/repositories/PortfolioRepository.h"
#include "ui/theme/Theme.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QPointer>
#include <QSplitter>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <memory>

namespace fincept::screens::fingpt {

namespace colors = fincept::ui::colors;
using services::equity::Candle;
using services::equity::EquityResearchService;
using services::equity::NewsArticle;
using services::equity::StockInfo;
using services::query::QueryStore;

namespace {

// FinGPT's own lookback cap is 4 weeks; 3mo of daily candles covers the
// 4-week window with margin and rides the existing historical cache TTL.
constexpr const char* kHistoryPeriod = "3mo";

QString label_color(const QString& label) {
    if (label == QStringLiteral("positive")) return colors::POSITIVE();
    if (label == QStringLiteral("negative")) return colors::NEGATIVE();
    return colors::TEXT_SECONDARY();
}

// The one-liner that keeps a zero-shot chat model inside FinGPT's completion
// contract (the fine-tuned adapters needed no reminder; a chat model does).
QString sentiment_system_prompt() {
    return QStringLiteral("You are a financial sentiment classifier. Reply with exactly one "
                          "word from the offered options and nothing else.");
}

// Editable ticker combo whose dropdown shows the CURRENT portfolio holdings:
// refreshed on open (at most every 15 s — the lookup is synchronous SQLite on
// the GUI thread), so a holding bought this session is pickable without
// restarting the pane. Typed text survives the refresh.
class TickerCombo : public QComboBox {
  public:
    explicit TickerCombo(QWidget* parent = nullptr) : QComboBox(parent) {}
    void refresh_items() {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - cache_ms_ < 15000)
            return;
        cache_ms_ = now;
        const QString keep = currentText();
        clear();
        addItems(FinGptAnalysisTab::portfolio_symbols());
        setEditText(keep);
    }
    void showPopup() override {
        refresh_items();
        QComboBox::showPopup();
    }

  private:
    qint64 cache_ms_ = -15000;  // first call always queries
};

} // namespace

// Cost basis in the instrument's own currency — a familiarity ranking, not a
// valuation (no FX, no live prices; PortfolioService owns the real figure).
// |qty × avg price| so a short position still counts as a holding the user
// knows. Zero-cost imports leave every value at 0; the stable sort then keeps
// the portfolio's own order, so the first real holding still leads.
QStringList FinGptAnalysisTab::portfolio_symbols() {
    auto& repo = fincept::PortfolioRepository::instance();
    QString pid = fincept::services::AppContextService::instance().snapshot().portfolio_id;
    if (pid.isEmpty()) {
        const auto ports = repo.list_portfolios();
        if (ports.is_ok() && !ports.value().isEmpty())
            pid = ports.value().first().id;
    }
    if (pid.isEmpty())
        return {};
    const auto assets = repo.get_assets(pid);
    if (!assets.is_ok())
        return {};
    QVector<QPair<QString, double>> ranked;
    for (const auto& a : assets.value()) {
        if (a.symbol.isEmpty())
            continue;
        ranked.append({a.symbol.toUpper(), std::abs(a.quantity * a.avg_buy_price)});
    }
    std::stable_sort(ranked.begin(), ranked.end(),
                     [](const auto& l, const auto& r) { return l.second > r.second; });
    QStringList out;
    for (const auto& [sym, v] : ranked)
        if (!out.contains(sym))
            out << sym;
    return out;
}

FinGptAnalysisTab::FinGptAnalysisTab(QWidget* parent) : QWidget(parent) {
    build_ui();
}

void FinGptAnalysisTab::build_ui() {
    setObjectName("fingptAnalysis");
    setStyleSheet(QStringLiteral(
        "#fingptAnalysis { background:%1; }"
        "QLabel { color:%2; font-size:11px; background:transparent; }"
        "QLabel#paneTitle { color:%8; font-size:10px; font-weight:700; letter-spacing:0.8px; }"
        "QLabel#verdictChip { background:%3; border:1px solid %5; padding:4px 10px; "
        "  font-size:12px; }"
        "QLineEdit, QComboBox, QPlainTextEdit { background:%3; color:%4; border:1px solid %5; "
        "  padding:4px 6px; font-size:12px; }"
        "QComboBox QAbstractItemView { background:%3; color:%4; "
        "  selection-background-color:%5; }"
        "QCheckBox { color:%2; font-size:11px; }"
        "QPushButton#runBtn { background:%6; color:%7; border:none; padding:5px 16px; "
        "  font-size:11px; font-weight:700; letter-spacing:0.5px; }"
        "QPushButton#runBtn:disabled { background:%5; color:%2; }"
        "QPushButton { background:%3; color:%4; border:1px solid %5; padding:5px 12px; "
        "  font-size:11px; font-weight:700; letter-spacing:0.5px; }"
        "QPushButton:hover { border-color:%6; color:%6; }"
        "QPushButton:disabled { color:%2; }"
        "QTextEdit { background:%3; color:%4; border:1px solid %5; font-size:12px; }"
        "QTableWidget { background:%3; color:%4; border:1px solid %5; font-size:12px; "
        "  gridline-color:transparent; }"
        "QTableWidget::item { padding:3px 6px; border-bottom:1px solid %5; }"
        "QHeaderView::section { background:%3; color:%4; border:none; border-bottom:2px solid %6; "
        "  padding:4px 6px; font-weight:700; }")
        .arg(colors::BG_BASE(), colors::TEXT_SECONDARY(), colors::BG_SURFACE(),
             colors::TEXT_PRIMARY(), colors::BORDER_DIM(), colors::AMBER(),
             colors::TEXT_ON_ACCENT(), colors::TEXT_DIM()));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 8, 10, 8);
    root->setSpacing(8);

    // ── Controls ─────────────────────────────────────────────────────────
    auto* controls = new QHBoxLayout();
    controls->setSpacing(8);
    controls->addWidget(new QLabel(QStringLiteral("Ticker"), this));
    auto* ticker_combo = new TickerCombo(this);
    symbol_combo_ = ticker_combo;
    symbol_combo_->setEditable(true);
    symbol_combo_->setInsertPolicy(QComboBox::NoInsert);
    symbol_combo_->setMaxVisibleItems(20);
    symbol_combo_->setMinimumWidth(110);
    ticker_combo->refresh_items();
    symbol_combo_->setEditText(QString());
    symbol_combo_->lineEdit()->setPlaceholderText(QStringLiteral("e.g. AAPL"));
    symbol_combo_->setToolTip(QStringLiteral(
        "Type any ticker, or open the dropdown to pick a portfolio holding "
        "(largest position first). Picking one assembles the evidence and "
        "headlines immediately — no model call until RUN ANALYSIS."));
    // Picking a holding gathers data at once; Enter on a typed ticker runs
    // the full analysis (the typist already knows what they want asked).
    connect(symbol_combo_, QOverload<int>::of(&QComboBox::activated), this,
            &FinGptAnalysisTab::on_symbol_picked);
    connect(symbol_combo_->lineEdit(), &QLineEdit::returnPressed, this, &FinGptAnalysisTab::on_run);
    controls->addWidget(symbol_combo_);

    controls->addWidget(new QLabel(QStringLiteral("Lookback"), this));
    weeks_combo_ = new QComboBox(this);
    for (int w = 1; w <= 4; ++w)
        weeks_combo_->addItem(QStringLiteral("%1 week%2").arg(w).arg(w > 1 ? "s" : ""), w);
    weeks_combo_->setCurrentIndex(2);  // FinGPT's default n_weeks = 3
    controls->addWidget(weeks_combo_);

    basics_check_ = new QCheckBox(QStringLiteral("Basic financials"), this);
    basics_check_->setChecked(true);
    basics_check_->setToolTip(QStringLiteral(
        "Include the company's current fundamentals in the prompt "
        "(FinGPT's \"Use Latest Basic Financials\" option)."));
    controls->addWidget(basics_check_);

    run_btn_ = new QPushButton(QStringLiteral("RUN ANALYSIS"), this);
    run_btn_->setObjectName("runBtn");
    run_btn_->setCursor(Qt::PointingHandCursor);
    run_btn_->setToolTip(QStringLiteral(
        "One forecast call, then one sentiment call per headline — all on the "
        "fingpt-role model."));
    connect(run_btn_, &QPushButton::clicked, this, &FinGptAnalysisTab::on_run);
    controls->addWidget(run_btn_);

    status_lbl_ = new QLabel(
        QStringLiteral("FinGPT forecaster + sentiment on one page — pick a ticker and RUN ANALYSIS."),
        this);
    controls->addWidget(status_lbl_, 1);
    root->addLayout(controls);

    // ── Verdict strip ────────────────────────────────────────────────────
    // Two chips, deliberately not merged into one score: the forecast and the
    // headline tally are different claims; the strip juxtaposes, never
    // reconciles.
    auto* verdicts = new QHBoxLayout();
    verdicts->setSpacing(8);
    forecast_chip_ = new QLabel(this);
    forecast_chip_->setObjectName("verdictChip");
    forecast_chip_->setToolTip(QStringLiteral(
        "The model's structured reading of the weekly news windows."));
    verdicts->addWidget(forecast_chip_);
    sentiment_chip_ = new QLabel(this);
    sentiment_chip_->setObjectName("verdictChip");
    sentiment_chip_->setToolTip(QStringLiteral(
        "Per-headline FinGPT sentiment tally of the current news. A separate "
        "read from the forecast — the page shows both, it does not average them."));
    verdicts->addWidget(sentiment_chip_);
    verdicts->addStretch(1);
    root->addLayout(verdicts);
    reset_forecast_chip();
    update_sentiment_chip();

    // ── Panes ────────────────────────────────────────────────────────────
    auto* split = new QSplitter(Qt::Horizontal, this);

    info_view_ = new QTextEdit(this);
    info_view_->setReadOnly(true);
    info_view_->setFontFamily(QStringLiteral("Consolas"));
    info_view_->setPlaceholderText(QStringLiteral(
        "The assembled evidence lands here: company introduction, each week's price move with "
        "the news that accompanied it, and current fundamentals — exactly what the model is "
        "shown, nothing more."));
    split->addWidget(build_pane(QStringLiteral("EVIDENCE — WHAT THE MODEL SEES"), info_view_));

    result_view_ = new QTextEdit(this);
    result_view_->setReadOnly(true);
    result_view_->setPlaceholderText(QStringLiteral(
        "The model's answer lands here, in FinGPT's structure:\n\n[Positive Developments]\n"
        "[Potential Concerns]\n[Prediction & Analysis]"));
    split->addWidget(build_pane(QStringLiteral("FORECAST — NEXT WEEK"), result_view_));

    table_ = new QTableWidget(0, 3, this);
    table_->setHorizontalHeaderLabels({QStringLiteral("SENTIMENT"), QStringLiteral("DATE"),
                                       QStringLiteral("HEADLINE")});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setShowGrid(false);
    connect(table_, &QTableWidget::cellDoubleClicked, this,
            &FinGptAnalysisTab::on_headline_activated);

    QVBoxLayout* sent_lay = nullptr;
    QWidget* sent_pane = build_pane(QStringLiteral("NEWS SENTIMENT"), table_, &sent_lay);
    summary_lbl_ = new QLabel(QString(), sent_pane);
    sent_lay->addWidget(summary_lbl_);

    // Free-text classification, compact, under the table: the one FinGPT
    // sentiment surface that is not headline-driven.
    text_edit_ = new QPlainTextEdit(sent_pane);
    text_edit_->setPlaceholderText(QStringLiteral(
        "Or paste a headline, tweet or sentence to classify…"));
    text_edit_->setMaximumHeight(46);
    sent_lay->addWidget(text_edit_);
    auto* text_row = new QHBoxLayout();
    text_row->setSpacing(8);
    classify_text_btn_ = new QPushButton(QStringLiteral("CLASSIFY TEXT"), sent_pane);
    classify_text_btn_->setCursor(Qt::PointingHandCursor);
    connect(classify_text_btn_, &QPushButton::clicked, this, &FinGptAnalysisTab::on_classify_text);
    text_row->addWidget(classify_text_btn_);
    vote_check_ = new QCheckBox(QStringLiteral("5-template vote"), sent_pane);
    vote_check_->setChecked(true);
    vote_check_->setToolTip(QStringLiteral(
        "FinGPT's multi-template majority vote: the text is classified under five "
        "differently-worded instructions and the labels vote (5 calls instead of 1)."));
    text_row->addWidget(vote_check_);
    text_result_lbl_ = new QLabel(QStringLiteral("—"), sent_pane);
    text_row->addWidget(text_result_lbl_, 1);
    sent_lay->addLayout(text_row);
    split->addWidget(sent_pane);

    // The evidence pane is the only collapsible one: on a narrower window the
    // user can drag it shut and keep the two verdict panes.
    split->setSizes({340, 420, 420});
    split->setCollapsible(0, true);
    split->setCollapsible(1, false);
    split->setCollapsible(2, false);
    root->addWidget(split, 1);

    auto* disclaimer = new QLabel(
        QStringLiteral("FinGPT-style analysis (AI4Finance). The forecast is a structured reading of the "
                       "window's news and the sentiment a per-headline tally — informational, not advice. "
                       "Double-click a headline to open the article."),
        this);
    disclaimer->setWordWrap(true);
    disclaimer->setStyleSheet(QStringLiteral("color:%1; font-size:10px;").arg(colors::TEXT_DIM()));
    root->addWidget(disclaimer);
}

QWidget* FinGptAnalysisTab::build_pane(const QString& title, QWidget* content,
                                       QVBoxLayout** out_lay) {
    auto* wrap = new QWidget(this);
    auto* lay = new QVBoxLayout(wrap);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(4);
    auto* t = new QLabel(title, wrap);
    t->setObjectName("paneTitle");
    lay->addWidget(t);
    lay->addWidget(content, 1);
    if (out_lay)
        *out_lay = lay;
    return wrap;
}

QString FinGptAnalysisTab::symbol() const {
    return symbol_combo_ ? symbol_combo_->currentText().trimmed().toUpper() : QString();
}

void FinGptAnalysisTab::set_symbol(const QString& symbol) {
    if (symbol_combo_ && symbol_combo_->currentText().trimmed().toUpper() != symbol.trimmed().toUpper())
        symbol_combo_->setEditText(symbol.trimmed().toUpper());
}

void FinGptAnalysisTab::unsubscribe_feeds() {
    // Blanket by owner: these three feeds are this tab's only subscriptions,
    // and hand-built key strings quietly stop matching when the service bumps
    // a cache version segment (the info key already carries ":v3:").
    //
    // Called from start_run() (and the restore-path headline fetch) only —
    // NEVER from inside a feed callback. QueryStore's warm-cache path delivers
    // synchronously from subscribe() without copying the functor, so a
    // self-unsubscribe mid-delivery would destroy the std::function while it
    // executes. Between runs the feeds simply stay subscribed (same lifecycle
    // as the ER tabs); destruction is covered by QueryStore's owner hook.
    QueryStore::instance().unsubscribe_all(this);
}

void FinGptAnalysisTab::fail(const QString& message) {
    running_ = false;
    run_btn_->setEnabled(true);
    status_lbl_->setText(message);
}

void FinGptAnalysisTab::on_run() { start_run(/*ask_model=*/true); }

void FinGptAnalysisTab::on_symbol_picked() {
    // A dropdown pick is a browse, not a question: assemble the evidence and
    // headlines for the holding, keep the model idle until RUN ANALYSIS.
    start_run(/*ask_model=*/false);
}

void FinGptAnalysisTab::seed(const QString& symbol) {
    // A run in flight, a restored answer, or restored evidence outranks the
    // seed entirely (re-fetching a restored info block on every open would
    // make the saved state dead weight); a ticker already in the field (link
    // traffic, restore) outranks the seed SYMBOL but still deserves its
    // evidence assembled.
    if (running_)
        return;
    if (!last_raw_answer_.isEmpty() || !last_info_block_.isEmpty()) {
        // Restored run: the evidence and answer panes are already filled, but
        // headlines are not persisted — fetch them so the sentiment pane opens
        // with content too (no LLM call; labelling waits for the user).
        if (articles_.isEmpty() && !this->symbol().isEmpty()) {
            ++epoch_;
            const quint64 epoch = epoch_;
            news_done_ = false;
            unsubscribe_feeds();
            EquityResearchService::instance().subscribe_news(
                this, this->symbol(), [this, epoch](const QueryStore::State& s) {
                    if (epoch != epoch_ || news_done_)
                        return;
                    const auto arts = s.data.value<QVector<NewsArticle>>();
                    if (!arts.isEmpty()) {
                        news_done_ = true;
                        articles_ = arts;
                        set_headlines(arts);
                    } else if (!s.loading) {
                        news_done_ = true;  // a failed restore fetch stays quiet
                    }
                });
        }
        return;
    }
    if (this->symbol().isEmpty())
        set_symbol(symbol);
    if (!this->symbol().isEmpty())
        start_run(/*ask_model=*/false);
}

void FinGptAnalysisTab::start_run(bool ask_model) {
    if (text_busy_) {
        // A full run outranks a free-text vote in flight; a background gather
        // does not (stranding the user's vote for a browse would be rude).
        if (!ask_model)
            return;
        ++epoch_;  // strand the vote chain
        text_busy_ = false;
        classify_text_btn_->setEnabled(true);
        text_result_lbl_->setText(QStringLiteral("—"));
    }
    if (running_) {
        if (ask_model_) {
            // A real model run in flight always wins — a second click or a
            // dropdown browse must not strand a paid-for answer. A refused
            // browse pick has already changed the field, so put the running
            // ticker back: the answer about to land must not render under
            // another symbol, and a silent refusal reads as a dead dropdown.
            if (!ask_model) {
                set_symbol(run_symbol_);
                status_lbl_->setText(
                    QStringLiteral("A model run for %1 is in flight — pick again when it "
                                   "finishes.").arg(run_symbol_));
            }
            return;
        }
        ++epoch_;  // supersede the seed/browse gather
        running_ = false;
        // The validation returns below must not inherit the gather's disabled
        // button — a stranded gather never reaches its own re-enable path.
        run_btn_->setEnabled(true);
    }
    const QString sym = symbol();
    if (sym.isEmpty()) {
        if (ask_model)
            status_lbl_->setText(QStringLiteral("Enter a ticker first."));
        return;
    }
    // The evidence pane and headline table need no model; only the ask does.
    if (ask_model && !ai_chat::LlmService::instance().is_configured()) {
        status_lbl_->setText(QStringLiteral(
            "No model configured — open Settings → AI Config and point finterm at a model."));
        return;
    }
    ask_model_ = ask_model;

    ++epoch_;
    const quint64 epoch = epoch_;
    running_ = true;
    run_btn_->setEnabled(false);
    run_symbol_ = sym;
    run_weeks_ = weeks_combo_->currentData().toInt();
    run_basics_ = basics_check_->isChecked();
    candles_done_ = info_done_ = news_done_ = false;
    assembled_ = false;
    forecast_made_ = false;
    news_error_.clear();
    candles_.clear();
    info_ = StockInfo{};
    articles_.clear();
    row_labels_.clear();
    // The previous run's answer is void the moment a new ticker's run starts:
    // keeping it would let save_state() pair the NEW symbol with the OLD
    // forecast when this run fails short of an answer.
    last_raw_answer_.clear();
    last_info_block_.clear();
    last_run_day_ = QDate();
    answer_symbol_.clear();
    table_->setRowCount(0);
    summary_lbl_->clear();
    result_view_->clear();
    info_view_->clear();
    reset_forecast_chip();
    update_sentiment_chip();
    status_lbl_->setText(QStringLiteral("Gathering prices, profile and news for %1…").arg(sym));

    unsubscribe_feeds();
    auto& svc = EquityResearchService::instance();

    svc.subscribe_historical(this, sym, QString::fromLatin1(kHistoryPeriod),
                             [this, epoch](const QueryStore::State& s) {
        if (epoch != epoch_) return;
        const auto candles = s.data.value<QVector<Candle>>();
        if (!candles.isEmpty()) {
            candles_ = candles;
            candles_done_ = true;
            try_assemble();
        } else if (!s.loading) {
            // No price history at all — nothing to anchor the weekly window
            // on. Say so; do not fabricate a window out of an empty series.
            candles_done_ = true;
            try_assemble();
        }
    });

    svc.subscribe_info(this, sym, [this, epoch](const QueryStore::State& s) {
        if (epoch != epoch_) return;
        const auto info = s.data.value<StockInfo>();
        if (!info.symbol.isEmpty()) {
            info_ = info;
            info_done_ = true;
            try_assemble();
        } else if (!s.loading) {
            info_done_ = true;  // intro degrades gracefully without a profile
            try_assemble();
        }
    });

    // One table fill per run: the subscription stays live and re-delivers on
    // QueryStore's refresh cadence — re-running set_headlines() then would
    // wipe labels mid- or post-classification. news_done_ is the latch.
    svc.subscribe_news(this, sym, [this, epoch](const QueryStore::State& s) {
        if (epoch != epoch_ || news_done_) return;
        const auto arts = s.data.value<QVector<NewsArticle>>();
        if (!arts.isEmpty()) {
            news_done_ = true;
            articles_ = arts;
            set_headlines(arts);
            try_assemble();
        } else if (!s.loading) {
            // A failed feed is NOT a ticker without news — remember the error
            // so the run refuses to present a news-driven forecast over it.
            news_error_ = s.error;
            news_done_ = true;  // genuinely-empty weeks carry "No relative news reported."
            set_headlines({});
            try_assemble();
        }
    });
}

void FinGptAnalysisTab::set_headlines(const QVector<NewsArticle>& articles) {
    articles_ = articles;
    row_labels_ = QStringList();
    for (int i = 0; i < articles_.size(); ++i)
        row_labels_ << QString();
    table_->setRowCount(static_cast<int>(articles_.size()));
    for (int i = 0; i < articles_.size(); ++i) {
        auto* sent = new QTableWidgetItem(QStringLiteral("—"));
        sent->setForeground(QColor(colors::TEXT_DIM()));
        table_->setItem(i, 0, sent);
        // published_date is an EVENT instant; its UTC prefix dates every
        // evening headline one day late — show the ET calendar day.
        const QDateTime pub = QDateTime::fromString(articles_[i].published_date, Qt::ISODate).toUTC();
        table_->setItem(i, 1, new QTableWidgetItem(
            pub.isValid() ? core::bartime::market_date_et(pub).toString(Qt::ISODate)
                          : articles_[i].published_date.left(10)));
        auto* head = new QTableWidgetItem(articles_[i].title);
        QString tip = articles_[i].description;
        if (!articles_[i].url.isEmpty())
            tip += (tip.isEmpty() ? QString() : QStringLiteral("\n\n"))
                   + QStringLiteral("Double-click to open: ") + articles_[i].url;
        head->setToolTip(tip);
        head->setData(Qt::UserRole, articles_[i].url);
        table_->setItem(i, 2, head);
    }
    summary_lbl_->clear();
    update_sentiment_chip();
}

void FinGptAnalysisTab::on_headline_activated(int row, int column) {
    Q_UNUSED(column);
    QTableWidgetItem* head = table_->item(row, 2);
    if (!head)
        return;
    const QString url = head->data(Qt::UserRole).toString();
    if (!url.isEmpty())
        QDesktopServices::openUrl(QUrl(url));
}

void FinGptAnalysisTab::try_assemble() {
    if (!running_ || assembled_ || !candles_done_ || !info_done_ || !news_done_)
        return;
    // The feeds stay subscribed (see unsubscribe_feeds for why this must not
    // unsubscribe from here) and re-deliver on refresh; the flag keeps one
    // run to one forecast call.
    assembled_ = true;

    if (candles_.isEmpty()) {
        // A failed SEED stays quiet: an error pane on a tab the user never
        // touched is worse than the blank one the seed exists to replace.
        if (!ask_model_) {
            fail(QStringLiteral("No price history for %1 — enter a ticker to build the "
                                "FinGPT window.").arg(run_symbol_));
            return;
        }
        result_view_->setPlainText(
            QStringLiteral("No tradable price history for %1 — the FinGPT weekly window cannot "
                           "be built. Expected for pre-IPO or untradable tickers.").arg(run_symbol_));
        // The backend is healthy (checked at start) — the headlines can still
        // be classified even when the forecast window cannot be built.
        begin_classification();
        return;
    }

    const QDate curday = core::bartime::market_today_et();
    QVector<WeekSlice> slices = make_week_slices(candles_, curday, run_weeks_);
    if (slices.isEmpty()) {
        if (!ask_model_) {
            fail(QStringLiteral("%1's history is too short for a %2-week window — lower the "
                                "lookback or pick another ticker.").arg(run_symbol_).arg(run_weeks_));
            return;
        }
        result_view_->setPlainText(
            QStringLiteral("%1's price history does not cover the requested %2-week window — "
                           "cannot assemble the FinGPT lookback.").arg(run_symbol_).arg(run_weeks_));
        begin_classification();
        return;
    }

    QVector<NewsItem> items;
    items.reserve(articles_.size());
    for (const NewsArticle& a : articles_) {
        NewsItem n;
        // published_date is an EVENT instant (ISO 8601 from yfinance).
        n.published_utc = QDateTime::fromString(a.published_date, Qt::ISODate).toUTC();
        n.headline = a.title;
        n.summary = a.description;
        if (n.published_utc.isValid() && !n.headline.isEmpty())
            items.append(n);
    }
    bucket_news(slices, items, curday);

    int weeks_with_news = 0;
    for (const WeekSlice& w : slices)
        if (!w.news.isEmpty()) ++weeks_with_news;

    const QString user_prompt =
        forecaster_user_prompt(run_symbol_, info_, slices, curday, run_basics_);
    last_run_day_ = curday;
    last_info_block_ = user_prompt;
    answer_symbol_ = run_symbol_;
    info_view_->setPlainText(user_prompt);

    if (!ask_model_) {
        // Seed/browse run: the evidence and headlines are the display; the
        // model waits for a click.
        running_ = false;
        run_btn_->setEnabled(true);
        status_lbl_->setText(
            news_error_.isEmpty()
                ? QStringLiteral("Evidence and %1 headlines ready for %2 (%3/%4 weeks have "
                                 "news) — RUN ANALYSIS to ask the model.")
                      .arg(articles_.size()).arg(run_symbol_).arg(weeks_with_news)
                      .arg(slices.size())
                : QStringLiteral("Evidence ready for %1, but the news fetch failed: %2")
                      .arg(run_symbol_, news_error_));
        return;
    }

    if (articles_.isEmpty() && !news_error_.isEmpty()) {
        // "0/N weeks have news" because the FEED broke is indistinguishable
        // from a quiet ticker in the prompt — don't spend a model call on a
        // news-driven forecast that saw none; say what actually happened.
        running_ = false;
        run_btn_->setEnabled(true);
        status_lbl_->setText(
            QStringLiteral("News fetch failed: %1 — evidence shown, the model was not asked "
                           "over an empty feed.").arg(news_error_));
        return;
    }

    QString note = QStringLiteral("Asking the model for the forecast… (%1/%2 weeks have news coverage)")
                       .arg(weeks_with_news).arg(slices.size());
    if (weeks_with_news < slices.size())
        note += QStringLiteral(" — the news feed is recent-biased; older weeks run on price alone.");
    status_lbl_->setText(note);

    forecast_made_ = true;
    run_llm(user_prompt);
}

void FinGptAnalysisTab::run_llm(const QString& user_prompt) {
    std::vector<ai_chat::ConversationMessage> history;
    history.push_back({QStringLiteral("system"), forecaster_system_prompt()});

    // Role-bound target; skip local chain-of-thought — the output is a short
    // structured one-shot (same setting as the ER AI Forecast tab).
    ai_chat::PersonaScope scope = ai_chat::LlmService::instance().scope_for_role(QStringLiteral("fingpt"));
    scope.think = false;
    // The whole pipeline samples news deterministically so a re-run over an
    // unchanged feed asks an identical question; default-temperature decoding
    // would reintroduce the rerun variance at the last stage.
    scope.temperature = 0.0;

    const quint64 epoch = epoch_;
    auto acc = std::make_shared<QString>();
    QPointer<FinGptAnalysisTab> self = this;
    ai_chat::LlmService::instance().chat_streaming(
        user_prompt, history,
        // Incremental display only — finalization happens in the per-call
        // CompletionCallback below, which is the one that carries resp.error.
        [self, acc, epoch](const QString& chunk, bool is_done) {
            if (!self || is_done) return;
            *acc += chunk;
            const QString snap = *acc;
            QMetaObject::invokeMethod(self.data(), [self, snap, epoch]() {
                if (!self || epoch != self->epoch_ || !self->running_) return;
                self->result_view_->setPlainText(strip_think(snap).trimmed());
            }, Qt::QueuedConnection);
        },
        /*use_tools=*/false, scope,
        [self, epoch](ai_chat::LlmResponse resp) {
            if (!self || epoch != self->epoch_) return;
            if (!resp.success) {
                // Show the actual failure, and do NOT chase it with a
                // per-headline chain against the same dead backend.
                self->result_view_->setPlainText(
                    QStringLiteral("The request failed: %1").arg(resp.error));
                self->fail(QStringLiteral("Model request failed — %1").arg(resp.error));
                return;
            }
            self->last_raw_answer_ = resp.content;
            self->render_answer(resp.content);
            self->begin_classification();
        });
}

void FinGptAnalysisTab::render_answer(const QString& raw) {
    const ForecasterAnswer a = parse_forecaster_answer(raw);
    const QString clean = strip_think(raw).trimmed();

    if (clean.isEmpty()) {
        result_view_->setPlainText(QStringLiteral("The model returned nothing."));
        status_lbl_->setText(QStringLiteral("Empty answer — try again or bind the FinGPT role "
                                            "to another model in Settings → AI Config."));
        return;
    }
    if (!a.sections_ok) {
        // Partial or free-form answer: show it as-is rather than pretending
        // the structure parsed.
        result_view_->setPlainText(clean);
        status_lbl_->setText(QStringLiteral("Answer did not follow the FinGPT section format — "
                                            "showing it unparsed."));
        return;
    }

    const auto esc = [](QString s) {
        // Local models emit literal markdown bold around factor names; this
        // pane renders plain text, where "**" is only clutter.
        s.remove(QStringLiteral("**"));
        return s.toHtmlEscaped().replace(QStringLiteral("\n"), QStringLiteral("<br>"));
    };
    const QString call = describe_forecast(a);
    const QDate curday =
        last_run_day_.isValid() ? last_run_day_ : core::bartime::market_today_et();
    QString html;
    html += QStringLiteral("<div style='color:%1;font-weight:700;'>[Positive Developments]</div>"
                           "<div style='color:%2;'>%3</div><br>")
                .arg(QString(colors::POSITIVE()), QString(colors::TEXT_PRIMARY()), esc(a.positives));
    html += QStringLiteral("<div style='color:%1;font-weight:700;'>[Potential Concerns]</div>"
                           "<div style='color:%2;'>%3</div><br>")
                .arg(QString(colors::NEGATIVE()), QString(colors::TEXT_PRIMARY()), esc(a.concerns));
    html += QStringLiteral("<div style='color:%1;font-weight:700;'>[Prediction &amp; Analysis]</div>")
                .arg(QString(colors::AMBER()));
    if (!call.isEmpty())
        html += QStringLiteral("<div style='color:%1;font-size:14px;font-weight:700;'>%2 "
                               "<span style='color:%3;font-size:11px;font-weight:400;'>next week "
                               "(%4 → %5)</span></div>")
                    .arg(a.direction > 0 ? QString(colors::POSITIVE()) : QString(colors::NEGATIVE()),
                         call.toHtmlEscaped(), QString(colors::TEXT_SECONDARY()),
                         curday.toString(Qt::ISODate), curday.addDays(7).toString(Qt::ISODate));
    if (!a.prediction.isEmpty())
        html += QStringLiteral("<div style='color:%1;'>Prediction: %2</div>")
                    .arg(QString(colors::TEXT_PRIMARY()), esc(a.prediction));
    if (!a.analysis.isEmpty())
        html += QStringLiteral("<div style='color:%1;'>%2</div>")
                    .arg(QString(colors::TEXT_PRIMARY()), esc(a.analysis));
    result_view_->setHtml(html);
    update_forecast_chip(call, a.direction, curday);
    status_lbl_->setText(call.isEmpty()
                             ? QStringLiteral("Forecast done — the model stated no direction.")
                             : QStringLiteral("Forecast: %1 — informational only.").arg(call));
}

void FinGptAnalysisTab::begin_classification() {
    if (articles_.isEmpty()) {
        finish_run(news_error_.isEmpty()
                       ? QStringLiteral("Done — no headlines to classify for %1.").arg(run_symbol_)
                       : QStringLiteral("News fetch failed: %1 — nothing to classify.")
                             .arg(news_error_));
        return;
    }
    // Rows were painted "—" by set_headlines for this run; just walk them.
    classify_next_row();
}

void FinGptAnalysisTab::classify_next_row() {
    int next = -1;
    for (int i = 0; i < row_labels_.size(); ++i)
        if (row_labels_[i].isEmpty()) { next = i; break; }
    if (next < 0) {
        // forecast_made_ keeps the message honest on the paths that classified
        // without a forecast (no price window for the ticker).
        finish_run(forecast_made_
                       ? QStringLiteral("Done — forecast plus %1 headlines labelled. Net score "
                                        "is (positive − negative) / total.")
                             .arg(row_labels_.size())
                       : QStringLiteral("Done — %1 headlines labelled (no forecast: no usable "
                                        "price window). Net score is (positive − negative) / "
                                        "total.").arg(row_labels_.size()));
        return;
    }
    status_lbl_->setText(QStringLiteral("Classifying headline %1 / %2…")
                             .arg(next + 1).arg(row_labels_.size()));
    // Headline plus description when present — FinGPT's FPB/NWGI inputs are
    // sentences, and a bare headline is often too terse to carry polarity.
    QString input = articles_[next].title;
    if (!articles_[next].description.isEmpty())
        input += QStringLiteral(" — ") + articles_[next].description.left(300);
    classify(sentiment_instruction_canonical(), input,
             [this, next](const QString& label, const QString& error) {
                 if (!error.isEmpty()) {
                     // Stop the whole batch: the next 15 calls will fail the
                     // same way, and the rows must stay unlabelled.
                     abort_run(QStringLiteral("Classification failed at headline %1: %2")
                                   .arg(next + 1).arg(error));
                     update_summary();
                     update_sentiment_chip();
                     return;
                 }
                 row_labels_[next] = label;
                 set_cell_label(next, label);
                 update_summary();
                 update_sentiment_chip();
                 classify_next_row();
             });
}

void FinGptAnalysisTab::finish_run(const QString& message) {
    running_ = false;
    run_btn_->setEnabled(true);
    update_summary();
    update_sentiment_chip();
    status_lbl_->setText(message);
}

void FinGptAnalysisTab::abort_run(const QString& message) {
    running_ = false;
    text_busy_ = false;
    run_btn_->setEnabled(true);
    classify_text_btn_->setEnabled(true);
    status_lbl_->setText(message);
}

void FinGptAnalysisTab::classify(const QString& instruction, const QString& input,
                                 std::function<void(QString, QString)> done) {
    std::vector<ai_chat::ConversationMessage> history;
    history.push_back({QStringLiteral("system"), sentiment_system_prompt()});

    ai_chat::PersonaScope scope = ai_chat::LlmService::instance().scope_for_role(QStringLiteral("fingpt"));
    scope.think = false;
    scope.temperature = 0.0;  // a classifier the user re-runs must not flip labels
    // think=false is a REQUEST, not a guarantee (LlmService.h: Ollama's /v1
    // shim drops it, and reasoning models spend completion tokens on the
    // trace). A one-word budget on such a backend returns finish_reason=length
    // with EMPTY content for every call — the whole tab reads as dead. Budget
    // for the trace; strip_think + substring parsing find the word in the tail.
    scope.max_tokens = 512;

    const quint64 epoch = epoch_;
    QPointer<FinGptAnalysisTab> self = this;
    ai_chat::LlmService::instance().chat_streaming(
        sentiment_frame(instruction, input), history,
        [](const QString&, bool) {},  // one-word answer — nothing to stream
        /*use_tools=*/false, scope,
        // The per-call CompletionCallback (UI thread) carries resp.error.
        // parse_sentiment_label("") would return "neutral", so a dead backend
        // must be caught HERE, before the parse — otherwise every failure is
        // painted as a confident NEUTRAL (the absent-becomes-a-value seam).
        [self, epoch, done = std::move(done)](ai_chat::LlmResponse resp) {
            if (!self || epoch != self->epoch_)
                return;
            // A leaked reasoning trace can name both labels mid-deliberation;
            // only the text after the trace is the model's answer.
            const QString content = strip_think(resp.content).trimmed();
            if (!resp.success || content.isEmpty()) {
                done(QString(), resp.error.isEmpty() ? QStringLiteral("the model returned nothing")
                                                     : resp.error);
                return;
            }
            done(parse_sentiment_label(content), QString());
        });
}

void FinGptAnalysisTab::set_cell_label(int row, const QString& label) {
    if (row < 0 || row >= table_->rowCount())
        return;
    QTableWidgetItem* item = table_->item(row, 0);
    if (!item)
        return;
    item->setText(label.toUpper());
    item->setForeground(QColor(label_color(label)));
}

void FinGptAnalysisTab::update_summary() {
    int pos = 0, neg = 0, neu = 0;
    for (const QString& l : row_labels_) {
        if (l == QStringLiteral("positive")) ++pos;
        else if (l == QStringLiteral("negative")) ++neg;
        else if (!l.isEmpty()) ++neu;
    }
    // Summary label only — the status line belongs to the caller: the abort
    // path writes its error there, and overwriting it with "Done…" made a
    // dead backend read as a completed run.
    const int n = pos + neg + neu;
    if (n == 0) {
        summary_lbl_->clear();
        return;
    }
    const double net = static_cast<double>(pos - neg) / n;
    summary_lbl_->setText(
        QStringLiteral("<span style='color:%1'>%2 positive</span> · "
                       "<span style='color:%3'>%4 negative</span> · "
                       "<span style='color:%5'>%6 neutral</span> · net score %7")
            .arg(QString(colors::POSITIVE()), QString::number(pos), QString(colors::NEGATIVE()),
                 QString::number(neg), QString(colors::TEXT_SECONDARY()), QString::number(neu),
                 QString::number(net, 'f', 2)));
}

void FinGptAnalysisTab::reset_forecast_chip() {
    forecast_chip_->setText(
        QStringLiteral("<span style='color:%1;font-weight:700;'>FORECAST</span> "
                       "<span style='color:%1;'>—</span>")
            .arg(QString(colors::TEXT_DIM())));
}

void FinGptAnalysisTab::update_forecast_chip(const QString& call, int direction,
                                             const QDate& curday) {
    if (call.isEmpty()) {
        forecast_chip_->setText(
            QStringLiteral("<span style='color:%1;font-weight:700;'>FORECAST</span> "
                           "<span style='color:%1;'>no direction stated</span>")
                .arg(QString(colors::TEXT_DIM())));
        return;
    }
    forecast_chip_->setText(
        QStringLiteral("<span style='color:%1;font-weight:700;'>FORECAST</span> "
                       "<b style='color:%2;'>%3</b> "
                       "<span style='color:%4;'>next week (%5 → %6)</span>")
            .arg(QString(colors::TEXT_DIM()),
                 direction > 0 ? QString(colors::POSITIVE()) : QString(colors::NEGATIVE()),
                 call.toHtmlEscaped(), QString(colors::TEXT_SECONDARY()),
                 curday.toString(Qt::ISODate), curday.addDays(7).toString(Qt::ISODate)));
}

void FinGptAnalysisTab::update_sentiment_chip() {
    int pos = 0, neg = 0, neu = 0;
    for (const QString& l : row_labels_) {
        if (l == QStringLiteral("positive")) ++pos;
        else if (l == QStringLiteral("negative")) ++neg;
        else if (!l.isEmpty()) ++neu;
    }
    const int n = pos + neg + neu;
    if (n == 0) {
        sentiment_chip_->setText(
            QStringLiteral("<span style='color:%1;font-weight:700;'>NEWS SENTIMENT</span> "
                           "<span style='color:%1;'>%2</span>")
                .arg(QString(colors::TEXT_DIM()),
                     row_labels_.isEmpty() ? QStringLiteral("—")
                                           : QStringLiteral("%1 headlines unclassified")
                                                 .arg(row_labels_.size())));
        return;
    }
    const double net = static_cast<double>(pos - neg) / n;
    const QString net_color = net > 0 ? colors::POSITIVE()
                              : net < 0 ? colors::NEGATIVE() : colors::TEXT_SECONDARY();
    sentiment_chip_->setText(
        QStringLiteral("<span style='color:%1;font-weight:700;'>NEWS SENTIMENT</span> "
                       "<b style='color:%2;'>net %3%4</b> "
                       "<span style='color:%5;'>(%6 pos · %7 neg · %8 neu of %9)</span>")
            .arg(QString(colors::TEXT_DIM()), net_color,
                 net > 0 ? QStringLiteral("+") : QString(), QString::number(net, 'f', 2),
                 QString(colors::TEXT_SECONDARY()), QString::number(pos), QString::number(neg),
                 QString::number(neu), QString::number(n)));
}

void FinGptAnalysisTab::on_classify_text() {
    if (running_ || text_busy_) {
        text_result_lbl_->setText(QStringLiteral("busy — a run is in flight"));
        return;
    }
    const QString input = text_edit_->toPlainText().trimmed();
    if (input.isEmpty()) {
        text_result_lbl_->setText(QStringLiteral("Nothing to classify."));
        return;
    }
    if (!ai_chat::LlmService::instance().is_configured()) {
        text_result_lbl_->setText(QStringLiteral("No model configured."));
        return;
    }
    // No epoch bump here: text_busy_ already serializes votes, and bumping
    // would strand the restore-path headline fetch seed() may have running —
    // the table would then stay empty all session with nothing to retry it.
    text_busy_ = true;
    classify_text_btn_->setEnabled(false);
    text_votes_.clear();
    // Captured once: the box stays editable while the vote runs, and
    // re-reading it per step would blend votes over two different texts
    // into one verdict.
    text_vote_input_ = input;
    if (vote_check_->isChecked()) {
        run_text_step(0);
    } else {
        text_result_lbl_->setText(QStringLiteral("Classifying…"));
        classify(sentiment_instruction_canonical(), input,
                 [this](const QString& label, const QString& error) {
                     if (!error.isEmpty()) {
                         abort_run(QStringLiteral("Classification failed: %1").arg(error));
                         text_result_lbl_->setText(QStringLiteral("—"));
                         return;
                     }
                     finish_text({label});
                 });
    }
}

void FinGptAnalysisTab::run_text_step(int step) {
    const QStringList instructions = sentiment_vote_instructions();
    if (step >= instructions.size()) {
        finish_text(text_votes_);
        return;
    }
    text_result_lbl_->setText(QStringLiteral("Vote %1 / %2…").arg(step + 1).arg(instructions.size()));
    classify(instructions[step], text_vote_input_,
             [this, step](const QString& label, const QString& error) {
                 if (!error.isEmpty()) {
                     abort_run(QStringLiteral("Vote %1 failed: %2").arg(step + 1).arg(error));
                     text_result_lbl_->setText(QStringLiteral("—"));
                     return;
                 }
                 text_votes_ << label;
                 run_text_step(step + 1);
             });
}

void FinGptAnalysisTab::finish_text(const QStringList& labels) {
    text_busy_ = false;
    classify_text_btn_->setEnabled(true);
    const QString verdict = vote_sentiment(labels);
    QString detail;
    if (labels.size() > 1) {
        int pos = 0, neg = 0;
        for (const QString& l : labels) {
            if (l == QStringLiteral("positive")) ++pos;
            else if (l == QStringLiteral("negative")) ++neg;
        }
        detail = QStringLiteral("  (votes: %1 pos / %2 neg / %3 neutral)")
                     .arg(pos).arg(neg).arg(labels.size() - pos - neg);
    }
    text_result_lbl_->setText(
        QStringLiteral("<b style='color:%1'>%2</b><span style='color:%3'>%4</span>")
            .arg(label_color(verdict), verdict.toUpper(), QString(colors::TEXT_SECONDARY()), detail));
}

QVariantMap FinGptAnalysisTab::save_state() const {
    QVariantMap m;
    m.insert(QStringLiteral("symbol"), symbol());
    m.insert(QStringLiteral("weeks"), weeks_combo_ ? weeks_combo_->currentData().toInt() : 3);
    m.insert(QStringLiteral("basics"), basics_check_ && basics_check_->isChecked());
    m.insert(QStringLiteral("last_answer"), last_raw_answer_);
    m.insert(QStringLiteral("last_info"), last_info_block_);
    m.insert(QStringLiteral("last_day"), last_run_day_.toString(Qt::ISODate));
    m.insert(QStringLiteral("answer_symbol"), answer_symbol_);
    m.insert(QStringLiteral("text"), text_edit_ ? text_edit_->toPlainText() : QString());
    m.insert(QStringLiteral("vote"), vote_check_ && vote_check_->isChecked());
    return m;
}

void FinGptAnalysisTab::restore_state(const QVariantMap& state) {
    // Every other state-mutating entry bumps the epoch; restore must too, or
    // an in-flight run's feed deliveries and LLM completion keep repainting
    // the panes the restored state just filled (pane duplication restores
    // into a live tab).
    ++epoch_;
    running_ = false;
    text_busy_ = false;
    if (run_btn_)
        run_btn_->setEnabled(true);
    if (classify_text_btn_)
        classify_text_btn_->setEnabled(true);
    const QString restored_symbol = state.value(QStringLiteral("symbol")).toString();
    if (!restored_symbol.isEmpty())
        set_symbol(restored_symbol);
    const int weeks = state.value(QStringLiteral("weeks"), 3).toInt();
    if (weeks_combo_) {
        const int idx = weeks_combo_->findData(weeks);
        if (idx >= 0) weeks_combo_->setCurrentIndex(idx);
    }
    if (basics_check_ && state.contains(QStringLiteral("basics")))
        basics_check_->setChecked(state.value(QStringLiteral("basics")).toBool());
    if (text_edit_ && state.contains(QStringLiteral("text")))
        text_edit_->setPlainText(state.value(QStringLiteral("text")).toString());
    if (vote_check_ && state.contains(QStringLiteral("vote")))
        vote_check_->setChecked(state.value(QStringLiteral("vote")).toBool());
    const QString answer = state.value(QStringLiteral("last_answer")).toString();
    const QString info = state.value(QStringLiteral("last_info")).toString();
    // The stored answer belongs to answer_symbol, not to whatever the field
    // said at save time (it can be edited mid-run). On a mismatch the stale
    // run is dropped — a blank pane the seed refills beats AAPL's forecast
    // wearing MSFT's ticker. Legacy saves carry no answer_symbol and restore
    // as before.
    const QString ans_sym = state.value(QStringLiteral("answer_symbol")).toString().toUpper();
    const QString field_sym = symbol();
    if (!ans_sym.isEmpty() && !field_sym.isEmpty() && ans_sym != field_sym)
        return;
    answer_symbol_ = ans_sym.isEmpty() ? field_sym : ans_sym;
    if (!info.isEmpty()) {
        last_info_block_ = info;
        info_view_->setPlainText(info);
    }
    if (!answer.isEmpty()) {
        last_run_day_ = QDate::fromString(state.value(QStringLiteral("last_day")).toString(), Qt::ISODate);
        last_raw_answer_ = answer;
        render_answer(answer);
        status_lbl_->setText(
            last_run_day_.isValid()
                ? QStringLiteral("Restored the %1 run — RUN ANALYSIS for a fresh read.")
                      .arg(last_run_day_.toString(Qt::ISODate))
                : QStringLiteral("Restored the previous run — RUN ANALYSIS for a fresh read."));
    }
}

} // namespace fincept::screens::fingpt
