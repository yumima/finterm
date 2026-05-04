#include "screens/knowledge/GroupedPane.h"

#include "ui/theme/Theme.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QSettings>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace fincept::knowledge {

namespace {

constexpr const char* MONO = "font-family: 'Consolas','Courier New',monospace;";
constexpr const char* BODY_BG = "#13110f";

QString tab_button_ss(bool active) {
    // Segmented uppercase pill — active button gets the amber accent,
    // inactive sits flat against the header bar.
    const QString fg = active ? ui::colors::AMBER() : ui::colors::TEXT_TERTIARY();
    const QString bg = active ? ui::colors::BG_RAISED() : QString("transparent");
    const QString border = active ? ui::colors::AMBER() : ui::colors::BORDER_DIM();
    return QString("QToolButton { color: %1; background: %2; border: 1px solid %3;"
                   " padding: 4px 12px; font-size: 10px; font-weight: bold;"
                   " letter-spacing: 1.4px; %4 }"
                   "QToolButton:hover { color: %5; border-color: %5; }")
        .arg(fg, bg, border, MONO, ui::colors::AMBER());
}

} // namespace

GroupedPane::GroupedPane(const QString& group_id, const QString& title, QWidget* parent)
    : QWidget(parent), group_id_(group_id) {
    setStyleSheet(QString("background: %1;").arg(QString(BODY_BG)));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Header bar: group label · segmented sub-tab buttons ───────────────────
    btn_row_ = new QWidget(this);
    btn_row_->setFixedHeight(46);
    btn_row_->setStyleSheet(QString("background: %1; border-bottom: 1px solid %2;")
                                .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));
    auto* hl = new QHBoxLayout(btn_row_);
    hl->setContentsMargins(12, 0, 12, 0);
    hl->setSpacing(8);

    auto* lbl = new QLabel(title, btn_row_);
    lbl->setStyleSheet(QString("color: %1; background: transparent; font-size: 11px; font-weight: bold;"
                               " letter-spacing: 1.6px; %2")
                           .arg(ui::colors::AMBER(), MONO));
    hl->addWidget(lbl);

    auto* sep = new QLabel("·", btn_row_);
    sep->setStyleSheet(QString("color: %1; background: transparent; %2").arg(ui::colors::BORDER_BRIGHT(), MONO));
    hl->addWidget(sep);

    btn_group_ = new QButtonGroup(this);
    btn_group_->setExclusive(true);

    // Buttons are added by addSubPane(); push a stretch now so they pack left.
    hl->addStretch();

    root->addWidget(btn_row_);

    // ── Stack of sub-panes ────────────────────────────────────────────────────
    stack_ = new QStackedWidget(this);
    stack_->setStyleSheet(QString("background: %1;").arg(QString(BODY_BG)));
    root->addWidget(stack_, 1);
}

void GroupedPane::addSubPane(const QString& tab_label, QWidget* widget) {
    if (!widget)
        return;
    const int idx = stack_->addWidget(widget);

    auto* btn = new QToolButton(btn_row_);
    btn->setText(tab_label);
    btn->setCheckable(true);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setStyleSheet(tab_button_ss(false));
    btn_group_->addButton(btn, idx);
    buttons_.push_back(btn);

    // Insert just before the trailing stretch in the header layout.
    auto* hl = qobject_cast<QHBoxLayout*>(btn_row_->layout());
    if (hl) {
        const int insert_at = hl->count() - 1; // before stretch
        hl->insertWidget(insert_at, btn);
    }

    connect(btn, &QToolButton::clicked, this, [this, idx]() { setActiveIndex(idx, /*persist*/ true); });
}

int GroupedPane::subPaneCount() const { return stack_->count(); }

QWidget* GroupedPane::currentSubPane() const { return stack_->currentWidget(); }

void GroupedPane::showSubPane(QWidget* widget) {
    const int idx = stack_->indexOf(widget);
    if (idx >= 0)
        setActiveIndex(idx, /*persist*/ true);
}

void GroupedPane::restoreActiveSubTab() {
    if (stack_->count() == 0)
        return;
    QSettings s;
    const int saved = s.value(QString("knowledge/%1/active_subtab").arg(group_id_), 0).toInt();
    const int idx = (saved >= 0 && saved < stack_->count()) ? saved : 0;
    setActiveIndex(idx, /*persist*/ false);
}

void GroupedPane::setActiveIndex(int idx, bool persist) {
    if (idx < 0 || idx >= stack_->count())
        return;
    stack_->setCurrentIndex(idx);
    if (auto* btn = btn_group_->button(idx))
        btn->setChecked(true);
    restyleButtons();
    if (persist) {
        QSettings s;
        s.setValue(QString("knowledge/%1/active_subtab").arg(group_id_), idx);
    }
    emit subPaneActivated(stack_->currentWidget());
}

void GroupedPane::restyleButtons() {
    for (int i = 0; i < buttons_.size(); ++i)
        buttons_[i]->setStyleSheet(tab_button_ss(buttons_[i]->isChecked()));
}

} // namespace fincept::knowledge
