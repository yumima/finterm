// src/screens/equity_research/EquityReportTab.cpp
#include "screens/equity_research/EquityReportTab.h"

#include "ai_chat/LlmService.h"
#include "core/logging/Logger.h"
#include "screens/equity_research/EquityPeersTab.h"
#include "services/equity/EquityResearchService.h"
#include "services/equity/ReportService.h"
#include "services/file_manager/FileManagerService.h"
#include "services/ownership/OwnershipService.h"
#include "ui/theme/Theme.h"
#include "ui/theme/ThemeManager.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QStandardPaths>
#include <QTextDocument>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>

namespace finterm::screens {

using namespace report;
namespace eq = services::equity;
using QState = services::query::QueryStore::State;

namespace {

const QString kBenchmark = QStringLiteral("SPY");
const QString kImageScheme = QStringLiteral("report");

// Content width: wide enough for two charts side by side, never so wide a
// line of text becomes hard to follow.
constexpr int kMinWidth = 760;
constexpr int kMaxWidth = 1320;

QString image_url(const QString& id) {
    return QStringLiteral("%1:/%2").arg(kImageScheme, id);
}

} // namespace

QVariant ReportView::loadResource(int type, const QUrl& name) {
    if (type == QTextDocument::ImageResource && name.scheme() == kImageScheme)
        return images.value(name.path().mid(1));
    return QTextBrowser::loadResource(type, name);
}

void ReportView::resizeEvent(QResizeEvent* e) {
    QTextBrowser::resizeEvent(e);
    if (e->size().width() != e->oldSize().width())
        emit resized();
}

EquityReportTab::EquityReportTab(QWidget* parent) : QWidget(parent) {
    parts_ = {
        {QStringLiteral("Company profile and ratios"), QStringLiteral("Yahoo Finance")},
        {QStringLiteral("Annual statements"), QStringLiteral("Yahoo Finance")},
        {QStringLiteral("Quarterly statements, rating changes"), QStringLiteral("Yahoo Finance")},
        {QStringLiteral("Earnings history and estimates"), QStringLiteral("Yahoo Finance")},
        {QStringLiteral("Peer ratios"), QStringLiteral("Yahoo Finance")},
        {QStringLiteral("Headlines"), QStringLiteral("Yahoo Finance")},
        {QStringLiteral("Technical rating"), QStringLiteral("computed from daily bars")},
        {QStringLiteral("Share price, one year"), QStringLiteral("Yahoo Finance")},
        {QStringLiteral("Benchmark, one year"), kBenchmark + QStringLiteral(" · Yahoo Finance")},
        {QStringLiteral("Annual and quarterly reports"), QStringLiteral("SEC EDGAR")},
    };
    build_ui();

    render_timer_ = new QTimer(this);
    render_timer_->setSingleShot(true);
    render_timer_->setInterval(120);
    connect(render_timer_, &QTimer::timeout, this, &EquityReportTab::render);
    connect(view_, &ReportView::resized, this, [this]() {
        if (!symbol_.isEmpty())
            schedule_render();
    });
    connect(&services::OwnershipService::instance(), &services::OwnershipService::snapshot_updated, this,
            [this](const QString& symbol) {
                if (symbol == symbol_) {
                    read_insiders();
                    schedule_render();
                }
            });
    connect(&ui::ThemeManager::instance(), &ui::ThemeManager::theme_changed, this, [this](const ui::ThemeTokens&) {
        apply_theme();
        schedule_render();
    });
    apply_theme();
}

void EquityReportTab::build_ui() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("reportBar"));
    bar->setFixedHeight(34);
    auto* bl = new QHBoxLayout(bar);
    bl->setContentsMargins(12, 0, 12, 0);
    bl->setSpacing(8);
    status_ = new QLabel(QStringLiteral("Pick a symbol to build its report."), bar);
    status_->setObjectName(QStringLiteral("reportStatus"));
    status_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    bl->addWidget(status_, 1);
    auto button = [&](const QString& text, const QString& tip) {
        auto* b = new QPushButton(text, bar);
        b->setObjectName(QStringLiteral("reportBtn"));
        b->setCursor(Qt::PointingHandCursor);
        b->setToolTip(tip);
        b->setEnabled(false);
        bl->addWidget(b);
        return b;
    };
    write_btn_ = button(QStringLiteral("REWRITE NARRATIVE"),
                        QStringLiteral("Ask the model to write the summary and business sections again"));
    pdf_btn_ = button(QStringLiteral("EXPORT PDF"), QStringLiteral("Save the report as a PDF, on white"));
    html_btn_ = button(QStringLiteral("EXPORT HTML"),
                       QStringLiteral("Save the report as one self-contained web page, charts included"));
    connect(write_btn_, &QPushButton::clicked, this, [this]() { maybe_write_narrative(true); });
    connect(pdf_btn_, &QPushButton::clicked, this, &EquityReportTab::export_pdf);
    connect(html_btn_, &QPushButton::clicked, this, &EquityReportTab::export_html);
    root->addWidget(bar);

    view_ = new ReportView(this);
    view_->setOpenExternalLinks(true);
    view_->setFrameShape(QFrame::NoFrame);
    view_->document()->setDocumentMargin(20);
    root->addWidget(view_, 1);
}

