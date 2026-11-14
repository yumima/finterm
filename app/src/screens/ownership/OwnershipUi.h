#pragma once
// Small shared pieces for the Ownership widgets: a section title, a data
// table with the terminal's conventions, a coloured cell, and the formatting
// rules every panel follows so the same number reads the same everywhere.

#include "ui/formatting/NumberFormat.h"
#include "ui/theme/Theme.h"

#include <QDate>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QWidget>

#include <algorithm>
#include <initializer_list>
#include <optional>

namespace finterm::screens::ownership_ui {

namespace fmt = finterm::ui::formatting;

/// The amber caps label every section starts with, with an optional right
/// side for the section's own "as of" line — the date is part of the title
/// because it is part of the number.
struct SectionTitle {
    QWidget* widget = nullptr;
    QLabel* left = nullptr;
    QLabel* right = nullptr;
};

inline SectionTitle section_title(const QString& text) {
    SectionTitle t;
    t.widget = new QWidget;
    auto* h = new QHBoxLayout(t.widget);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(10);
    t.left = new QLabel(text);
    t.left->setStyleSheet(
        QString("color:%1;font-weight:700;font-size:12px;letter-spacing:1px;").arg(ui::colors::AMBER()));
    t.right = new QLabel;
    t.right->setStyleSheet(QString("color:%1;font-size:12px;").arg(ui::colors::TEXT_SECONDARY()));
    t.right->setWordWrap(true);
    h->addWidget(t.left);
    h->addWidget(t.right, 1);
    return t;
}

inline QTableWidget* make_table(const QStringList& headers) {
    auto* t = new QTableWidget;
    t->setColumnCount(headers.size());
    t->setHorizontalHeaderLabels(headers);
    t->verticalHeader()->setVisible(false);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setAlternatingRowColors(true);
    t->setSortingEnabled(false);
    // Interactive + stretch-last: the reader can size columns, and nothing
    // re-measures on every layout pass. Callers size once after filling.
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    t->horizontalHeader()->setSectionsMovable(true);
    // Content-width columns with empty space to the right, rather than a
    // last column stretched across half the pane with its header adrift.
    t->horizontalHeader()->setStretchLastSection(false);
    t->setWordWrap(false);
    return t;
}

namespace detail {
/// Gives a table's spare width to its text columns; see fill_width().
class ColumnFiller : public QObject {
  public:
    explicit ColumnFiller(QTableWidget* t) : QObject(t), table_(t) {
        setObjectName(QStringLiteral("ownershipColumnFiller"));
        t->viewport()->installEventFilter(this);
        // The header is Interactive: a column the reader sizes by hand is
        // theirs until the next refill (fill_width() after new data), and
        // resizing any other column re-balances the rest.
        QObject::connect(t->horizontalHeader(), &QHeaderView::sectionResized, this,
                         [this](int logical, int, int) {
                             if (applying_)
                                 return;
                             if (const auto i = columns_.indexOf(logical); i >= 0) {
                                 columns_.removeAt(i);
                                 natural_.removeAt(i);
                             }
                             apply();
                         });
    }
    void set(QList<int> columns, QList<int> natural) {
        columns_ = std::move(columns);
        natural_ = std::move(natural);
        apply();
    }
    void apply() {
        if (columns_.isEmpty())
            return;
        applying_ = true;
        auto* h = table_->horizontalHeader();
        int others = 0;
        for (int c = 0; c < h->count(); ++c)
            if (!columns_.contains(c) && !h->isSectionHidden(c))
                others += h->sectionSize(c);
        int natural_sum = 0;
        for (int n : natural_)
            natural_sum += n;
        // Spare width is shared equally; a column never goes below its sized width.
        const int spare = std::max(0, table_->viewport()->width() - others - natural_sum);
        for (int i = 0; i < columns_.size(); ++i) {
            if (columns_[i] < 0 || columns_[i] >= h->count())
                continue;
            const int share = spare / int(columns_.size()) + (i < spare % int(columns_.size()) ? 1 : 0);
            h->resizeSection(columns_[i], natural_[i] + share);
        }
        applying_ = false;
    }

