// src/screens/fingpt/FinGptForecasterTab.cpp
#include "screens/fingpt/FinGptForecasterTab.h"

#include "ai_chat/LlmService.h"
#include "core/util/BarTime.h"
#include "screens/fingpt/FinGptParse.h"
#include "screens/fingpt/FinGptPrompts.h"
#include "services/equity/EquityResearchService.h"
#include "services/query/QueryStore.h"
#include "ui/theme/Theme.h"
#include "ui/theme/ThemeManager.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QPointer>
#include <QSplitter>
#include <QVBoxLayout>

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
} // namespace

FinGptForecasterTab::FinGptForecasterTab(QWidget* parent) : QWidget(parent) {
    build_ui();
}

void FinGptForecasterTab::build_ui() {
    setObjectName("fingptForecaster");
    setStyleSheet(QStringLiteral(
        "#fingptForecaster { background:%1; }"
        "QLabel { color:%2; font-size:11px; background:transparent; }"
        "QLineEdit, QComboBox { background:%3; color:%4; border:1px solid %5; padding:4px 6px; "
        "  font-size:12px; }"
        "QCheckBox { color:%2; font-size:11px; }"
        "QPushButton#runBtn { background:%6; color:%7; border:none; padding:5px 16px; "
        "  font-size:11px; font-weight:700; letter-spacing:0.5px; }"
        "QPushButton#runBtn:disabled { background:%5; color:%2; }"
        "QTextEdit { background:%3; color:%4; border:1px solid %5; font-size:12px; }")
        .arg(colors::BG_BASE(), colors::TEXT_SECONDARY(), colors::BG_SURFACE(),
             colors::TEXT_PRIMARY(), colors::BORDER_DIM(), colors::AMBER(),
             colors::TEXT_ON_ACCENT()));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 8, 10, 8);
    root->setSpacing(8);

    auto* controls = new QHBoxLayout();
    controls->setSpacing(8);
    controls->addWidget(new QLabel(QStringLiteral("Ticker"), this));
    symbol_edit_ = new QLineEdit(this);
    // "e.g." matters: a bare "AAPL" placeholder reads as a filled field
    // sitting next to a button that then insists a ticker is missing.
    symbol_edit_->setPlaceholderText(QStringLiteral("e.g. AAPL"));
    symbol_edit_->setMaximumWidth(110);
    connect(symbol_edit_, &QLineEdit::returnPressed, this, &FinGptForecasterTab::on_run);
    controls->addWidget(symbol_edit_);

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

    run_btn_ = new QPushButton(QStringLiteral("RUN FORECASTER"), this);
    run_btn_->setObjectName("runBtn");
    run_btn_->setCursor(Qt::PointingHandCursor);
    connect(run_btn_, &QPushButton::clicked, this, &FinGptForecasterTab::on_run);
    controls->addWidget(run_btn_);

    status_lbl_ = new QLabel(QStringLiteral("Weekly news-driven analysis in the FinGPT-Forecaster format."), this);
    controls->addWidget(status_lbl_, 1);
    root->addLayout(controls);

    auto* split = new QSplitter(Qt::Horizontal, this);
    info_view_ = new QTextEdit(split);
    info_view_->setReadOnly(true);
    info_view_->setFontFamily(QStringLiteral("Consolas"));
    info_view_->setPlaceholderText(QStringLiteral(
        "The assembled evidence lands here: company introduction, each week's price move with "
        "the news that accompanied it, and current fundamentals — exactly what the model is "
        "shown, nothing more."));
    result_view_ = new QTextEdit(split);
    result_view_->setReadOnly(true);
    result_view_->setPlaceholderText(QStringLiteral(
        "The model's answer lands here, in FinGPT's structure:\n\n[Positive Developments]\n"
        "[Potential Concerns]\n[Prediction & Analysis]"));
    split->setSizes({420, 560});
    split->setCollapsible(0, false);
    split->setCollapsible(1, false);
    root->addWidget(split, 1);

    auto* disclaimer = new QLabel(
        QStringLiteral("FinGPT-style analysis (AI4Finance). The prediction is a structured reading of the "
                       "window's news, not advice — which news made the window changes the answer."),
        this);
    disclaimer->setWordWrap(true);
    disclaimer->setStyleSheet(QStringLiteral("color:%1; font-size:10px;").arg(colors::TEXT_DIM()));
    root->addWidget(disclaimer);
}