void EquityReportTab::apply_theme() {
    using namespace ui::colors;
    setStyleSheet(QStringLiteral("QWidget#reportBar{background:%1;border-bottom:1px solid %2;}"
                                 "QLabel#reportStatus{color:%3;font-size:%4px;background:transparent;}"
                                 "QPushButton#reportBtn{background:transparent;color:%5;border:1px solid %2;"
                                 "padding:4px 12px;font-size:%4px;font-weight:700;letter-spacing:1px;}"
                                 "QPushButton#reportBtn:hover{color:%6;border-color:%6;}"
                                 "QPushButton#reportBtn:disabled{color:%7;}"
                                 "QTextBrowser{background:%8;border:none;}")
                      .arg(BG_SURFACE(), BORDER_DIM(), TEXT_SECONDARY())
                      .arg(ui::fonts::font_px(-2))
                      .arg(TEXT_PRIMARY(), AMBER(), TEXT_DIM(), BG_BASE()));
}

// ── loading ────────────────────────────────────────────────────────────────

void EquityReportTab::reset(const QString& symbol) {
    symbol_ = symbol;
    ++epoch_;
    doc_ = ReportDoc{};
    doc_.symbol = symbol;
    doc_.bench_symbol = kBenchmark;
    for (auto& p : parts_) {
        p.state = Source::State::Pending;
        p.detail.clear();
    }
    narrative_started_ = false;
    narrative_pending_ = 0;
    narrative_model_.clear();
    narrative_written_at_ = {};
    narrative_problem_.clear();
    rendered_width_ = 0;
    view_->images.clear();
    view_->clear();
}

void EquityReportTab::mark(Part part, Source::State state, const QString& detail) {
    parts_[part].state = state;
    parts_[part].detail = detail;
    schedule_render();
    maybe_write_narrative();
}

bool EquityReportTab::settled(Part part) const {
    return parts_[part].state != Source::State::Pending;
}

