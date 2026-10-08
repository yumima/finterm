// src/screens/fingpt/FinGptSentimentTab.cpp
#include "screens/fingpt/FinGptSentimentTab.h"

#include "ai_chat/LlmService.h"
#include "core/util/BarTime.h"
#include "screens/fingpt/FinGptParse.h"
#include "screens/fingpt/FinGptPrompts.h"
#include "services/equity/EquityResearchService.h"
#include "services/query/QueryStore.h"
#include "ui/theme/Theme.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QPointer>
#include <QVBoxLayout>

#include <memory>

namespace fincept::screens::fingpt {

namespace colors = fincept::ui::colors;
using services::equity::EquityResearchService;
using services::equity::NewsArticle;
using services::query::QueryStore;

namespace {

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

} // namespace

FinGptSentimentTab::FinGptSentimentTab(QWidget* parent) : QWidget(parent) {
    build_ui();
}

void FinGptSentimentTab::build_ui() {
    setObjectName("fingptSentiment");
    setStyleSheet(QStringLiteral(
        "#fingptSentiment { background:%1; }"
        "QLabel { color:%2; font-size:11px; background:transparent; }"
        "QLineEdit, QPlainTextEdit { background:%3; color:%4; border:1px solid %5; "
        "  padding:4px 6px; font-size:12px; }"
        "QCheckBox { color:%2; font-size:11px; }"
        "QPushButton { background:%3; color:%4; border:1px solid %5; padding:5px 12px; "
        "  font-size:11px; font-weight:700; letter-spacing:0.5px; }"
        "QPushButton:hover { border-color:%6; color:%6; }"
        "QPushButton:disabled { color:%2; }"
        "QTableWidget { background:%3; color:%4; border:1px solid %5; font-size:12px; "
        "  gridline-color:transparent; }"
        "QTableWidget::item { padding:3px 6px; border-bottom:1px solid %5; }"
        "QHeaderView::section { background:%3; color:%4; border:none; border-bottom:2px solid %6; "
        "  padding:4px 6px; font-weight:700; }")
        .arg(colors::BG_BASE(), colors::TEXT_SECONDARY(), colors::BG_SURFACE(),
             colors::TEXT_PRIMARY(), colors::BORDER_DIM(), colors::AMBER()));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 8, 10, 8);
    root->setSpacing(8);

    // ── Free text row ────────────────────────────────────────────────────
    auto* text_row = new QHBoxLayout();
    text_row->setSpacing(8);
    text_edit_ = new QPlainTextEdit(this);
    text_edit_->setPlaceholderText(QStringLiteral(
        "Paste a headline, tweet or sentence to classify: positive / negative / neutral…"));
    text_edit_->setMaximumHeight(58);
    text_row->addWidget(text_edit_, 1);
    auto* text_side = new QVBoxLayout();
    classify_text_btn_ = new QPushButton(QStringLiteral("CLASSIFY TEXT"), this);
    classify_text_btn_->setCursor(Qt::PointingHandCursor);
    connect(classify_text_btn_, &QPushButton::clicked, this, &FinGptSentimentTab::on_classify_text);
    text_side->addWidget(classify_text_btn_);
    vote_check_ = new QCheckBox(QStringLiteral("5-template vote"), this);
    vote_check_->setChecked(true);
    vote_check_->setToolTip(QStringLiteral(
        "FinGPT's multi-template majority vote: the text is classified under five "
        "differently-worded instructions and the labels vote (5 calls instead of 1)."));
    text_side->addWidget(vote_check_);
    text_result_lbl_ = new QLabel(QStringLiteral("—"), this);
    text_side->addWidget(text_result_lbl_);
    text_side->addStretch(1);
    text_row->addLayout(text_side);
    root->addLayout(text_row);