QString FinGptForecasterTab::symbol() const {
    return symbol_edit_ ? symbol_edit_->text().trimmed().toUpper() : QString();
}

void FinGptForecasterTab::set_symbol(const QString& symbol) {
    if (symbol_edit_ && symbol_edit_->text().trimmed().toUpper() != symbol.trimmed().toUpper())
        symbol_edit_->setText(symbol.trimmed().toUpper());
}

void FinGptForecasterTab::unsubscribe_feeds() {
    // Blanket by owner: these three feeds are this tab's only subscriptions,
    // and hand-built key strings quietly stop matching when the service bumps
    // a cache version segment (the info key already carries ":v3:").
    //
    // Called from on_run() only — NEVER from inside a feed callback.
    // QueryStore's warm-cache path delivers synchronously from subscribe()
    // without copying the functor, so a self-unsubscribe mid-delivery would
    // destroy the std::function while it executes. Between runs the feeds
    // simply stay subscribed (same lifecycle as the ER tabs); destruction is
    // covered by QueryStore's owner hook.
    QueryStore::instance().unsubscribe_all(this);
}

void FinGptForecasterTab::fail(const QString& message) {
    running_ = false;
    run_btn_->setEnabled(true);
    status_lbl_->setText(message);
}

void FinGptForecasterTab::on_run() {
    if (running_)
        return;
    const QString sym = symbol();
    if (sym.isEmpty()) {
        status_lbl_->setText(QStringLiteral("Enter a ticker first."));
        return;
    }
    if (!ai_chat::LlmService::instance().is_configured()) {
        status_lbl_->setText(QStringLiteral(
            "No model configured — open Settings → AI Config and point finterm at a model."));
        return;
    }

    ++epoch_;
    const quint64 epoch = epoch_;
    running_ = true;
    run_btn_->setEnabled(false);
    run_symbol_ = sym;
    run_weeks_ = weeks_combo_->currentData().toInt();
    run_basics_ = basics_check_->isChecked();
    candles_done_ = info_done_ = news_done_ = false;
    assembled_ = false;
    candles_.clear();
    info_ = StockInfo{};
    articles_.clear();
    result_view_->clear();
    info_view_->clear();
    status_lbl_->setText(QStringLiteral("Gathering prices, profile and news for %1…").arg(sym));

    unsubscribe_feeds();
    auto& svc = EquityResearchService::instance();

    svc.subscribe_historical(this, sym, QString::fromLatin1(kHistoryPeriod),
                             [this, epoch, sym](const QueryStore::State& s) {
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

    svc.subscribe_news(this, sym, [this, epoch](const QueryStore::State& s) {
        if (epoch != epoch_) return;
        const auto arts = s.data.value<QVector<NewsArticle>>();
        if (!arts.isEmpty()) {
            articles_ = arts;
            news_done_ = true;
            try_assemble();
        } else if (!s.loading) {
            news_done_ = true;  // weeks will carry "No relative news reported."
            try_assemble();
        }
    });
}

void FinGptForecasterTab::try_assemble() {
    if (!running_ || assembled_ || !candles_done_ || !info_done_ || !news_done_)
        return;
    // The feeds stay subscribed (see unsubscribe_feeds for why this must not
    // unsubscribe from here) and re-deliver on refresh; the flag keeps one
    // run to one LLM call.
    assembled_ = true;

    if (candles_.isEmpty()) {
        result_view_->setPlainText(
            QStringLiteral("No tradable price history for %1 — the FinGPT weekly window cannot "
                           "be built. Expected for pre-IPO or untradable tickers.").arg(run_symbol_));
        fail(QStringLiteral("No price history for %1.").arg(run_symbol_));
        return;
    }

    const QDate curday = core::bartime::market_today_et();
    QVector<WeekSlice> slices = make_week_slices(candles_, curday, run_weeks_);
    if (slices.isEmpty()) {
        result_view_->setPlainText(
            QStringLiteral("%1's price history does not cover the requested %2-week window — "
                           "cannot assemble the FinGPT lookback.").arg(run_symbol_).arg(run_weeks_));
        fail(QStringLiteral("Not enough history for a %1-week window.").arg(run_weeks_));
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
    last_info_block_ = user_prompt;
    info_view_->setPlainText(user_prompt);

    QString note = QStringLiteral("Asking the model… (%1/%2 weeks have news coverage)")
                       .arg(weeks_with_news).arg(slices.size());
    if (weeks_with_news < slices.size())
        note += QStringLiteral(" — the news feed is recent-biased; older weeks run on price alone.");
    status_lbl_->setText(note);

    run_llm(user_prompt);
}

void FinGptForecasterTab::run_llm(const QString& user_prompt) {
    std::vector<ai_chat::ConversationMessage> history;
    history.push_back({QStringLiteral("system"), forecaster_system_prompt()});

    // Role-bound target; skip local chain-of-thought — the output is a short
    // structured one-shot (same setting as the ER AI Forecast tab).
    ai_chat::PersonaScope scope = ai_chat::LlmService::instance().scope_for_role(QStringLiteral("fingpt"));
    scope.think = false;

    const quint64 epoch = epoch_;
    auto acc = std::make_shared<QString>();
    QPointer<FinGptForecasterTab> self = this;
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
            self->running_ = false;
            self->run_btn_->setEnabled(true);
            if (!resp.success) {
                // Show the actual failure. "Empty answer — rebind the model"
                // over an HTTP 401 sends the user to the wrong fix.
                self->result_view_->setPlainText(
                    QStringLiteral("The request failed: %1").arg(resp.error));
                self->status_lbl_->setText(QStringLiteral("Model request failed — %1").arg(resp.error));
                return;
            }
            self->last_raw_answer_ = resp.content;
            self->render_answer(resp.content);
        });
}

void FinGptForecasterTab::render_answer(const QString& raw) {
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
    const QDate curday = core::bartime::market_today_et();
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
    status_lbl_->setText(call.isEmpty()
                             ? QStringLiteral("Done — the model stated no direction.")
                             : QStringLiteral("Done. The model's call: %1 — informational only.").arg(call));
}

QVariantMap FinGptForecasterTab::save_state() const {
    QVariantMap m;
    m.insert(QStringLiteral("symbol"), symbol());
    m.insert(QStringLiteral("weeks"), weeks_combo_ ? weeks_combo_->currentData().toInt() : 3);
    m.insert(QStringLiteral("basics"), basics_check_ && basics_check_->isChecked());
    m.insert(QStringLiteral("last_answer"), last_raw_answer_);
    m.insert(QStringLiteral("last_info"), last_info_block_);
    return m;
}

void FinGptForecasterTab::restore_state(const QVariantMap& state) {
    set_symbol(state.value(QStringLiteral("symbol")).toString());
    const int weeks = state.value(QStringLiteral("weeks"), 3).toInt();
    if (weeks_combo_) {
        const int idx = weeks_combo_->findData(weeks);
        if (idx >= 0) weeks_combo_->setCurrentIndex(idx);
    }
    if (basics_check_ && state.contains(QStringLiteral("basics")))
        basics_check_->setChecked(state.value(QStringLiteral("basics")).toBool());
    const QString answer = state.value(QStringLiteral("last_answer")).toString();
    const QString info = state.value(QStringLiteral("last_info")).toString();
    if (!info.isEmpty()) {
        last_info_block_ = info;
        info_view_->setPlainText(info);
    }
    if (!answer.isEmpty()) {
        last_raw_answer_ = answer;
        render_answer(answer);
        status_lbl_->setText(QStringLiteral("Restored the previous run — RUN FORECASTER for a fresh read."));
    }
}

} // namespace fincept::screens::fingpt