  protected:
    bool eventFilter(QObject* o, QEvent* e) override {
        if (o == table_->viewport() && e->type() == QEvent::Resize)
            apply();
        return false;
    }

  private:
    QTableWidget* table_;
    QList<int> columns_;
    QList<int> natural_;
    bool applying_ = false;
};
} // namespace detail

/// After sizing columns to content, share the table's spare width among the
/// text `columns` (Company, Roles …) and keep doing so as the panel resizes.
/// Content-width columns alone left a wide pane ~470px of empty grid on the
/// right; stretching the LAST column instead put a numeric header adrift
/// across the gap. A column never shrinks below its sized width — a narrow
/// pane scrolls rather than eliding names.
inline void fill_width(QTableWidget* t, std::initializer_list<int> columns) {
    // No Q_OBJECT on the filler (header-only), so look it up by name and
    // static_cast — the name is only ever given to a ColumnFiller.
    auto* found = t->findChild<QObject*>(QStringLiteral("ownershipColumnFiller"), Qt::FindDirectChildrenOnly);
    auto* f = found ? static_cast<detail::ColumnFiller*>(found) : new detail::ColumnFiller(t);
    QList<int> cols(columns), natural;
    for (int c : cols)
        natural << t->columnWidth(c);
    f->set(cols, natural);
}

inline QTableWidgetItem* cell(const QString& text, const QString& colour = {},
                              Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter) {
    auto* it = new QTableWidgetItem(text);
    it->setTextAlignment(align);
    if (!colour.isEmpty())
        it->setForeground(QColor(colour));
    return it;
}

inline QTableWidgetItem* num_cell(const QString& text, const QString& colour = {}) {
    return cell(text, colour, Qt::AlignRight | Qt::AlignVCenter);
}

/// Compact, not exact: a position value is read for its magnitude.
inline QString compact_or_dash(const std::optional<double>& v) {
    return v ? fmt::format_compact(*v) : fmt::placeholder();
}

inline QString pct_or_dash(const std::optional<double>& fraction, int dp = 1, bool sign = false) {
    return fraction ? fmt::format_percent(*fraction * 100.0, dp, sign) : fmt::placeholder();
}

inline QString date_or_dash(const QDate& d, const char* f = "yyyy-MM-dd") {
    return d.isValid() ? d.toString(QLatin1String(f)) : fmt::placeholder();
}

/// Green up, red down, the primary text colour for nothing.
inline QString signed_colour(const std::optional<double>& v) {
    if (!v)
        return {};
    return *v >= 0 ? ui::colors::GREEN() : ui::colors::RED();
}

/// The stylesheet every ownership widget applies: the tables are the screen,
/// and Qt's default palette renders them light-on-light against the
/// terminal's dark ground.
inline QString table_stylesheet() {
    return QString("QWidget{background:%1;color:%2;}"
                   "QTableWidget{background:%1;gridline-color:%3;"
                   "alternate-background-color:%6;color:%2;border:none;}"
                   // No `color` on ::item: a stylesheet colour there overrides
                   // every per-cell foreground, and the green/red/amber cells
                   // are half of what these tables say.
                   "QTableWidget::item{padding:1px 4px;}"
                   "QHeaderView::section{background:%4;color:%5;padding:4px;border:0;}"
                   "QToolTip{color:%2;background:%4;border:1px solid %3;}")
        .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_DIM(), ui::colors::BG_RAISED(),
             ui::colors::TEXT_SECONDARY(), ui::colors::BG_SURFACE());
}

/// A dim, wrapping note under a table: coverage, exclusions, what was left out.
inline QLabel* note_label() {
    auto* l = new QLabel;
    l->setWordWrap(true);
    l->setStyleSheet(QString("color:%1;font-size:12px;").arg(ui::colors::TEXT_SECONDARY()));
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return l;
}

/// A thin rule between sections.
inline QFrame* rule() {
    auto* f = new QFrame;
    f->setFrameShape(QFrame::HLine);
    f->setStyleSheet(QString("color:%1;background:%1;max-height:1px;").arg(ui::colors::BORDER_DIM()));
    return f;
}

} // namespace finterm::screens::ownership_ui
