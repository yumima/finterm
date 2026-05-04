#include "screens/knowledge/CategoryColumn.h"

#include "screens/knowledge/ContentLoader.h"
#include "screens/knowledge/EntryBodyView.h"
#include "ui/theme/Theme.h"

#include <QComboBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QVBoxLayout>

namespace fincept::knowledge {

namespace {
constexpr const char* MONO = "font-family: 'Consolas','Courier New',monospace;";
constexpr const char* BODY_BG = "#13110f";

QString picker_ss() {
    return QString("QComboBox { background: %1; color: %2; border: 1px solid %3;"
                   " padding: 5px 10px; font-size: 11px; %4 }"
                   "QComboBox:hover { border-color: %5; }"
                   "QComboBox::drop-down { border: none; width: 18px; }"
                   "QComboBox::down-arrow { width: 0; height: 0;"
                   " border-left: 4px solid transparent; border-right: 4px solid transparent;"
                   " border-top: 5px solid %5; margin-right: 6px; }"
                   "QComboBox QAbstractItemView { background: %1; color: %2; border: 1px solid %3;"
                   " selection-background-color: %6; selection-color: %5; outline: 0;"
                   " font-size: 11px; %4 }")
        .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_DIM(), MONO,
             ui::colors::AMBER(), ui::colors::BG_RAISED());
}
} // namespace

CategoryColumn::CategoryColumn(const KnowledgeCategory& category, QWidget* parent)
    : QWidget(parent), category_id_(category.id) {
    setStyleSheet(QString("background: %1;").arg(QString(BODY_BG)));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Header bar ────────────────────────────────────────────────────────────
    auto* header = new QWidget(this);
    header->setFixedHeight(46);
    header->setStyleSheet(QString("background: %1; border-bottom: 1px solid %2;")
                              .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));
    auto* hl = new QHBoxLayout(header);
    hl->setContentsMargins(12, 0, 8, 0);
    hl->setSpacing(8);

    header_label_ = new QLabel(category.label, header);
    header_label_->setStyleSheet(QString("color: %1; background: transparent; font-size: 11px; font-weight: bold;"
                                         " letter-spacing: 1.5px; %2")
                                     .arg(ui::colors::TEXT_TERTIARY(), MONO));
    hl->addWidget(header_label_);

    picker_ = new QComboBox(header);
    picker_->setStyleSheet(picker_ss());
    picker_->setMinimumWidth(140);
    for (const auto& e : category.entries)
        picker_->addItem(e.title, e.id);
    hl->addWidget(picker_, 1);

    root->addWidget(header);

    // ── Body view ─────────────────────────────────────────────────────────────
    body_ = new EntryBodyView(this);
    root->addWidget(body_, 1);

    // ── Wire ──────────────────────────────────────────────────────────────────
    connect(picker_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        if (idx < 0)
            return;
        const QString id = picker_->itemData(idx).toString();
        set_active_entry_internal(id);
    });
    connect(body_, &EntryBodyView::open_entry, this, &CategoryColumn::open_entry_external);

    // Click anywhere on header → focus this column (drive the rail).
    header->installEventFilter(this);
    body_->installEventFilter(this);

    // Auto-select first entry so the column has content immediately.
    if (!category.entries.isEmpty()) {
        picker_->setCurrentIndex(0);
        set_active_entry_internal(category.entries.first().id);
    }
}

bool CategoryColumn::open_entry(const QString& entry_id) {
    // Find the index in the picker matching this entry id.
    for (int i = 0; i < picker_->count(); ++i) {
        if (picker_->itemData(i).toString() == entry_id) {
            picker_->setCurrentIndex(i); // triggers currentIndexChanged → set_active_entry
            return true;
        }
    }
    return false;
}

void CategoryColumn::set_active_entry_internal(const QString& entry_id) {
    if (entry_id.isEmpty())
        return;
    active_entry_id_ = entry_id;
    body_->set_entry(ContentLoader::instance().entry(entry_id));
    emit entry_activated(entry_id);
}

void CategoryColumn::set_active(bool active) {
    header_label_->setStyleSheet(QString("color: %1; background: transparent; font-size: 11px; font-weight: bold;"
                                         " letter-spacing: 1.5px; %2")
                                     .arg(active ? ui::colors::AMBER() : ui::colors::TEXT_TERTIARY(), MONO));
}

bool CategoryColumn::eventFilter(QObject* watched, QEvent* event) {
    // Any mouse press inside this column (other than the picker dropdown
    // changing entries — that already triggers entry_activated) should
    // "focus" the column so the rail follows it.
    if (event->type() == QEvent::MouseButtonPress) {
        if (!active_entry_id_.isEmpty())
            emit entry_activated(active_entry_id_);
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace fincept::knowledge