    // ── Headline batch controls ──────────────────────────────────────────
    auto* batch_row = new QHBoxLayout();
    batch_row->setSpacing(8);
    batch_row->addWidget(new QLabel(QStringLiteral("Ticker"), this));
    symbol_edit_ = new QLineEdit(this);
    symbol_edit_->setPlaceholderText(QStringLiteral("e.g. AAPL"));
    symbol_edit_->setMaximumWidth(110);
    connect(symbol_edit_, &QLineEdit::returnPressed, this, &FinGptSentimentTab::on_fetch_headlines);
    batch_row->addWidget(symbol_edit_);
    fetch_btn_ = new QPushButton(QStringLiteral("FETCH HEADLINES"), this);
    fetch_btn_->setCursor(Qt::PointingHandCursor);
    connect(fetch_btn_, &QPushButton::clicked, this, &FinGptSentimentTab::on_fetch_headlines);
    batch_row->addWidget(fetch_btn_);
    classify_all_btn_ = new QPushButton(QStringLiteral("CLASSIFY ALL"), this);
    classify_all_btn_->setCursor(Qt::PointingHandCursor);
    classify_all_btn_->setEnabled(false);
    connect(classify_all_btn_, &QPushButton::clicked, this, &FinGptSentimentTab::on_classify_all);
    batch_row->addWidget(classify_all_btn_);
    status_lbl_ = new QLabel(
        QStringLiteral("FinGPT sentiment: {negative / neutral / positive}, one call per headline."), this);
    batch_row->addWidget(status_lbl_, 1);
    root->addLayout(batch_row);

    // ── Table ────────────────────────────────────────────────────────────
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
    root->addWidget(table_, 1);

    summary_lbl_ = new QLabel(QString(), this);
    root->addWidget(summary_lbl_);
}

QString FinGptSentimentTab::symbol() const {
    return symbol_edit_ ? symbol_edit_->text().trimmed().toUpper() : QString();
}

void FinGptSentimentTab::set_symbol(const QString& symbol) {
    if (symbol_edit_ && symbol_edit_->text().trimmed().toUpper() != symbol.trimmed().toUpper())
        symbol_edit_->setText(symbol.trimmed().toUpper());
}

void FinGptSentimentTab::on_fetch_headlines() {
    const QString sym = symbol();
    if (sym.isEmpty()) {
        status_lbl_->setText(QStringLiteral("Enter a ticker first."));
        return;
    }
    ++epoch_;  // strands any running classification chain
    if (busy_) {
        // A stranded chain never reaches its re-enable path — restore EVERY
        // control here (fetch can be reached by returnPressed while a batch
        // has the buttons disabled, and a missed one stays dead all session).
        busy_ = false;
        fetch_btn_->setEnabled(true);
        classify_text_btn_->setEnabled(true);
        text_result_lbl_->setText(QStringLiteral("—"));
    }
    const quint64 epoch = epoch_;
    status_lbl_->setText(QStringLiteral("Fetching headlines for %1…").arg(sym));
    classify_all_btn_->setEnabled(false);

    // Drop any previous symbol's subscription HERE, never inside the callback:
    // QueryStore's warm-cache path delivers synchronously from subscribe() and
    // invokes the subscriber's std::function without copying it first, so a
    // self-unsubscribe mid-delivery destroys the functor while it is running.
    // Unsubscribing at the start of the NEXT fetch (and on destruction, via
    // QueryStore's owner hook) covers the abandoned-symbol leak; news is this
    // tab's only subscription, so the blanket form is exact.
    QueryStore::instance().unsubscribe_all(this);
    // One table fill per fetch: the subscription stays live between fetches
    // and QueryStore re-delivers on its refresh cadence — re-running
    // set_headlines() then would wipe labels mid- or post-classification.
    auto handled = std::make_shared<bool>(false);
    EquityResearchService::instance().subscribe_news(this, sym,
                                                     [this, epoch, handled](const QueryStore::State& s) {
        if (epoch != epoch_ || *handled) return;
        const auto arts = s.data.value<QVector<NewsArticle>>();
        if (!arts.isEmpty()) {
            *handled = true;
            set_headlines(arts);
        } else if (!s.loading) {
            *handled = true;
            set_headlines({});
            status_lbl_->setText(s.error.isEmpty()
                                     ? QStringLiteral("No headlines available for this ticker.")
                                     : QStringLiteral("News fetch failed: %1").arg(s.error));
        }
    });
}

void FinGptSentimentTab::set_headlines(const QVector<NewsArticle>& articles) {
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
        head->setToolTip(articles_[i].description);
        table_->setItem(i, 2, head);
    }
    summary_lbl_->clear();
    classify_all_btn_->setEnabled(!articles_.isEmpty());
    if (!articles_.isEmpty())
        status_lbl_->setText(QStringLiteral("%1 headlines — CLASSIFY ALL to label them.")
                                 .arg(articles_.size()));
}