void EquityReportTab::set_symbol(const QString& symbol) {
    const QString sym = symbol.trimmed().toUpper();
    if (sym.isEmpty())
        return;
    // Coming back to the same company keeps its report — unless part of it
    // failed, in which case returning to the tab is the retry.
    const bool failed = std::any_of(parts_.begin(), parts_.end(),
                                    [](const Source& p) { return p.state == Source::State::Failed; });
    if (sym == symbol_ && !failed)
        return;
    // Not from inside a delivery: QueryStore calls its functor without
    // copying it, so the old symbol's subscriptions are dropped here.
    services::query::QueryStore::instance().unsubscribe_all(this);
    reset(sym);
    const quint64 epoch = epoch_;
    auto& svc = eq::EquityResearchService::instance();
    const QString as_of_fmt = QStringLiteral("d MMM HH:mm");

    /// One QueryStore stream into one part of the report. An error with no
    /// data marks the part unavailable; an error after data keeps the data.
    auto bind = [this, epoch, as_of_fmt](Part part, std::function<bool(const QVariant&)> take) {
        return [this, epoch, part, as_of_fmt, take](const QState& s) {
            if (epoch != epoch_)
                return;
            if (s.data.isValid() && !s.data.isNull()) {
                // An answer that holds nothing is still an answer: the part is
                // settled as empty, or the report would wait on it for ever.
                if (take(s.data))
                    mark(part, Source::State::Ready,
                         s.fetched_at.isValid() ? QStringLiteral("as of %1").arg(s.fetched_at.toLocalTime().toString(as_of_fmt))
                                                : QString());
                else if (parts_[part].state == Source::State::Pending)
                    mark(part, part == Info ? Source::State::Failed : Source::State::None,
                         QStringLiteral("the vendor returned nothing for this symbol"));
                return;
            }
            if (!s.error.isEmpty() && parts_[part].state != Source::State::Ready)
                mark(part, Source::State::Failed,
                     eq::is_transient_error(s.error) ? QStringLiteral("refused or timed out — reopen the tab to retry")
                                                     : s.error.left(140));
        };
    };

    svc.subscribe_info(this, sym, bind(Info, [this](const QVariant& v) {
        doc_.info = v.value<eq::StockInfo>();
        doc_.has_info = doc_.info.valid;
        return doc_.has_info;
    }));
    svc.subscribe_financials(this, sym, bind(Financials, [this](const QVariant& v) {
        doc_.financials = v.value<eq::FinancialsData>();
        doc_.has_financials = true;
        return true;
    }));
    svc.subscribe_earnings_analysis(this, sym, bind(Earnings, [this](const QVariant& v) {
        doc_.earnings = v.value<eq::EarningsAnalysis>();
        doc_.has_earnings = true;
        return true;
    }));
    svc.subscribe_news(this, sym, bind(News, [this](const QVariant& v) {
        doc_.news = v.value<QVector<eq::NewsArticle>>();
        return true;
    }));
    svc.subscribe_technicals(this, sym, QStringLiteral("1y"), QStringLiteral("1d"), bind(Technicals, [this](const QVariant& v) {
        doc_.technicals = v.value<eq::TechnicalsData>();
        doc_.has_technicals = true;
        return true;
    }));
    svc.subscribe_historical(this, sym, QStringLiteral("1y"), bind(Prices, [this](const QVariant& v) {
        doc_.candles = v.value<QVector<eq::Candle>>();
        return !doc_.candles.isEmpty();
    }));
    svc.subscribe_historical(this, kBenchmark, QStringLiteral("1y"), bind(Benchmark, [this](const QVariant& v) {
        doc_.bench = v.value<QVector<eq::Candle>>();
        return !doc_.bench.isEmpty();
    }));

    // A basket of index funds is the "no basket" answer, not a peer group.
    const QStringList peers = EquityPeersTab::default_peers(sym);
    if (peers == QStringList{QStringLiteral("SPY"), QStringLiteral("QQQ"), QStringLiteral("DIA")}) {
        parts_[Peers].state = Source::State::None;
        parts_[Peers].detail = QStringLiteral("no peer basket is kept for this ticker");
    } else {
        svc.subscribe_peers(this, sym, peers, bind(Peers, [this](const QVariant& v) {
            doc_.peers = v.value<QVector<eq::PeerData>>();
            return !doc_.peers.isEmpty();
        }));
    }

    eq::fetch_report_extras(sym, this, [this, epoch](bool ok, QJsonObject o, QString err) {
        if (epoch != epoch_)
            return;
        if (!ok) {
            mark(Extras, Source::State::Failed, err);
            return;
        }
        doc_.extras = parse_extras(o);
        doc_.has_extras = true;
        mark(Extras, Source::State::Ready, doc_.extras.problems.join(QStringLiteral("; ")));
    });
    eq::fetch_report_filings(sym, this, [this, epoch](bool ok, QJsonObject o, QString err) {
        if (epoch != epoch_)
            return;
        if (!ok) {
            doc_.filings.annual_error = err;
            mark(Filings, Source::State::Failed, err);
            return;
        }
        doc_.filings = parse_filings(o);
        QStringList cites;
        for (const auto* f : {&doc_.filings.annual, &doc_.filings.quarterly})
            if (f->valid())
                cites << f->cite();
        if (!doc_.filings.annual_error.isEmpty())
            cites << QStringLiteral("annual: ") + doc_.filings.annual_error;
        mark(Filings, Source::State::Ready, cites.join(QStringLiteral(" · ")));
    });

    read_insiders();
    schedule_render();
}

