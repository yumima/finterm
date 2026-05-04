#include "screens/knowledge/KnowledgeScreen.h"

#include "screens/knowledge/AbbreviationsColumn.h"
#include "screens/knowledge/CategoryColumn.h"
#include "screens/knowledge/ContentLoader.h"
#include "screens/knowledge/HelpHint.h"
#include "screens/knowledge/RailWidget.h"
#include "ui/theme/Theme.h"

#include <QCoreApplication>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QShortcut>
#include <QSplitter>
#include <QVBoxLayout>

namespace fincept::knowledge {

namespace {

constexpr const char* MONO = "font-family: 'Consolas','Courier New',monospace;";

QString search_ss() {
    return QString("QLineEdit { background: %1; color: %2; border: 1px solid %3;"
                   " padding: 6px 10px; font-size: 12px; %4 }"
                   "QLineEdit:focus { border-color: %5; }")
        .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_DIM(), MONO,
             ui::colors::AMBER());
}

QString small_label_ss() {
    return QString("color: %1; background: transparent; font-size: 11px; %2")
        .arg(ui::colors::TEXT_TERTIARY(), MONO);
}

QString resolve_root() {
    const QString exe_dir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        exe_dir + "/resources/knowledge",
        exe_dir + "/../resources/knowledge",
        exe_dir + "/../share/fincept/knowledge",
    };
    for (const auto& p : candidates)
        if (QDir(p).exists())
            return QDir(p).absolutePath();
    return candidates.first();
}

} // namespace

KnowledgeScreen::KnowledgeScreen(QWidget* parent) : QWidget(parent) {
    setObjectName("knowledgeScreen");
    setStyleSheet(QString("QWidget#knowledgeScreen { background: %1; }").arg(ui::colors::BG_BASE()));

    auto& loader = ContentLoader::instance();
    if (!loader.is_loaded())
        loader.load(resolve_root());

    build_layout();

    // Wire HelpHint global navigator to ourselves on construction.
    HelpHint::set_navigator([this](QString id) { open_entry(id); });
}

