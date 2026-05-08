#include "screens/knowledge/GroupedPane.h"

#include "ui/theme/Theme.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QSettings>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace fincept::knowledge {

namespace {

constexpr const char* MONO = "font-family: 'Consolas','Courier New',monospace;";
constexpr const char* BODY_BG = "#13110f";

QString tab_button_ss(bool active) {
    // Segmented uppercase pill. Active = amber accent on raised bg with amber
    // border (the "selected chip" look). Inactive = bright primary text on a
    // subtle raised bg with a visible mid-gray border, so it reads as a
    // tappable button rather than a faint label.
    const QString fg     = active ? ui::colors::AMBER()     : ui::colors::TEXT_PRIMARY();
    const QString bg     = active ? ui::colors::BG_RAISED() : ui::colors::BG_SURFACE();
    const QString border = active ? ui::colors::AMBER()     : ui::colors::BORDER_BRIGHT();
    return QString("QToolButton { color: %1; background: %2; border: 1px solid %3;"
                   " padding: 3px 9px; font-size: 10px; font-weight: bold;"
                   " letter-spacing: 1px; %4 }"
                   "QToolButton:hover { color: %5; border-color: %5;"
                   "                    background: %6; }")
        .arg(fg, bg, border, MONO, ui::colors::AMBER(), ui::colors::BG_RAISED());
}

} // namespace

GroupedPane::GroupedPane(const QString& group_id, const QString& title, QWidget* parent)
    : QWidget(parent), group_id_(group_id) {
    setStyleSheet(QString("background: %1;").arg(QString(BODY_BG)));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Header bar: group label · scrollable sub-tab buttons ─────────────────
    // Wrapped in a QScrollArea so a pane with many sub-tabs (e.g. REFERENCE
    // with Formulas · Regulators) never clips the rightmost tab.
    auto* header_container = new QWidget(this);
    header_container->setFixedHeight(46);
    header_container->setStyleSheet(QString("background: %1; border-bottom: 1px solid %2;")
                                        .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));
    auto* hc_layout = new QHBoxLayout(header_container);
    hc_layout->setContentsMargins(12, 0, 12, 0);
    hc_layout->setSpacing(8);

    auto* lbl = new QLabel(title, header_container);
    lbl->setStyleSheet(QString("color: %1; background: transparent; font-size: 11px; font-weight: bold;"
                               " letter-spacing: 1.6px; %2")
                           .arg(ui::colors::AMBER(), MONO));
    hc_layout->addWidget(lbl);

    auto* sep = new QLabel("\xc2\xb7", header_container);
    sep->setStyleSheet(QString("color: %1; background: transparent; %2").arg(ui::colors::BORDER_BRIGHT(), MONO));
    hc_layout->addWidget(sep);

    // Scrollable button strip — buttons are added inside btn_row_ which sits
    // inside a horizontally-scrolling QScrollArea so tabs never get clipped.
    auto* btn_scroll = new QScrollArea(header_container);
    btn_scroll->setFrameShape(QFrame::NoFrame);
    btn_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    btn_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    btn_scroll->setWidgetResizable(true);
    btn_scroll->setStyleSheet("QScrollArea{border:none;background:transparent;}");

    btn_row_ = new QWidget(btn_scroll);
    btn_row_->setStyleSheet("background:transparent;");
    auto* hl = new QHBoxLayout(btn_row_);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(6);

    btn_group_ = new QButtonGroup(this);
    btn_group_->setExclusive(true);

    hl->addStretch(); // buttons inserted before this stretch in addSubPane()
    btn_scroll->setWidget(btn_row_);
    hc_layout->addWidget(btn_scroll, 1);

    root->addWidget(header_container);

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
