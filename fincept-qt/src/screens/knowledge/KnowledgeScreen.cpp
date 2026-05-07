#include "screens/knowledge/KnowledgeScreen.h"

#include "screens/knowledge/AbbreviationsColumn.h"
#include "screens/knowledge/CategoryColumn.h"
#include "screens/knowledge/ContentLoader.h"
#include "screens/knowledge/GroupedPane.h"
#include "screens/knowledge/HelpHint.h"
#include "screens/knowledge/RailWidget.h"
#include "ui/theme/Theme.h"

#include <QCoreApplication>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShortcut>
#include <QSplitter>
#include <QVBoxLayout>

namespace fincept::knowledge {

namespace {

constexpr const char* MONO = "font-family: 'Consolas','Courier New',monospace;";

// Sub-tab assignment — 3 content panes around the rail.
//   BASICS   : reference lookup — Glossary · Concepts · Abbreviations
//   REFERENCE: deeper reference — Formulas · Regulators · Interviews
//   PRACTICE : learning paths   — Cases · Tracks · Playbooks
const QStringList BASICS_CATS    = {"glossary", "concepts"};
const QStringList REFERENCE_CATS = {"formulas", "regulators", "interviews"};
const QStringList PRACTICE_CATS  = {"cases", "tracks", "playbooks"};

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

    breadcrumb_ = new QLabel("Cockpit · Basics / Practice / Context", cmd);
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

    // ── Recently viewed bar ───────────────────────────────────────────────────
    recent_bar_ = new QWidget(this);
    recent_bar_->setFixedHeight(30);
    recent_bar_->setStyleSheet(QString("background:%1; border-bottom:1px solid %2;")
                                   .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));
    recent_hl_ = new QHBoxLayout(recent_bar_);
    recent_hl_->setContentsMargins(14, 4, 14, 4);
    recent_hl_->setSpacing(6);
    auto* recent_lbl = new QLabel("RECENT:", recent_bar_);
    recent_lbl->setStyleSheet(QString("color:%1;font-size:10px;font-weight:700;"
                                      "letter-spacing:1px;background:transparent;")
                                  .arg(ui::colors::TEXT_TERTIARY()));
    recent_hl_->addWidget(recent_lbl);
    recent_hl_->addStretch();
    recent_bar_->setVisible(false); // hidden until there's something to show
    root->addWidget(recent_bar_);

    // ── 4-pane horizontal splitter ────────────────────────────────────────────
    auto* split = new QSplitter(Qt::Horizontal, this);
    split->setHandleWidth(1);
    split->setStyleSheet(QString("QSplitter::handle { background: %1; }").arg(ui::colors::BORDER_DIM()));
    split->setChildrenCollapsible(false);

    auto& loader = ContentLoader::instance();
    int total_entries = 0;

    // Build CategoryColumns up front, indexed by category id.
    for (const auto& cat : loader.categories()) {
        auto* col = new CategoryColumn(cat);
        category_cols_.insert(cat.id, col);
        total_entries += cat.entries.size();
        connect(col, &CategoryColumn::entry_activated, this,
                [this, col](const QString& id) { on_category_active(col, id); });
        connect(col, &CategoryColumn::open_entry_external, this, &KnowledgeScreen::open_entry);
    }

    // ── BASICS pane (Glossary, Concepts, Abbreviations) ───────────────────────
    basics_pane_ = new GroupedPane("basics", "BASICS", split);
    basics_pane_->setMinimumWidth(280);
    for (const auto& cat_id : BASICS_CATS) {
        if (auto it = category_cols_.find(cat_id); it != category_cols_.end()) {
            const auto* meta = loader.category(cat_id);
            const QString lbl = meta ? meta->label : cat_id.toUpper();
            basics_pane_->addSubPane(lbl, *it);
        }
    }
    if (!loader.abbreviations().isEmpty()) {
        abbrev_col_ = new AbbreviationsColumn();
        basics_pane_->addSubPane("ABBREV.", abbrev_col_);
        connect(abbrev_col_, &AbbreviationsColumn::entry_activated, this, [this](const QString& id) {
            if (rail_)
                rail_->set_entry(ContentLoader::instance().entry(id));
            if (const auto* e = ContentLoader::instance().entry(id))
                breadcrumb_->setText(QString("Cockpit · BASICS / ABBREV. · %1").arg(e->title));
        });
    }
    split->addWidget(basics_pane_);

    // ── PRACTICE pane ─────────────────────────────────────────────────────────
    practice_pane_ = new GroupedPane("practice", "PRACTICE", split);
    practice_pane_->setMinimumWidth(240);
    for (const auto& cat_id : PRACTICE_CATS) {
        if (auto it = category_cols_.find(cat_id); it != category_cols_.end()) {
            const auto* meta = loader.category(cat_id);
            const QString lbl = meta ? meta->label : cat_id.toUpper();
            practice_pane_->addSubPane(lbl, *it);
        }
    }
    split->addWidget(practice_pane_);

    // ── CONTEXT rail ──────────────────────────────────────────────────────────
    rail_ = new RailWidget(split);
    rail_->setMinimumWidth(320);
    split->addWidget(rail_);
    connect(rail_, &RailWidget::open_entry, this, &KnowledgeScreen::open_entry);
    connect(rail_, &RailWidget::request_action, this,
            [this](const QString& screen, const QString& ticker) { emit navigate_to_screen(screen, ticker); });

