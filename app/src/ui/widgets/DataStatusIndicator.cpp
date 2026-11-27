#include "ui/widgets/DataStatusIndicator.h"

#include "datahub/DataHub.h"
#include "services/data_health/DataHealth.h"
#include "ui/theme/Theme.h"
#include "ui/theme/ThemeManager.h"

#include <QApplication>
#include <QClipboard>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

namespace finterm::ui {

namespace {
constexpr int kPanelW = 460;
constexpr int kPanelMaxH = 520;

QLabel* ds_text(const QString& s, const QString& color, int px, bool bold = false) {
    auto* l = new QLabel(s);
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->setStyleSheet(QStringLiteral("color:%1; font-size:%2px; %3 background:transparent; border:none;")
                         .arg(color)
                         .arg(px)
                         .arg(bold ? QStringLiteral("font-weight:700;") : QString()));
    return l;
}

QPushButton* ds_link(const QString& label) {
    auto* b = new QPushButton(label);
    b->setFlat(true);
    b->setCursor(Qt::PointingHandCursor);
    b->setStyleSheet(QStringLiteral("QPushButton { color:%1; background:transparent; border:1px solid %2;"
                                    " padding:2px 10px; font-size:11px; }"
                                    "QPushButton:hover { border-color:%1; }")
                         .arg(colors::AMBER(), colors::BORDER_MED()));
    return b;
}
} // namespace

DataStatusIndicator::DataStatusIndicator(QWidget* parent) : QPushButton(parent) {
    setObjectName(QStringLiteral("sbData"));
    setFlat(true);
    setCursor(Qt::PointingHandCursor);
    auto& health = services::DataHealth::instance();
    connect(&health, &services::DataHealth::changed, this, &DataStatusIndicator::refresh);
    connect(this, &QPushButton::clicked, this, &DataStatusIndicator::toggle_panel);
    connect(&ThemeManager::instance(), &ThemeManager::theme_changed, this,
            [this](const ThemeTokens&) { refresh_theme(); });
    refresh();
}

void DataStatusIndicator::refresh() {
    const auto problems = services::DataHealth::instance().problems();
    if (problems.isEmpty()) {
        setText(QStringLiteral("READY"));
        setToolTip(QStringLiteral("Data is loading normally."));
    } else {
        const auto& top = problems.first().failure;
        const QString src = top.source.isEmpty() ? QStringLiteral("data") : top.source;
        QString label = QStringLiteral("⚠ DATA: %1 %2").arg(src, data_failure::kind_label(top.kind));
        if (problems.size() > 1)
            label += QStringLiteral(" +%1").arg(problems.size() - 1);
        setText(label);
        setToolTip(top.reason + QStringLiteral("\nClick for details and what to do."));
    }
    refresh_theme();
    if (panel_ && panel_->isVisible())
        fill_panel();
}

void DataStatusIndicator::refresh_theme() {
    const bool trouble = services::DataHealth::instance().any();
    setStyleSheet(QStringLiteral("QPushButton#sbData { color:%1; font-weight:700; background:transparent; border:none;"
                                 " padding:0 6px; }"
                                 "QPushButton#sbData:hover { text-decoration:underline; }")
                      .arg(trouble ? colors::WARNING() : colors::POSITIVE()));
}

void DataStatusIndicator::toggle_panel() {
    if (!panel_) {
        panel_ = new QFrame(window(), Qt::Popup | Qt::FramelessWindowHint);
        panel_->setObjectName(QStringLiteral("dataPanel"));
        panel_->setFixedWidth(kPanelW);
        panel_->setStyleSheet(QStringLiteral("QFrame#dataPanel { background:%1; border:1px solid %2; }")
                                  .arg(colors::BG_SURFACE(), colors::BORDER_MED()));
        auto* outer = new QVBoxLayout(panel_);
        outer->setContentsMargins(0, 0, 0, 0);
        auto* scroll = new QScrollArea(panel_);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setStyleSheet(QStringLiteral("QScrollArea { background:transparent; }"));
        auto* body = new QWidget;
        body->setStyleSheet(QStringLiteral("background:transparent;"));
        panel_list_ = new QVBoxLayout(body);
        panel_list_->setContentsMargins(14, 12, 14, 12);
        panel_list_->setSpacing(8);
        scroll->setWidget(body);
        outer->addWidget(scroll);
    }
    if (panel_->isVisible()) {
        panel_->hide();
        return;
    }
    fill_panel();
    panel_->show();
    panel_->raise();
}

void DataStatusIndicator::fill_panel() {
    while (QLayoutItem* it = panel_list_->takeAt(0)) {
        if (QWidget* w = it->widget())
            w->deleteLater();
        delete it;
    }
    const auto problems = services::DataHealth::instance().problems();
    panel_list_->addWidget(ds_text(QStringLiteral("DATA STATUS"), colors::AMBER(), 11, true));
    if (problems.isEmpty()) {
        panel_list_->addWidget(ds_text(QStringLiteral("Everything is loading normally."), colors::TEXT_PRIMARY(), 12));
    }
    QStringList report;
    for (const auto& p : problems) {
        const auto& f = p.failure;
        auto* card = new QFrame;
        card->setObjectName(QStringLiteral("dataCard"));
        card->setStyleSheet(QStringLiteral("QFrame#dataCard { background:%1; border:1px solid %2; }")
                                .arg(colors::BG_RAISED(), colors::BORDER_DIM()));
        auto* vl = new QVBoxLayout(card);
        vl->setContentsMargins(10, 8, 10, 8);
        vl->setSpacing(4);
        const QString title = (f.source.isEmpty() ? QStringLiteral("Data") : f.source) + QStringLiteral(" — ") +
                              data_failure::kind_label(f.kind) +
                              (f.http_status ? QStringLiteral(" (HTTP %1)").arg(f.http_status) : QString());
        vl->addWidget(ds_text(title, colors::WARNING(), 12, true));
        vl->addWidget(ds_text(f.reason, colors::TEXT_PRIMARY(), 12));
        vl->addWidget(ds_text(QStringLiteral("What to do: ") + f.action, colors::TEXT_SECONDARY(), 11));
        vl->addWidget(ds_text(QStringLiteral("Affected — ") + p.items.join(QStringLiteral(" · ")),
                           colors::TEXT_SECONDARY(), 11));
        vl->addWidget(ds_text(QStringLiteral("Last at %1 · %2 failure%3")
                               .arg(p.last.toString(QStringLiteral("HH:mm:ss")))
                               .arg(p.count)
                               .arg(p.count == 1 ? QString() : QStringLiteral("s")),
                           colors::TEXT_TERTIARY(), 10));
        if (!p.keys.isEmpty()) {
            auto* row = new QHBoxLayout;
            auto* retry = ds_link(QStringLiteral("Retry now"));
            const QStringList keys = p.keys;
            connect(retry, &QPushButton::clicked, this, [keys, retry]() {
                datahub::DataHub::instance().request(keys, /*force=*/true);
                retry->setText(QStringLiteral("Retrying…"));
                retry->setEnabled(false);
            });
            row->addWidget(retry);
            row->addStretch();
            vl->addLayout(row);
        }
        panel_list_->addWidget(card);
        report << QStringLiteral("%1\n  %2\n  affected: %3\n  last: %4 (%5 failures)\n  reported: %6")
                      .arg(title, f.reason, p.items.join(QStringLiteral("; ")),
                           p.last.toString(Qt::ISODate))
                      .arg(p.count)
                      .arg(f.raw.left(400));
    }
    if (!problems.isEmpty()) {
        // In a widget of its own, so the next refill deletes it with the rest.
        auto* holder = new QWidget;
        holder->setStyleSheet(QStringLiteral("background:transparent;"));
        auto* row = new QHBoxLayout(holder);
        row->setContentsMargins(0, 0, 0, 0);
        auto* copy = ds_link(QStringLiteral("Copy details"));
        copy->setToolTip(QStringLiteral("Copy what failed and the exact errors — to paste into a support request."));
        connect(copy, &QPushButton::clicked, this, [report, copy]() {
            QApplication::clipboard()->setText(QStringLiteral("finterm %1 — data status\n\n")
                                                   .arg(QCoreApplication::applicationVersion()) +
                                               report.join(QStringLiteral("\n\n")));
            copy->setText(QStringLiteral("Copied"));
        });
        row->addWidget(copy);
        row->addStretch();
        panel_list_->addWidget(holder);
    }
    panel_list_->addStretch();
    // Above the status bar, right-aligned to this slot.
    panel_->adjustSize();
    const int h = qMin(kPanelMaxH, panel_list_->sizeHint().height() + 4);
    panel_->setFixedHeight(h);
    const QPoint at = mapToGlobal(QPoint(width() - kPanelW, -h));
    panel_->move(at);
}

} // namespace finterm::ui