void EquityReportTab::read_insiders() {
    const auto snap = services::OwnershipService::instance().snapshot(symbol_);
    InsiderFlow flow;
    if (snap.edgar_ok && snap.symbol.compare(symbol_, Qt::CaseInsensitive) == 0) {
        flow.known = true;
        flow.window_months = snap.window_months;
        for (const auto& t : snap.transactions) {
            // A decision about price: open market, common stock, not a 10%
            // holder and not a trade scheduled months before under a plan.
            if (!t.open_market || t.derivative || t.ten_percent_owner || t.plan_10b5_1.value_or(false))
                continue;
            (t.acquired ? flow.buys : flow.sells) += 1;
            (t.acquired ? flow.buy_value : flow.sell_value) += t.value.value_or(0.0);
        }
    }
    doc_.insiders = flow;
}

// ── narrative ──────────────────────────────────────────────────────────────

void EquityReportTab::run_model(const QString& prompt, int max_tokens,
                                std::function<void(bool, QString, QString)> done) {
    auto& llm = finterm::ai_chat::LlmService::instance();
    finterm::ai_chat::PersonaScope scope = llm.scope_for_role(QStringLiteral("equity_research"));
    scope.think = false;
    scope.json_object = true;
    scope.temperature = 0.0; // the same data writes the same report
    scope.max_tokens = max_tokens;
    scope.grounding_note = false; // the report's own note says figures are checked
    QPointer<EquityReportTab> self = this;
    llm.chat_streaming(
        prompt, {}, [](const QString&, bool) {}, /*use_tools=*/false, scope,
        [self, done](finterm::ai_chat::LlmResponse resp) {
            if (!self)
                return;
            if (!resp.success || resp.content.trimmed().isEmpty()) {
                done(false, resp.error.isEmpty() ? QStringLiteral("the model returned nothing") : resp.error, QString());
                return;
            }
            done(true, resp.content, resp.model_used);
        });
}

void EquityReportTab::maybe_write_narrative(bool force) {
    if (symbol_.isEmpty() || narrative_pending_ > 0 || (narrative_started_ && !force))
        return;
    // The prose is written once, from the whole picture: wait for every input
    // it reads to arrive or fail.
    for (Part p : {Info, Financials, Extras, Earnings, News, Filings, Peers})
        if (!settled(p))
            return;
    if (!doc_.has_info)
        return;
    narrative_started_ = true;
    narrative_problem_.clear();
    if (!finterm::ai_chat::LlmService::instance().is_configured()) {
        narrative_problem_ = QStringLiteral("The written sections need a language model: Settings → AI Config. The charts and "
                                            "the rule-based findings below do not.");
        schedule_render();
        return;
    }
    const QVector<Headline> heads = headlines(doc_);
    const QString name = doc_.info.company_name;
    if (force) { // a rewrite must not be answered from the kept copy
        eq::forget_narrative(QStringLiteral("summary"), symbol_);
        eq::forget_narrative(QStringLiteral("business"), symbol_);
        doc_.summary = {};
        doc_.business = {};
    }
    narrative_model_.clear();
    narrative_written_at_ = {};
    // One after the other, the short one first: a local model answers one
    // request at a time, and the second of two sent together spends its two
    // minutes waiting in the queue. The budgets leave room for a model that
    // thinks aloud before it answers.
    QVector<Section> sections;
    sections.append({QStringLiteral("summary"), QStringLiteral("the summary"),
                     summary_prompt(symbol_, name, figures_block(doc_), derive_findings(make_inputs(doc_)), heads), 3200,
                     [this](const QString& reply) {
                         const SummaryNarrative n = parse_summary(reply);
                         if (n.valid)
                             doc_.summary = n;
                         return n.valid;
                     }});
    sections.append({QStringLiteral("business"), QStringLiteral("the business section"),
                     business_prompt(symbol_, name, doc_.info.description, doc_.filings, heads), 4096,
                     [this](const QString& reply) {
                         const BusinessNarrative n = parse_business(reply);
                         if (n.valid)
                             doc_.business = n;
                         return n.valid;
                     }});
    narrative_pending_ = sections.size();
    write_sections(sections, std::make_shared<QStringList>(), epoch_);
    schedule_render();
}