void FinGptSentimentTab::classify(const QString& instruction, const QString& input,
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
    QPointer<FinGptSentimentTab> self = this;
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

void FinGptSentimentTab::abort_run(const QString& message) {
    busy_ = false;
    fetch_btn_->setEnabled(true);
    classify_all_btn_->setEnabled(!articles_.isEmpty());
    classify_text_btn_->setEnabled(true);
    status_lbl_->setText(message);
}

void FinGptSentimentTab::on_classify_all() {
    if (busy_ || articles_.isEmpty())
        return;
    if (!ai_chat::LlmService::instance().is_configured()) {
        status_lbl_->setText(QStringLiteral(
            "No model configured — open Settings → AI Config and point finterm at a model."));
        return;
    }
    ++epoch_;
    busy_ = true;
    classify_all_btn_->setEnabled(false);
    fetch_btn_->setEnabled(false);
    for (int i = 0; i < row_labels_.size(); ++i) {
        row_labels_[i].clear();
        // The painted cells must match the state: after an abort, a previous
        // run's confident labels over unclassified rows would lie.
        if (QTableWidgetItem* item = table_->item(i, 0)) {
            item->setText(QStringLiteral("—"));
            item->setForeground(QColor(colors::TEXT_DIM()));
        }
    }
    summary_lbl_->clear();
    classify_next_row();
}

void FinGptSentimentTab::classify_next_row() {
    int next = -1;
    for (int i = 0; i < row_labels_.size(); ++i)
        if (row_labels_[i].isEmpty()) { next = i; break; }
    if (next < 0) {
        busy_ = false;
        classify_all_btn_->setEnabled(true);
        fetch_btn_->setEnabled(true);
        update_summary();
        const int n = static_cast<int>(row_labels_.size());
        status_lbl_->setText(QStringLiteral("Done — %1 headlines labelled. Net score is "
                                            "(positive − negative) / total.").arg(n));
        return;
    }
    status_lbl_->setText(QStringLiteral("Classifying %1 / %2…").arg(next + 1).arg(row_labels_.size()));
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
                     return;
                 }
                 row_labels_[next] = label;
                 set_cell_label(next, label);
                 classify_next_row();
             });
}

void FinGptSentimentTab::set_cell_label(int row, const QString& label) {
    if (row < 0 || row >= table_->rowCount())
        return;
    QTableWidgetItem* item = table_->item(row, 0);
    if (!item)
        return;
    item->setText(label.toUpper());
    item->setForeground(QColor(label_color(label)));
}

void FinGptSentimentTab::update_summary() {
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

void FinGptSentimentTab::on_classify_text() {
    if (busy_)
        return;
    const QString input = text_edit_->toPlainText().trimmed();
    if (input.isEmpty()) {
        text_result_lbl_->setText(QStringLiteral("Nothing to classify."));
        return;
    }
    if (!ai_chat::LlmService::instance().is_configured()) {
        text_result_lbl_->setText(QStringLiteral("No model configured."));
        return;
    }
    ++epoch_;
    busy_ = true;
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

void FinGptSentimentTab::run_text_step(int step) {
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

void FinGptSentimentTab::finish_text(const QStringList& labels) {
    busy_ = false;
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

QVariantMap FinGptSentimentTab::save_state() const {
    QVariantMap m;
    m.insert(QStringLiteral("symbol"), symbol());
    m.insert(QStringLiteral("text"), text_edit_ ? text_edit_->toPlainText() : QString());
    m.insert(QStringLiteral("vote"), vote_check_ && vote_check_->isChecked());
    return m;
}

void FinGptSentimentTab::restore_state(const QVariantMap& state) {
    set_symbol(state.value(QStringLiteral("symbol")).toString());
    if (text_edit_)
        text_edit_->setPlainText(state.value(QStringLiteral("text")).toString());
    if (vote_check_ && state.contains(QStringLiteral("vote")))
        vote_check_->setChecked(state.value(QStringLiteral("vote")).toBool());
}

} // namespace fincept::screens::fingpt
