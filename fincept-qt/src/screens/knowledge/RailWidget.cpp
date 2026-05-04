#include "screens/knowledge/RailWidget.h"

#include "screens/knowledge/CalculatorPanel.h"
#include "screens/knowledge/ContentLoader.h"
#include "screens/knowledge/ExposurePanel.h"
#include "screens/knowledge/HistoryChartPanel.h"
#include "screens/knowledge/LiveDataPanel.h"
#include "screens/knowledge/PeerChartPanel.h"
#include "ui/theme/Theme.h"

#include <QDesktopServices>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QUrl>
#include <QVBoxLayout>

namespace fincept::knowledge {

namespace {

constexpr const char* MONO = "font-family: 'Consolas','Courier New',monospace;";

struct LinkTypeStyle { QString icon; QString label; };
LinkTypeStyle link_type_style(const QString& type) {
    if (type == "reference") return {QString::fromUtf8("📊"), "REFERENCE"};
    if (type == "deep_dive") return {QString::fromUtf8("🎓"), "DEEP DIVE"};
    if (type == "video")     return {QString::fromUtf8("🎥"), "VIDEO"};
    if (type == "tool")      return {QString::fromUtf8("🛠"), "TOOL"};
    return {QString::fromUtf8("📖"), "PRIMER"};
}

QString section_label_ss() {
    return QString("color: %1; font-size: 10px; font-weight: bold; letter-spacing: 1.5px;"
                   " background: transparent; padding: 8px 0 2px 0; %2")
        .arg(ui::colors::TEXT_TERTIARY(), MONO);
}
QString panel_ss() {
    return QString("background: %1; border: 1px solid %2;").arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM());
}
QString link_button_ss() {
    return QString("QPushButton { color: %1; background: transparent; border: none; text-align: left;"
                   " padding: 3px 0; font-size: 11px; %2 }"
                   "QPushButton:hover { color: %3; }")
        .arg(ui::colors::CYAN(), MONO, ui::colors::AMBER());
}
QString action_button_ss() {
    return QString("QPushButton { color: %1; background: %2; border: 1px solid %3; padding: 6px 10px;"
                   " font-size: 11px; font-weight: 500; text-align: left; %4 }"
                   "QPushButton:hover { color: %5; border-color: %5; background: %6; }")
        .arg(ui::colors::TEXT_PRIMARY(), ui::colors::BG_BASE(), ui::colors::BORDER_DIM(), MONO,
             ui::colors::AMBER(), ui::colors::ACCENT_BG());
}
QString related_chip_ss() {
    return QString("QPushButton { color: %1; background: %2; border: 1px solid %3; padding: 4px 9px;"
                   " font-size: 11px; %4 }"
                   "QPushButton:hover { color: %5; border-color: %5; background: %6; }")
        .arg(ui::colors::TEXT_PRIMARY(), ui::colors::BG_BASE(), ui::colors::BORDER_DIM(), MONO,
             ui::colors::AMBER(), ui::colors::BG_RAISED());
}
QString scroll_ss() {
    return QString("QScrollArea { border: none; background: transparent; }"
                   "QScrollBar:vertical { width: 6px; background: transparent; }"
                   "QScrollBar::handle:vertical { background: %1; }"
                   "QScrollBar::handle:vertical:hover { background: %2; }"
                   "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }")
        .arg(ui::colors::BORDER_MED(), ui::colors::BORDER_BRIGHT());
}

} // namespace

RailWidget::RailWidget(QWidget* parent) : QWidget(parent) {
    setStyleSheet(QString("background: %1;").arg(ui::colors::BG_BASE()));
    root_ = new QVBoxLayout(this);
    root_->setContentsMargins(0, 0, 0, 0);
    root_->setSpacing(0);
    rebuild();
}

void RailWidget::set_entry(const KnowledgeEntry* entry) {
    current_ = entry;
    rebuild();
}