    // ── REFERENCE pane (Formulas · Regulators · Interviews) ──────────────────
    reference_pane_ = new GroupedPane("reference", "REFERENCE", split);
    reference_pane_->setMinimumWidth(240);
    for (const auto& cat_id : REFERENCE_CATS) {
        if (auto it = category_cols_.find(cat_id); it != category_cols_.end()) {
            const auto* meta = loader.category(cat_id);
            const QString lbl = meta ? meta->label : cat_id.toUpper();
            reference_pane_->addSubPane(lbl, *it);
        }
    }
    split->addWidget(reference_pane_);

    // BASICS | PRACTICE | CONTEXT (rail) | REFERENCE — 25/20/35/20
    split->setSizes({250, 200, 360, 200});
    split->setStretchFactor(0, 25);
    split->setStretchFactor(1, 20);
    split->setStretchFactor(2, 35);
    split->setStretchFactor(3, 20);

    root->addWidget(split, 1);

    // Restore each grouped pane's previously-active sub-tab. The CategoryColumns
    // already restored their own active entries at construction, so this also
    // makes the rail / breadcrumb reflect the user's last-viewed entry.
    basics_pane_->restoreActiveSubTab();
    if (reference_pane_) reference_pane_->restoreActiveSubTab();
    practice_pane_->restoreActiveSubTab();

    // Trigger the rail to follow whichever sub-tab is currently active in BASICS
    // (the leftmost group leads on cold start).
    if (auto* current = qobject_cast<CategoryColumn*>(basics_pane_->currentSubPane())) {
        const auto id = current->active_entry_id();
        if (!id.isEmpty())
            on_category_active(current, id);
    }

    // When the user switches sub-tabs, refresh the rail/breadcrumb to the
    // restored entry of that newly-active sub-pane.
    auto refresh_from_subpane = [this](QWidget* w) {
        if (auto* col = qobject_cast<CategoryColumn*>(w)) {
            const auto id = col->active_entry_id();
            if (!id.isEmpty())
                on_category_active(col, id);
        } else if (auto* abbr = qobject_cast<AbbreviationsColumn*>(w)) {
            const auto id = abbr->last_selected_entry_id();
            if (!id.isEmpty() && rail_)
                rail_->set_entry(ContentLoader::instance().entry(id));
        }
    };
    connect(basics_pane_,     &GroupedPane::subPaneActivated, this, refresh_from_subpane);
    if (reference_pane_)
        connect(reference_pane_,  &GroupedPane::subPaneActivated, this, refresh_from_subpane);
    connect(practice_pane_,   &GroupedPane::subPaneActivated, this, refresh_from_subpane);

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

    auto it = category_cols_.find(e->category);
    if (it == category_cols_.end())
        return;

    auto* col = *it;
    if (basics_pane_)    basics_pane_->showSubPane(col);
    if (reference_pane_) reference_pane_->showSubPane(col);
    if (practice_pane_)  practice_pane_->showSubPane(col);
    col->open_entry(id);

    // Track in recently viewed (deduplicated, most recent first)
    recently_viewed_.removeAll(id);
    recently_viewed_.prepend(id);
    if (recently_viewed_.size() > kMaxRecent)
        recently_viewed_.resize(kMaxRecent);
    update_recent_bar();
}

void KnowledgeScreen::update_recent_bar() {
    // Remove all chips (keep the "RECENT:" label at index 0)
    while (recent_hl_->count() > 2)  // label + stretch
        if (auto* item = recent_hl_->takeAt(1)) {
            if (auto* w = item->widget()) w->deleteLater();
            delete item;
        }

    const auto& loader = ContentLoader::instance();
    for (const auto& rid : recently_viewed_) {
        const auto* e = loader.entry(rid);
        if (!e) continue;
        auto* chip = new QPushButton(e->title, recent_bar_);
        chip->setFlat(true);
        chip->setCursor(Qt::PointingHandCursor);
        chip->setStyleSheet(
            QString("QPushButton{color:%1;font-size:10px;background:%2;border:1px solid %3;"
                    "border-radius:10px;padding:1px 8px;}"
                    "QPushButton:hover{border-color:%4;color:%4;}")
                .arg(ui::colors::TEXT_SECONDARY(), ui::colors::BG_RAISED(),
                     ui::colors::BORDER_DIM(), ui::colors::AMBER()));
        connect(chip, &QPushButton::clicked, this, [this, rid]() { open_entry(rid); });
        recent_hl_->insertWidget(recent_hl_->count() - 1, chip); // before stretch
    }
    recent_bar_->setVisible(!recently_viewed_.isEmpty());
}

void KnowledgeScreen::on_category_active(CategoryColumn* col, const QString& entry_id) {
    if (rail_)
        rail_->set_entry(ContentLoader::instance().entry(entry_id));

    if (const auto* e = ContentLoader::instance().entry(entry_id)) {
        QString cat = e->category;
        if (auto* c = ContentLoader::instance().category(e->category))
            cat = c->label;
        const QString group = BASICS_CATS.contains(e->category)    ? "BASICS"
                            : REFERENCE_CATS.contains(e->category) ? "REFERENCE"
                                                                    : "PRACTICE";
        breadcrumb_->setText(QString("Cockpit · %1 / %2 · %3").arg(group, cat, e->title));
    }
    Q_UNUSED(col);
}

void KnowledgeScreen::on_search(const QString& /*query*/) {
    // No-op for now — the typeahead semantics live on returnPressed which
    // jumps to the top match. Live-filtering each column would be confusing
    // since the cockpit shows multiple entries simultaneously. Future:
    // dropdown of matches on focus.
}

} // namespace fincept::knowledge
