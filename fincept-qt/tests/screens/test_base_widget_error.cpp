// test_base_widget_error.cpp — BaseWidget::set_error() must not destroy content.
//
// It used to delete every widget in the content layout and put an error label
// in their place, so every dashboard widget that kept a pointer into its body
// (most of them) held dangling pointers after a fetch failure. Eight widgets
// grew their own workaround — null-and-rebuild, inline error rows, avoiding
// set_error entirely. The error is now a separate page; this pins it.

#include <QLabel>
#include <QPointer>
#include <QTest>
#include <QVBoxLayout>

#include "screens/dashboard/widgets/BaseWidget.h"

using fincept::screens::widgets::BaseWidget;

class TestBaseWidgetError : public QObject {
    Q_OBJECT
  private slots:
    void error_keeps_content_alive() {
        BaseWidget w(QStringLiteral("TEST"));
        QPointer<QLabel> child = new QLabel(QStringLiteral("data"));
        w.content_layout()->addWidget(child);
        w.show();

        w.set_error(QStringLiteral("Quotes unavailable"));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); // a deleteLater would land here
        QVERIFY2(child, "set_error() destroyed the widget's content");
        QVERIFY(w.has_error());
        QVERIFY(!child->isVisible()); // covered by the error page

        child->setText(QStringLiteral("updated while the error shows")); // pointers stay usable
        w.clear_error();
        QVERIFY(!w.has_error());
        QVERIFY(child->isVisible());
        QCOMPARE(child->text(), QStringLiteral("updated while the error shows"));
    }

    void empty_message_is_ignored() {
        BaseWidget w(QStringLiteral("TEST"));
        w.set_error(QString());
        QVERIFY(!w.has_error());
    }
};

QTEST_MAIN(TestBaseWidgetError)
#include "test_base_widget_error.moc"
