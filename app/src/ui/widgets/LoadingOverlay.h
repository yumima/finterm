// src/ui/widgets/LoadingOverlay.h
#pragma once
#include "ui/theme/ThemeTokens.h"

#include <QLabel>
#include <QTimer>
#include <QWidget>

namespace finterm::ui {

/// Loading overlay with pulsing bar animation.
/// Colors update automatically when the theme changes.
/// Usage:
///   1. Create as child of the content widget: new LoadingOverlay(content_widget)
///   2. Call show_loading("message") to display, hide_loading() to dismiss.
class LoadingOverlay : public QWidget {
    Q_OBJECT
  public:
    explicit LoadingOverlay(QWidget* parent = nullptr);

    void show_loading(const QString& message = "LOADING\xe2\x80\xa6");
    /// Same veil, message only — no bars, no animation timer. For a state
    /// that is not in progress (a failed fetch), so it neither claims work is
    /// happening nor repaints 10x a second for as long as it stands.
    void show_message(const QString& message);
    void hide_loading();
    /// For a view that re-renders while it waits: shows the bars when `on`
    /// (keeping a running animation as it is — show_loading restarts it, a
    /// stutter on every repaint of the view), hides them when not.
    void set_loading(bool on, const QString& message = "LOADING\xe2\x80\xa6");

  protected:
    void paintEvent(QPaintEvent* e) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    QTimer* anim_timer_ = nullptr;
    QString message_;
    int tick_ = 0;
    bool animated_ = true;
    ThemeTokens tokens_{};

    static constexpr int BAR_COUNT = 5;
    static constexpr int ANIM_INTERVAL_MS = 100;
};

} // namespace finterm::ui
