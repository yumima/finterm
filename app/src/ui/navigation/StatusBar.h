#pragma once
#include <QHBoxLayout>
#include <QLabel>
#include <QList>
#include <QPointer>
#include <QWidget>

namespace finterm::ui {

class NotifBell;
class NotifPanel;
class NotifToast;

/// The app's one bottom status bar — build version, feed indicators, ready
/// status and the notification bell. (The dashboard used to carry a second
/// bar of its own that repeated the version and feeds; the bell was its only
/// unique content, so it lives here now and is reachable from every screen.)
class StatusBar : public QWidget {
    Q_OBJECT
  public:
    explicit StatusBar(QWidget* parent = nullptr);
    void set_ready(bool ready);
    /// Append a widget at the far right (after the notification bell) — the
    /// AI assistant launcher lives here so it never overlays screen content.
    void add_trailing_widget(QWidget* w);

  private:
    static StatusBar* primary_bar();
    static QList<QPointer<StatusBar>>& all_bars();
    void refresh_theme();
    void toggle_notif_panel();
    QLabel* ready_label_ = nullptr;
    NotifBell* notif_bell_ = nullptr;
    NotifPanel* notif_panel_ = nullptr;
    /// The slide-in toast for every notification, over this window — made on
    /// the first one, shown only while the shell is (not on the lock screen).
    NotifToast* notif_toast_ = nullptr;
    QHBoxLayout* layout_ = nullptr;
};

} // namespace finterm::ui
