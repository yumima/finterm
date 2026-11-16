// src/screens/futures/FuturesPanels.cpp
#include "screens/futures/FuturesPanels.h"

#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>
#include <QPixmap>
#include <QTimer>
#include <QVBoxLayout>

namespace finterm::screens::futures {

PanelFrame::PanelFrame(const QString& id, QHBoxLayout* title_row, QWidget* body, QWidget* parent)
    : QWidget(parent), id_(id) {
    setObjectName(QStringLiteral("futPanel"));
    setAcceptDrops(true);
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    bar_ = new QWidget(this);
    bar_->setObjectName(QStringLiteral("futPanelBar"));
    bar_->setFixedHeight(26);
    bar_->setCursor(Qt::OpenHandCursor);
    bar_->setToolTip(QStringLiteral("Drag onto another panel to swap places"));
    bar_->installEventFilter(this);
    source_ = new QLabel(bar_);
    source_->setObjectName(QStringLiteral("futSource"));
    title_row->insertWidget(title_row->count() > 0 ? 1 : 0, source_);
    // The whole bar is the handle, labels included — watched rather than made
    // transparent, so their own tooltips (the source's detail, the chart's
    // note on roll gaps) still show. Buttons in the bar keep their clicks.
    handles_ << bar_;
    for (int i = 0; i < title_row->count(); ++i)
        if (auto* l = qobject_cast<QLabel*>(title_row->itemAt(i)->widget())) {
            l->installEventFilter(this);
            handles_ << l;
        }
    bar_->setLayout(title_row);
    title_row->setContentsMargins(8, 0, 6, 0);
    title_row->setSpacing(6);
    v->addWidget(bar_);
    v->addWidget(body, 1);

    // Drawn over the children while another pane is dragged over this one.
    drop_mark_ = new QWidget(this);
    drop_mark_->setObjectName(QStringLiteral("futDropMark"));
    drop_mark_->setAttribute(Qt::WA_TransparentForMouseEvents);
    drop_mark_->hide();
}

void PanelFrame::set_source(const QString& text, const QString& detail) {
    source_->setText(text.isEmpty() ? QString() : QStringLiteral("· %1").arg(text));
    source_->setToolTip(detail);
}

bool PanelFrame::is_handle(QObject* obj) const {
    return handles_.contains(obj);
}

bool PanelFrame::eventFilter(QObject* obj, QEvent* event) {
    if (!is_handle(obj))
        return QWidget::eventFilter(obj, event);
    if (event->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton)
            press_ = me->globalPosition().toPoint();
    } else if (event->type() == QEvent::MouseMove) {
        auto* me = static_cast<QMouseEvent*>(event);
        if (!(me->buttons() & Qt::LeftButton) || press_.isNull() ||
            (me->globalPosition().toPoint() - press_).manhattanLength() < QApplication::startDragDistance())
            return false;
        press_ = {};
        auto* mime = new QMimeData;
        mime->setData(QString::fromLatin1(panel_mime()), id_.toUtf8());
        auto* drag = new QDrag(this);
        drag->setMimeData(mime);
        const QPixmap shot = grab().scaledToWidth(qMax(120, width() / 4), Qt::SmoothTransformation);
        drag->setPixmap(shot);
        drag->setHotSpot(QPoint(shot.width() / 2, 8));
        drag->exec(Qt::MoveAction);
        return true;
    } else if (event->type() == QEvent::MouseButtonRelease) {
        press_ = {};
    }
    return false;
}

void PanelFrame::dragEnterEvent(QDragEnterEvent* e) {
    const QString mime = QString::fromLatin1(panel_mime());
    if (!e->mimeData()->hasFormat(mime) || QString::fromUtf8(e->mimeData()->data(mime)) == id_) {
        e->ignore();
        return;
    }
    e->acceptProposedAction();
    set_drop_mark(true);
}

void PanelFrame::dragLeaveEvent(QDragLeaveEvent* e) {
    set_drop_mark(false);
    QWidget::dragLeaveEvent(e);
}

void PanelFrame::dropEvent(QDropEvent* e) {
    set_drop_mark(false);
    const QString from = QString::fromUtf8(e->mimeData()->data(QString::fromLatin1(panel_mime())));
    if (from.isEmpty() || from == id_)
        return;
    e->acceptProposedAction();
    // After the drag has finished: the swap re-parents the pane being
    // dragged, which must not happen while its drag is still running.
    QTimer::singleShot(0, this, [this, from]() { emit swap_requested(from, id_); });
}

void PanelFrame::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    drop_mark_->setGeometry(rect());
}

void PanelFrame::set_drop_mark(bool on) {
    drop_mark_->setGeometry(rect());
    drop_mark_->setVisible(on);
    if (on)
        drop_mark_->raise();
}

} // namespace finterm::screens::futures