void KnowledgeScreen::build_layout() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Command bar ───────────────────────────────────────────────────────────
    auto* cmd = new QWidget(this);
    cmd->setFixedHeight(40);
    cmd->setStyleSheet(QString("background: %1; border-bottom: 1px solid %2;")
                           .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));
    auto* ch = new QHBoxLayout(cmd);
    ch->setContentsMargins(14, 0, 14, 0);
    ch->setSpacing(12);

    auto* title = new QLabel("KNOWLEDGE", cmd);
    title->setStyleSheet(QString("color: %1; font-size: 13px; font-weight: bold; letter-spacing: 1.5px;"
                                 " background: transparent; %2")
                             .arg(ui::colors::AMBER(), MONO));
    ch->addWidget(title);

    auto* sep = new QLabel("|", cmd);
    sep->setStyleSheet(QString("color: %1; background: transparent; %2").arg(ui::colors::BORDER_BRIGHT(), MONO));
    ch->addWidget(sep);

    breadcrumb_ = new QLabel("Cockpit · 5-pane view", cmd);
    breadcrumb_->setStyleSheet(QString("color: %1; background: transparent; font-size: 11px; %2")
                                   .arg(ui::colors::TEXT_SECONDARY(), MONO));
    ch->addWidget(breadcrumb_);
    ch->addStretch();

    search_ = new QLineEdit(cmd);
    search_->setPlaceholderText("Jump to entry…   ( / to focus )");
    search_->setFixedWidth(320);
    search_->setStyleSheet(search_ss());
    ch->addWidget(search_);

    count_label_ = new QLabel("", cmd);
    count_label_->setStyleSheet(small_label_ss());
    ch->addWidget(count_label_);

    root->addWidget(cmd);

    // ── 5-pane horizontal splitter ────────────────────────────────────────────
    auto* split = new QSplitter(Qt::Horizontal, this);
    split->setHandleWidth(1);
    split->setStyleSheet(QString("QSplitter::handle { background: %1; }").arg(ui::colors::BORDER_DIM()));
    split->setChildrenCollapsible(false);

    auto& loader = ContentLoader::instance();
    const auto cats = loader.categories();
    int total_entries = 0;
    constexpr int COL_MIN_WIDTH = 240;

    // Order: GLOSSARY, CONCEPTS, PLAYBOOKS, FINTERM RAIL, ABBREVIATIONS (rightmost)
    for (const auto& cat : cats) {
        auto* col = new CategoryColumn(cat, split);
        col->setMinimumWidth(COL_MIN_WIDTH);
        category_cols_.push_back(col);
        split->addWidget(col);
        total_entries += cat.entries.size();

        connect(col, &CategoryColumn::entry_activated, this,
                [this, col](const QString& id) { on_column_active(col, id); });
        connect(col, &CategoryColumn::open_entry_external, this, &KnowledgeScreen::open_entry);
    }

    // Rail column (between playbooks and abbreviations)
    rail_ = new RailWidget(split);
    rail_->setMinimumWidth(COL_MIN_WIDTH);
    split->addWidget(rail_);
    connect(rail_, &RailWidget::open_entry, this, &KnowledgeScreen::open_entry);
    connect(rail_, &RailWidget::request_action, this,
            [this](const QString& screen, const QString& ticker) { emit navigate_to_screen(screen, ticker); });

    // Abbreviations column (rightmost)
    if (!loader.abbreviations().isEmpty()) {
        abbrev_col_ = new AbbreviationsColumn(split);
        abbrev_col_->setMinimumWidth(COL_MIN_WIDTH);
        split->addWidget(abbrev_col_);
        connect(abbrev_col_, &AbbreviationsColumn::entry_activated, this, [this](const QString& id) {
            set_active_column(abbrev_col_);
            if (rail_)
                rail_->set_entry(ContentLoader::instance().entry(id));
        });
    }

    // Equal widths and equal stretch for all panes — drag handles to rebalance.
    const int n = split->count();
    QList<int> sizes;
    for (int i = 0; i < n; ++i)
        sizes << 400;
    split->setSizes(sizes);
    for (int i = 0; i < n; ++i)
        split->setStretchFactor(i, 1);

    root->addWidget(split, 1);

    // ── Initial state: first column drives the rail ───────────────────────────
    if (!category_cols_.isEmpty()) {
        set_active_column(category_cols_.first());
        const auto first_id = category_cols_.first()->active_entry_id();
        if (!first_id.isEmpty())
            rail_->set_entry(loader.entry(first_id));
    }

    // ── Search wiring (typeahead routes into the right column) ────────────────
    connect(search_, &QLineEdit::textChanged, this, &KnowledgeScreen::on_search);
    connect(search_, &QLineEdit::returnPressed, this, [this]() {
        const auto results = ContentLoader::instance().search(search_->text(), 1);
        if (!results.isEmpty())
            open_entry(results.first());
    });

    auto* slash = new QShortcut(QKeySequence("/"), this);
    slash->setContext(Qt::WidgetWithChildrenShortcut);
    connect(slash, &QShortcut::activated, this, [this]() {
        search_->setFocus();
        search_->selectAll();
    });

    count_label_->setText(QString("%1 ENTRIES  ·  %2 ABBREV.")
                              .arg(total_entries)
                              .arg(loader.abbreviations().size()));
}

void KnowledgeScreen::open_entry(const QString& entry_id) {
    if (entry_id.isEmpty())
        return;
    auto& loader = ContentLoader::instance();
    QString id = loader.resolve_alias(entry_id);
    if (id.isEmpty())
        id = entry_id;
    const auto* e = loader.entry(id);
    if (!e)
        return;

    // Find the category column that owns this entry, switch its picker to it,
    // and make it active.
    for (auto* col : category_cols_) {
        if (col->category_id() == e->category) {
            col->open_entry(id); // triggers entry_activated → on_column_active
            return;
        }
    }
}

void KnowledgeScreen::on_column_active(CategoryColumn* col, const QString& entry_id) {
    set_active_column(col);
    if (rail_)
        rail_->set_entry(ContentLoader::instance().entry(entry_id));

    if (const auto* e = ContentLoader::instance().entry(entry_id)) {
        QString cat = e->category;
        if (auto* c = ContentLoader::instance().category(e->category))
            cat = c->label;
        breadcrumb_->setText(QString("Cockpit · %1 / %2").arg(cat, e->title));
    }
}

void KnowledgeScreen::set_active_column(QWidget* col) {
    if (active_col_ == col)
        return;
    // Toggle the highlight on category columns. Abbreviations doesn't need it.
    for (auto* c : category_cols_)
        c->set_active(c == col);
    active_col_ = col;
}

void KnowledgeScreen::on_search(const QString& /*query*/) {
    // No-op for now — the typeahead semantics live on returnPressed which
    // jumps to the top match. Live-filtering each column would be confusing
    // since the cockpit shows multiple entries simultaneously. Future:
    // dropdown of matches on focus.
}

} // namespace fincept::knowledge
