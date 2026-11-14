// test_chat_panel_size.cpp — the AI assistant panel's default and clamp rule.
// Default is twice the original 320x440 panel, but never more than 60% of the
// window in either axis, and never below the size the header + input fit in.

#include "ai_chat/ChatPanelSize.h"

#include <QTest>

using namespace finterm::ai_chat::panel_size;

class TestChatPanelSize : public QObject {
    Q_OBJECT
  private slots:
    void default_is_double_on_a_big_window() {
        QCOMPARE(default_panel_size(QSize(2560, 1600)), QSize(640, 880));
    }
    void default_clamps_to_sixty_percent() {
        // 1600x900 window, dock area ~1600x780: height capped at 468.
        QCOMPARE(default_panel_size(QSize(1600, 780)), QSize(640, 468));
        QCOMPARE(default_panel_size(QSize(900, 700)), QSize(540, 420));
    }
    void minimum_wins_on_a_tiny_host() {
        QCOMPARE(default_panel_size(QSize(200, 200)), QSize(MIN_PANEL_W, MIN_PANEL_H));
        QCOMPARE(clamp_panel_size(QSize(10, 10), QSize(2000, 2000)), QSize(MIN_PANEL_W, MIN_PANEL_H));
    }
    void user_size_is_kept_when_it_fits() {
        QCOMPARE(clamp_panel_size(QSize(700, 500), QSize(1920, 1000)), QSize(700, 500));
        // A size saved on a bigger monitor is clamped on a smaller one.
        QCOMPARE(clamp_panel_size(QSize(1500, 1000), QSize(1280, 700)), QSize(768, 420));
    }
};

QTEST_APPLESS_MAIN(TestChatPanelSize)
#include "test_chat_panel_size.moc"
