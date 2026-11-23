#include "screens/dashboard/canvas/SelectionLink.h"

#include "screens/dashboard/canvas/DashboardCanvas.h"

#include <QEvent>
#include <QItemSelectionModel>
#include <QPointer>
#include <QTableWidget>
#include <QTimer>

namespace finterm::screens {

SelectionLink* SelectionLink::of(QWidget* w) {
    for (; w; w = w->parentWidget())
        if (auto* canvas = qobject_cast<DashboardCanvas*>(w)) {
            if (auto* link = canvas->findChild<SelectionLink*>(QString(), Qt::FindDirectChildrenOnly))
                return link;
            return new SelectionLink(canvas);
        }
    return nullptr;
}

void SelectionLink::select(const QString& symbol, const QString& label) {
    symbol_ = symbol;
    label_ = label;
    picked_ = true;
    emit selected(symbol, label);
    emit marked(symbol);
}

void SelectionLink::reset() {
    symbol_.clear();
    label_.clear();
    picked_ = false;
    emit marked(QString());
}

void SelectionLink::seed(const QString& symbol, const QString& label) {
    // A click outranks a chart's own symbol; a later chart's seed replaces an
    // earlier one (the chart that seeded may have been closed).
    if (picked_ || symbol.isEmpty() || symbol == symbol_)
        return;
    symbol_ = symbol;
    label_ = label;
    emit marked(symbol);
}

namespace {

/// Keeps one table's mark in step with its canvas's link (a child of the
/// table: it goes with it).
class RowFollower : public QObject {
  public:
    RowFollower(QTableWidget* table, std::function<QString(int)> symbol_of)
        : QObject(table), table_(table), symbol_of_(std::move(symbol_of)) {
        table->installEventFilter(this);
        auto* model = table->model();
        // Rows rebuilt or re-filled lose (or misplace) the mark: re-mark once
        // the table has settled.
        connect(model, &QAbstractItemModel::modelReset, this, [this]() { remark_later(); });
        connect(model, &QAbstractItemModel::layoutChanged, this, [this]() { remark_later(); });
        connect(model, &QAbstractItemModel::rowsInserted, this, [this]() { remark_later(); });
        connect(model, &QAbstractItemModel::rowsRemoved, this, [this]() { remark_later(); });
        // Rows re-filled in place (a reused row pool, a list reordered).
        connect(model, &QAbstractItemModel::dataChanged, this, [this]() { remark_later(); });
        // A click on a row that is not a selection (the "+ add" row, a blank
        // pool row) must not move the mark: once the click is done (the
        // widget's own handler has run), the link decides it. On the press
        // itself the row the user is pressing shows the band — feedback.
        connect(table, &QTableWidget::cellClicked, this, [this]() { remark_later(); });
        if (table->isVisible())
            attach();
    }

    bool eventFilter(QObject* obj, QEvent* e) override {
        if (obj == table_ && e->type() == QEvent::Show)
            attach();
        return QObject::eventFilter(obj, e);
    }

  private:
    void attach() {
        if (!link_) {
            link_ = SelectionLink::of(table_);
            if (!link_)
                return;
            connect(link_, &SelectionLink::marked, this, [this](const QString&) { mark(); });
        }
        mark();
    }
    void remark_later() {
        if (!pending_) {
            pending_ = true;
            QTimer::singleShot(0, this, [this]() {
                pending_ = false;
                mark();
            });
        }
    }
    void mark() {
        if (!table_ || !link_)
            return;
        const QString sym = link_->symbol();
        int hit = -1;
        if (!sym.isEmpty())
            for (int r = 0; r < table_->rowCount() && hit < 0; ++r)
                if (symbol_of_(r) == sym)
                    hit = r;
        auto* sel = table_->selectionModel();
        if (!sel)
            return;
        const auto rows = sel->selectedRows();
        if (hit < 0) {
            if (!rows.isEmpty())
                sel->clearSelection();
        } else if (rows.size() != 1 || rows.first().row() != hit) {
            // select(), not selectRow(): selectRow moves the current index,
            // and the view scrolls to it — a reader scrolled down a long list
            // was yanked back to the mark on every refresh.
            sel->select(table_->model()->index(hit, 0),
                        QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        }
    }

    QPointer<QTableWidget> table_;
    QPointer<SelectionLink> link_;
    std::function<QString(int)> symbol_of_;
    bool pending_ = false;
};

} // namespace

void SelectionLink::follow_rows(QTableWidget* table, std::function<QString(int row)> symbol_of) {
    if (table)
        new RowFollower(table, std::move(symbol_of));
}

} // namespace finterm::screens