void EquityReportTab::write_sections(QVector<Section> todo, std::shared_ptr<QStringList> problems, quint64 epoch) {
    if (epoch != epoch_)
        return;
    if (todo.isEmpty()) {
        if (!problems->isEmpty())
            narrative_problem_ = QStringLiteral("Written sections incomplete: %1. REWRITE NARRATIVE tries again.")
                                     .arg(problems->join(QStringLiteral("; ")));
        schedule_render();
        return;
    }
    const Section step = todo.takeFirst();
    auto done = [this, todo, problems, epoch](const QString& model, const QDateTime& written_at) {
        --narrative_pending_;
        if (!model.isEmpty())
            narrative_model_ = model;
        // The report states the OLDEST section's time: that is how old its prose is.
        if (written_at.isValid() && (!narrative_written_at_.isValid() || written_at < narrative_written_at_))
            narrative_written_at_ = written_at;
        schedule_render();
        write_sections(todo, problems, epoch);
    };
    const eq::KeptNarrative kept = eq::kept_narrative(step.key, symbol_);
    if (kept.valid() && step.apply(kept.reply)) {
        done(kept.model, kept.written_at);
        return;
    }
    run_model(step.prompt, step.max_tokens, [this, step, problems, epoch, done](bool ok, QString reply, QString model) {
        if (epoch != epoch_)
            return;
        if (ok && step.apply(reply)) {
            eq::keep_narrative(step.key, symbol_, reply, model);
            done(model, QDateTime::currentDateTime());
            return;
        }
        if (ok) // what came back, for whoever has to work out why it did not parse
            LOG_WARN("Report", QStringLiteral("unreadable reply for %1 (%2 chars): %3 … %4")
                                   .arg(step.name)
                                   .arg(reply.size())
                                   .arg(reply.left(300).simplified(), reply.right(600).simplified()));
        *problems << (ok ? QStringLiteral("the model's reply for %1 could not be read").arg(step.name)
                         : QStringLiteral("the model did not write %1 (%2)").arg(step.name, reply.left(120)));
        done(QString(), QDateTime());
    });
}

// ── rendering ──────────────────────────────────────────────────────────────

void EquityReportTab::schedule_render() {
    render_timer_->start();
}

void EquityReportTab::update_status() {
    int ready = 0, pending = 0, failed = 0;
    for (const auto& p : parts_) {
        ready += p.state == Source::State::Ready;
        pending += p.state == Source::State::Pending;
        failed += p.state == Source::State::Failed;
    }
    QString text;
    if (pending > 0)
        text = QStringLiteral("%1 — loading %2 of %3 sources…").arg(symbol_).arg(parts_.size() - pending).arg(parts_.size());
    else if (narrative_pending_ > 0)
        text = QStringLiteral("%1 — figures loaded; the model is writing the narrative…").arg(symbol_);
    else
        text = QStringLiteral("%1 — report built %2").arg(symbol_, doc_.generated.toString(QStringLiteral("d MMM yyyy HH:mm")));
    if (failed > 0)
        text += QStringLiteral(" · %1 source%2 unavailable (see Sources)").arg(failed).arg(failed == 1 ? "" : "s");
    status_->setText(text);
    const bool has_body = doc_.has_info;
    pdf_btn_->setEnabled(has_body && pending == 0);
    html_btn_->setEnabled(has_body && pending == 0);
    write_btn_->setEnabled(has_body && pending == 0 && narrative_pending_ == 0 &&
                           finterm::ai_chat::LlmService::instance().is_configured());
}

