// src/screens/futures/FuturesPanels.h
#pragma once
// The Futures screen's panes: each one titled, carrying the source of its
// data, and movable — drag a pane by its title bar onto another and the two
// swap places. Sizes and places are kept between sessions by the screen.

#include <QPoint>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <algorithm>

class QLabel;
class QLayout;
class QHBoxLayout;

namespace finterm::screens::futures {

/// The drag payload that names a pane.
inline const char* panel_mime() {
    return "application/x-finterm-futures-panel";
}

/// A saved arrangement is used only when it names exactly the panes this
/// build has, each once; anything else (an older build, a damaged setting)
/// falls back to the default rather than losing a pane.
inline QStringList valid_order(const QStringList& saved, const QStringList& defaults) {
    return std::is_permutation(saved.begin(), saved.end(), defaults.begin(), defaults.end()) ? saved : defaults;
}

class PanelFrame : public QWidget {
    Q_OBJECT
  public:
    /// `title_row` becomes the pane's title bar; its first widget should be
    /// the title. The source label is placed right after it.
    PanelFrame(const QString& id, QHBoxLayout* title_row, QWidget* body, QWidget* parent = nullptr);

    QString id() const { return id_; }
    /// Where the pane's numbers come from, shown dimmed beside the title.
    void set_source(const QString& text, const QString& detail = {});

  signals:
    void swap_requested(const QString& dragged_id, const QString& onto_id);

  protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragLeaveEvent(QDragLeaveEvent* e) override;
    void dropEvent(QDropEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

  private:
    void set_drop_mark(bool on);

    bool is_handle(QObject* obj) const;

    QString id_;
    QWidget* bar_ = nullptr;
    QList<QObject*> handles_; ///< the bar and its labels: a press on any of them starts a drag
    QLabel* source_ = nullptr;
    QWidget* drop_mark_ = nullptr;
    QPoint press_;
};

} // namespace finterm::screens::futures