void RailWidget::rebuild() {
    if (content_) {
        root_->removeWidget(content_);
        content_->deleteLater();
        content_ = nullptr;
    }

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setStyleSheet(scroll_ss());

    auto* inner = new QWidget(scroll);
    inner->setStyleSheet(QString("background: %1;").arg(ui::colors::BG_BASE()));
    auto* rl = new QVBoxLayout(inner);
    rl->setContentsMargins(14, 14, 14, 20);
    rl->setSpacing(6);

    if (!current_) {
        auto* lbl = new QLabel(QString("<i>Click an entry in any column to see its finterm context here.</i>"), inner);
        lbl->setStyleSheet(QString("color: %1; background: transparent; font-size: 11px; %2")
                               .arg(ui::colors::TEXT_DIM(), MONO));
        lbl->setWordWrap(true);
        rl->addWidget(lbl);
        rl->addStretch();
        scroll->setWidget(inner);
        content_ = scroll;
        root_->addWidget(content_, 1);
        return;
    }

    // Subtle context line at top so the user knows which entry the rail is for.
    {
        auto* ctx = new QLabel(QString("Context · %1").arg(current_->title), inner);
        ctx->setStyleSheet(QString("color: %1; background: transparent; font-size: 10px; font-weight: bold;"
                                   " letter-spacing: 1px; padding-bottom: 6px;"
                                   " border-bottom: 1px solid %2; margin-bottom: 4px; %3")
                               .arg(ui::colors::AMBER(), ui::colors::BORDER_DIM(), MONO));
        rl->addWidget(ctx);
    }

    auto add_panel = [&](const QString& title, QWidget* widget) {
        auto* lbl = new QLabel(title, inner);
        lbl->setStyleSheet(section_label_ss());
        rl->addWidget(lbl);
        auto* frame = new QFrame(inner);
        frame->setStyleSheet(panel_ss());
        auto* fl = new QVBoxLayout(frame);
        fl->setContentsMargins(12, 10, 12, 10);
        fl->setSpacing(6);
        fl->addWidget(widget);
        rl->addWidget(frame);
    };

    if (!current_->live_examples.isEmpty())
        add_panel("LIVE IN FINTERM", new LiveDataPanel(current_->live_examples, inner));

    // Mini history chart for the primary ticker.
    if (!current_->primary_ticker.isEmpty())
        add_panel("HISTORY · 1Y", new HistoryChartPanel(current_->primary_ticker, "1y", inner));

    // Peer comparison bar chart.
    {
        QString metric = current_->primary_metric;
        if (metric.isEmpty() && !current_->live_examples.isEmpty())
            metric = current_->live_examples.first().metric;
        if (!current_->peer_tickers.isEmpty() && !metric.isEmpty())
            add_panel("PEER COMPARISON", new PeerChartPanel(current_->peer_tickers, metric, inner));
    }

    // Rule of thumb panel — typical ranges / norms.
    if (!current_->rules_of_thumb.isEmpty()) {
        auto* container = new QWidget(inner);
        container->setStyleSheet("background: transparent;");
        auto* cl = new QVBoxLayout(container);
        cl->setContentsMargins(0, 0, 0, 0);
        cl->setSpacing(5);
        for (const auto& r : current_->rules_of_thumb) {
            auto* row = new QWidget(container);
            row->setStyleSheet("background: transparent;");
            auto* rl = new QHBoxLayout(row);
            rl->setContentsMargins(0, 2, 0, 2);
            rl->setSpacing(8);
            auto* k = new QLabel(r.label, row);
            k->setStyleSheet(QString("color: %1; background: transparent; font-size: 11px; %2")
                                 .arg(ui::colors::TEXT_SECONDARY(), MONO));
            k->setFixedWidth(140);
            rl->addWidget(k);
            auto* v = new QLabel(r.value, row);
            v->setStyleSheet(QString("color: %1; background: transparent; font-size: 12px; font-weight: bold; %2")
                                 .arg(ui::colors::AMBER(), MONO));
            rl->addWidget(v, 1);
            cl->addWidget(row);
            if (!r.note.isEmpty()) {
                auto* note = new QLabel(QString::fromUtf8("   %1").arg(r.note), container);
                note->setStyleSheet(QString("color: %1; background: transparent; font-size: 10px; %2")
                                        .arg(ui::colors::TEXT_DIM(), MONO));
                note->setWordWrap(true);
                cl->addWidget(note);
            }
        }
        add_panel("RULE OF THUMB", container);
    }

    // Common pitfalls panel — quick caution list.
    if (!current_->pitfalls.isEmpty()) {
        auto* container = new QWidget(inner);
        container->setStyleSheet("background: transparent;");
        auto* cl = new QVBoxLayout(container);
        cl->setContentsMargins(0, 0, 0, 0);
        cl->setSpacing(6);
        for (const auto& p : current_->pitfalls) {
            auto* lbl = new QLabel(QString::fromUtf8("✕  %1").arg(p.text), container);
            lbl->setStyleSheet(QString("color: %1; background: transparent; font-size: 11px; line-height: 155%%; %2")
                                   .arg(ui::colors::TEXT_PRIMARY(), MONO));
            lbl->setWordWrap(true);
            cl->addWidget(lbl);
        }
        add_panel("COMMON PITFALLS", container);
    }

    // Calculator panel — only if entry declares one.
    if (!current_->calculator.kind.isEmpty())
        add_panel("CALCULATOR", new CalculatorPanel(current_->calculator, inner));

    // Portfolio exposure — only if criterion or explicit tickers are set.
    if (!current_->exposure_criterion.isEmpty() || !current_->exposure_tickers.isEmpty())
        add_panel("YOUR PORTFOLIO", new ExposurePanel(*current_, inner));

    if (!current_->actions.isEmpty()) {
        auto* container = new QWidget(inner);
        container->setStyleSheet("background: transparent;");
        auto* cl = new QVBoxLayout(container);
        cl->setContentsMargins(0, 0, 0, 0);
        cl->setSpacing(5);
        for (const auto& a : current_->actions) {
            auto* btn = new QPushButton(a.label, container);
            btn->setCursor(Qt::PointingHandCursor);
            btn->setStyleSheet(action_button_ss());
            if (!a.hint.isEmpty()) btn->setToolTip(a.hint);
            const QString screen = a.screen;
            const QString ticker = a.ticker;
            connect(btn, &QPushButton::clicked, this,
                    [this, screen, ticker]() { emit request_action(screen, ticker); });
            cl->addWidget(btn);
        }
        add_panel("TRY IN FINTERM", container);
    }

    if (!current_->seen_in.isEmpty()) {
        auto* container = new QWidget(inner);
        container->setStyleSheet("background: transparent;");
        auto* cl = new QVBoxLayout(container);
        cl->setContentsMargins(0, 0, 0, 0);
        cl->setSpacing(2);
        for (const auto& r : current_->seen_in) {
            auto* btn = new QPushButton(QString("→  %1").arg(r.label.isEmpty() ? r.screen : r.label), container);
            btn->setCursor(Qt::PointingHandCursor);
            btn->setStyleSheet(link_button_ss());
            const QString screen = r.screen;
            connect(btn, &QPushButton::clicked, this,
                    [this, screen]() { emit request_action(screen, QString{}); });
            cl->addWidget(btn);
        }
        add_panel("WHERE IN FINTERM", container);
    }

    if (!current_->related.isEmpty()) {
        auto* container = new QWidget(inner);
        container->setStyleSheet("background: transparent;");
        auto* fl = new QHBoxLayout(container);
        fl->setContentsMargins(0, 0, 0, 0);
        fl->setSpacing(6);
        for (const auto& rid : current_->related) {
            const auto* re = ContentLoader::instance().entry(rid);
            const QString label = re ? re->title : rid;
            auto* btn = new QPushButton(label, container);
            btn->setCursor(Qt::PointingHandCursor);
            btn->setStyleSheet(related_chip_ss());
            const QString id = rid;
            connect(btn, &QPushButton::clicked, this, [this, id]() { emit open_entry(id); });
            fl->addWidget(btn);
        }
        fl->addStretch();
        add_panel("RELATED", container);
    }

    if (!current_->external_links.isEmpty()) {
        auto* container = new QWidget(inner);
        container->setStyleSheet("background: transparent;");
        auto* cl = new QVBoxLayout(container);
        cl->setContentsMargins(0, 0, 0, 0);
        cl->setSpacing(4);
        for (const auto& link : current_->external_links) {
            const auto ts = link_type_style(link.type);
            auto* row = new QWidget(container);
            row->setStyleSheet("background: transparent;");
            auto* rrl = new QHBoxLayout(row);
            rrl->setContentsMargins(0, 2, 0, 2);
            rrl->setSpacing(8);

            auto* icon = new QLabel(ts.icon, row);
            icon->setFixedWidth(18);
            icon->setStyleSheet("background: transparent; font-size: 12px;");
            rrl->addWidget(icon);

            auto* type_lbl = new QLabel(ts.label, row);
            type_lbl->setFixedWidth(74);
            type_lbl->setStyleSheet(QString("color: %1; font-size: 9px; font-weight: bold; letter-spacing: 1px;"
                                            " background: transparent; %2")
                                        .arg(ui::colors::TEXT_TERTIARY(), MONO));
            rrl->addWidget(type_lbl);

            auto* btn = new QPushButton(QString("%1 · %2").arg(link.source, link.title), row);
            btn->setCursor(Qt::PointingHandCursor);
            btn->setStyleSheet(link_button_ss());
            btn->setToolTip(link.url);
            const QString url = link.url;
            connect(btn, &QPushButton::clicked, btn, [url]() { QDesktopServices::openUrl(QUrl(url)); });
            rrl->addWidget(btn, 1);
            cl->addWidget(row);
        }
        add_panel("FURTHER READING", container);
    }

    rl->addStretch();
    scroll->setWidget(inner);
    content_ = scroll;
    root_->addWidget(content_, 1);
}

} // namespace fincept::knowledge