void EquityReportTab::render() {
    if (symbol_.isEmpty())
        return;
    doc_.generated = QDateTime::currentDateTime();
    doc_.sources = parts_;
    if (narrative_pending_ > 0)
        doc_.narrative_note = QStringLiteral("The model is writing this section — a local model takes a minute or two…");
    else if (!narrative_problem_.isEmpty())
        doc_.narrative_note = narrative_problem_;
    else if (doc_.summary.valid || doc_.business.valid)
        doc_.narrative_note =
            QStringLiteral("Written sections by %1%2, from the filings and the figures as they stood then.")
                .arg(narrative_model_.isEmpty() ? QStringLiteral("the configured language model") : narrative_model_,
                     narrative_written_at_.isValid()
                         ? QStringLiteral(" at %1").arg(narrative_written_at_.toString(QStringLiteral("HH:mm 'on' d MMM")))
                         : QString());
    else
        doc_.narrative_note = QStringLiteral("The written sections appear once the figures have loaded.");
    update_status();
    if (!doc_.has_info) {
        const bool failed = parts_[Info].state == Source::State::Failed;
        view_->setHtml(QStringLiteral("<p style='color:%1;font-size:%2px;margin:30px;'>%3</p>")
                           .arg(ui::colors::TEXT_TERTIARY())
                           .arg(ui::fonts::font_px(0))
                           .arg(failed ? QStringLiteral("The report for %1 could not be built: %2").arg(symbol_, parts_[Info].detail).toHtmlEscaped()
                                       : QStringLiteral("Building the report for %1…").arg(symbol_)));
        return;
    }
    const int width = std::clamp(view_->viewport()->width() - 2 * 20 - 4, kMinWidth, kMaxWidth);
    rendered_width_ = width;
    QHash<QString, QImage> images;
    const QString html = build_report_html(doc_, Palette::from_theme(), width, devicePixelRatioF(),
                                           [&images](const QString& id, const QImage& img) {
                                               images.insert(id, img);
                                               return image_url(id);
                                           });
    const int scroll = view_->verticalScrollBar()->value();
    view_->images = images;
    view_->setHtml(html);
    view_->verticalScrollBar()->setValue(scroll);
}

// ── export ─────────────────────────────────────────────────────────────────

QString EquityReportTab::export_path(const QString& extension, const QString& filter) {
    // Documents, not wherever the terminal happened to be started from.
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString name = QStringLiteral("%1_report_%2.%3")
                             .arg(symbol_, QDate::currentDate().toString(QStringLiteral("yyyyMMdd")), extension);
    return QFileDialog::getSaveFileName(this, QStringLiteral("Export the %1 report").arg(symbol_),
                                        QDir(dir).filePath(name), filter);
}

void EquityReportTab::export_pdf() {
    const QString path = export_path(QStringLiteral("pdf"), QStringLiteral("PDF (*.pdf)"));
    if (path.isEmpty())
        return;
    const int pages = write_report_pdf(doc_, path);
    status_->setText(pages > 0 ? QStringLiteral("Saved %1 (%2 pages)").arg(path).arg(pages)
                               : QStringLiteral("The PDF could not be written to %1").arg(path));
}

void EquityReportTab::export_html() {
    const QString path = export_path(QStringLiteral("html"), QStringLiteral("Web page (*.html)"));
    if (path.isEmpty())
        return;
    if (!write_report_html(doc_, path)) {
        status_->setText(QStringLiteral("The page could not be written to %1").arg(path));
        return;
    }
    status_->setText(QStringLiteral("Saved %1").arg(path));
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

} // namespace finterm::screens
