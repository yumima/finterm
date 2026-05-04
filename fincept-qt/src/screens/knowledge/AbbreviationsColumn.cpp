#include "screens/knowledge/AbbreviationsColumn.h"

#include "screens/knowledge/ContentLoader.h"
#include "ui/theme/Theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace fincept::knowledge {

namespace {
constexpr const char* MONO = "font-family: 'Consolas','Courier New',monospace;";
constexpr const char* SERIF =
    "font-family: 'Charter','Bitstream Charter','Iowan Old Style','Georgia','Cambria',serif;";
constexpr const char* BODY_BG = "#13110f";

QString filter_ss() {
    return QString("QLineEdit { background: %1; color: %2; border: 1px solid %3;"
                   " padding: 5px 9px; font-size: 11px; %4 }"
                   "QLineEdit:focus { border-color: %5; }")
        .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_DIM(), MONO,
             ui::colors::AMBER());
}

QString list_ss() {
    return QString("QListWidget { background: %1; color: %2; border: none; outline: 0;"
                   " font-size: 12px; %3 }"
                   "QListWidget::item { padding: 8px 14px; border-bottom: 1px solid %4; }"
                   "QListWidget::item:hover { background: %5; }"
                   "QListWidget::item:selected { background: %6; color: %7; }"
                   "QScrollBar:vertical { width: 6px; background: transparent; }"
                   "QScrollBar::handle:vertical { background: %8; }"
                   "QScrollBar::handle:vertical:hover { background: %9; }"
                   "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }")
        .arg(QString(BODY_BG), ui::colors::TEXT_PRIMARY(), QString(SERIF), ui::colors::BORDER_DIM(),
             ui::colors::BG_RAISED(), ui::colors::ACCENT_BG(), ui::colors::AMBER(),
             ui::colors::BORDER_MED(), ui::colors::BORDER_BRIGHT());
}

} // namespace

AbbreviationsColumn::AbbreviationsColumn(QWidget* parent) : QWidget(parent) {
    setStyleSheet(QString("background: %1;").arg(QString(BODY_BG)));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Header
    auto* header = new QWidget(this);
    header->setFixedHeight(46);
    header->setStyleSheet(QString("background: %1; border-bottom: 1px solid %2;")
                              .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));
    auto* hl = new QHBoxLayout(header);
    hl->setContentsMargins(12, 0, 8, 0);
    hl->setSpacing(8);

    auto* lbl = new QLabel("ABBREVIATIONS", header);
    lbl->setStyleSheet(QString("color: %1; background: transparent; font-size: 11px; font-weight: bold;"
                               " letter-spacing: 1.5px; %2")
                           .arg(ui::colors::TEXT_TERTIARY(), MONO));
    hl->addWidget(lbl);

    filter_ = new QLineEdit(header);
    filter_->setPlaceholderText("Filter…");
    filter_->setStyleSheet(filter_ss());
    filter_->setMinimumWidth(140);
    hl->addWidget(filter_, 1);
    root->addWidget(header);

    // List
    list_ = new QListWidget(this);
    list_->setStyleSheet(list_ss());
    list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list_->setUniformItemSizes(false);
    list_->setWordWrap(true);
    root->addWidget(list_, 1);

    connect(filter_, &QLineEdit::textChanged, this, &AbbreviationsColumn::rebuild_list);
    connect(list_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (!item)
            return;
        const auto data = item->data(Qt::UserRole).toString();
        if (data.isEmpty())
            return;
        // data holds the abbreviation key — try to resolve to a knowledge entry.
        const QString entry_id = ContentLoader::instance().resolve_alias(data);
        if (!entry_id.isEmpty()) {
            last_entry_id_ = entry_id;
            emit entry_activated(entry_id);
        }
    });

    rebuild_list();
}

void AbbreviationsColumn::rebuild_list(const QString& filter) {
    list_->clear();
    auto& loader = ContentLoader::instance();
    const auto& abbrs = loader.abbreviations();

    QStringList keys = abbrs.keys();
    std::sort(keys.begin(), keys.end());
    const QString needle = filter.trimmed().toLower();

    for (const auto& k : keys) {
        const QString expansion = abbrs.value(k);
        if (!needle.isEmpty()) {
            if (!k.toLower().contains(needle) && !expansion.toLower().contains(needle))
                continue;
        }
        // Two-line render: bold key, then expansion below — using HTML in a
        // QListWidgetItem keeps the styling without a custom delegate.
        const bool linked = !loader.resolve_alias(k).isEmpty();
        const QString key_color = linked ? ui::colors::AMBER() : ui::colors::TEXT_PRIMARY();
        QString text = QString("%1\n%2").arg(k, expansion);
        auto* item = new QListWidgetItem(text, list_);
        item->setData(Qt::UserRole, k);
        if (linked)
            item->setToolTip(QString("Click to load %1 into the finterm context →").arg(k));
        // Color the key via item's foreground (whole row); good enough for v1.
        item->setForeground(QColor(key_color));
    }
}

} // namespace fincept::knowledge
