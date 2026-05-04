#include "screens/knowledge/HelpHint.h"

#include "screens/knowledge/ContentLoader.h"
#include "ui/theme/Theme.h"

#include <QApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QScreen>
#include <QVBoxLayout>

namespace fincept::knowledge {

namespace {
std::function<void(QString)>& navigator_storage() {
    static std::function<void(QString)> n;
    return n;
}
constexpr int POPOVER_WIDTH = 320;
constexpr int HINT_SIZE = 16;
} // namespace

void HelpHint::set_navigator(std::function<void(QString)> nav) {
    navigator_storage() = std::move(nav);
}

HelpHint::HelpHint(QString entry_id, QWidget* parent) : QPushButton(parent), entry_id_(std::move(entry_id)) {
    setText("?");
    setFixedSize(HINT_SIZE, HINT_SIZE);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setToolTip(QString("Learn: %1").arg(entry_id_));
    setStyleSheet(QString("QPushButton { color: %1; background: transparent; border: 1px solid %2;"
                          " border-radius: 8px; font-size: 9px; font-weight: bold;"
                          " font-family: 'Consolas','Courier New',monospace; padding: 0; }"
                          "QPushButton:hover { color: %3; border-color: %3; background: %4; }")
                      .arg(ui::colors::TEXT_TERTIARY(), ui::colors::BORDER_DIM(), ui::colors::AMBER(),
                           ui::colors::BG_RAISED()));
    connect(this, &QPushButton::clicked, this, &HelpHint::show_popover);
}

void HelpHint::show_popover() {
    const auto* entry = ContentLoader::instance().entry(entry_id_);

    auto* popover = new QFrame(nullptr, Qt::Popup);
    popover->setAttribute(Qt::WA_DeleteOnClose);
    popover->setFixedWidth(POPOVER_WIDTH);
    popover->setStyleSheet(QString("QFrame { background: %1; border: 1px solid %2;"
                                   " border-left: 3px solid %3; }")
                               .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_BRIGHT(), ui::colors::AMBER()));

    auto* vl = new QVBoxLayout(popover);
    vl->setContentsMargins(14, 12, 14, 12);
    vl->setSpacing(8);

    if (!entry) {
        auto* miss = new QLabel(QString("No knowledge entry for \"%1\"").arg(entry_id_), popover);
        miss->setStyleSheet(QString("color: %1; font-size: 11px; background: transparent;"
                                    " font-family: 'Consolas','Courier New',monospace;")
                                .arg(ui::colors::TEXT_SECONDARY()));
        miss->setWordWrap(true);
        vl->addWidget(miss);
    } else {
        auto* title = new QLabel(entry->title, popover);
        title->setStyleSheet(QString("color: %1; font-size: 13px; font-weight: bold; background: transparent;"
                                     " font-family: 'Consolas','Courier New',monospace;")
                                 .arg(ui::colors::AMBER()));
        vl->addWidget(title);

        if (!entry->abbreviation.isEmpty() && entry->abbreviation != entry->title) {
            auto* abbr = new QLabel(entry->abbreviation, popover);
            abbr->setStyleSheet(QString("color: %1; font-size: 10px; background: transparent;"
                                        " font-family: 'Consolas','Courier New',monospace;")
                                    .arg(ui::colors::TEXT_TERTIARY()));
            vl->addWidget(abbr);
        }
        if (!entry->subtitle.isEmpty()) {
            auto* sub = new QLabel(entry->subtitle, popover);
            sub->setStyleSheet(QString("color: %1; font-size: 11px; background: transparent;"
                                       " font-family: 'Consolas','Courier New',monospace;")
                                   .arg(ui::colors::TEXT_SECONDARY()));
            sub->setWordWrap(true);
            vl->addWidget(sub);
        }

        // First paragraph of the body as a teaser.
        QString body = ContentLoader::instance().load_body(entry->id);
        if (!body.isEmpty()) {
            QString teaser = body.section("\n\n", 0, 0).trimmed();
            // Strip a leading markdown header.
            if (teaser.startsWith("#")) {
                int nl = teaser.indexOf('\n');
                teaser = nl < 0 ? QString{} : teaser.mid(nl + 1).trimmed();
            }
            if (!teaser.isEmpty()) {
                if (teaser.size() > 280)
                    teaser = teaser.left(277) + "...";
                auto* tlabel = new QLabel(teaser, popover);
                tlabel->setStyleSheet(QString("color: %1; font-size: 11px; background: transparent;"
                                              " font-family: 'Consolas','Courier New',monospace;"
                                              " padding-top: 4px; border-top: 1px solid %2;")
                                          .arg(ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_DIM()));
                tlabel->setWordWrap(true);
                vl->addWidget(tlabel);
            }
        }

        auto* open_btn = new QPushButton("Open in  KNOWLEDGE  →", popover);
        open_btn->setCursor(Qt::PointingHandCursor);
        open_btn->setStyleSheet(QString("QPushButton { color: %1; background: %2; border: 1px solid %3;"
                                        " padding: 6px 10px; font-size: 11px; font-weight: bold;"
                                        " font-family: 'Consolas','Courier New',monospace; }"
                                        "QPushButton:hover { background: %4; color: %5; border-color: %5; }")
                                    .arg(ui::colors::TEXT_PRIMARY(), ui::colors::BG_RAISED(),
                                         ui::colors::BORDER_DIM(), ui::colors::ACCENT_BG(), ui::colors::AMBER()));
        QString id = entry->id;
        connect(open_btn, &QPushButton::clicked, popover, [popover, id]() {
            popover->close();
            if (auto& nav = navigator_storage(); nav)
                nav(id);
        });
        vl->addWidget(open_btn);
    }

    popover->adjustSize();
    QPoint anchor = mapToGlobal(QPoint(0, height() + 4));
    if (auto* scr = QApplication::screenAt(anchor)) {
        const QRect avail = scr->availableGeometry();
        if (anchor.x() + popover->width() > avail.right())
            anchor.setX(avail.right() - popover->width() - 4);
        if (anchor.y() + popover->height() > avail.bottom())
            anchor.setY(mapToGlobal(QPoint(0, 0)).y() - popover->height() - 4);
    }
    popover->move(anchor);
    popover->show();
}

} // namespace fincept::knowledge
