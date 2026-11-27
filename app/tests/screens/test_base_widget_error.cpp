// test_base_widget_error.cpp — BaseWidget::set_error() must not destroy content.
//
// It used to delete every widget in the content layout and put an error label
// in their place, so every dashboard widget that kept a pointer into its body
// (most of them) held dangling pointers after a fetch failure. Eight widgets
// grew their own workaround — null-and-rebuild, inline error rows, avoiding
// set_error entirely. The error is now a separate page; this pins it.

#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QVBoxLayout>

#include "screens/dashboard/widgets/BaseWidget.h"

using finterm::screens::widgets::BaseWidget;

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

    // A failed load says why and what to do, offers Retry (the widget's own
    // refresh) and keeps the raw error behind Details.
    void load_error_explains() {
        BaseWidget w(QStringLiteral("TEST"));
        QSignalSpy refresh(&w, &BaseWidget::refresh_requested);
        w.show();
        const QString raw = QStringLiteral("Yahoo refused: YFRateLimitError('Too Many Requests. Rate limited.')");
        w.set_load_error(QStringLiteral("quotes"), raw);
        QVERIFY(w.has_error());
        QStringList shown;
        QLabel* detail = nullptr;
        for (QLabel* l : w.findChildren<QLabel*>()) {
            if (l->text() == raw)
                detail = l;
            else if (l->isVisible())
                shown << l->text();
        }
        const QString all = shown.join(QLatin1Char('\n'));
        QVERIFY2(all.contains(QStringLiteral("Couldn't load quotes")), qPrintable(all));
        QVERIFY2(all.contains(QStringLiteral("HTTP 429")), qPrintable(all));
        QVERIFY2(all.contains(QStringLiteral("What to do:")), qPrintable(all));
        QVERIFY2(detail && !detail->isVisible(), "the raw error is behind Details");
        QPushButton* retry = nullptr;
        QPushButton* details = nullptr;
        for (QPushButton* b : w.findChildren<QPushButton*>()) {
            if (b->text() == QLatin1String("Retry"))
                retry = b;
            if (b->text() == QLatin1String("Details"))
                details = b;
        }
        QVERIFY(retry && details);
        details->click();
        QVERIFY(detail->isVisible());
        retry->click();
        QCOMPARE(refresh.count(), 1);
        // A plain message afterwards doesn't keep the old reason around.
        w.set_error(QStringLiteral("Not configured"));
        for (QLabel* l : w.findChildren<QLabel*>())
            if (l->isVisible())
                QVERIFY2(!l->text().contains(QStringLiteral("HTTP 429")), qPrintable(l->text()));
    }

    void empty_message_is_ignored() {
        BaseWidget w(QStringLiteral("TEST"));
        w.set_error(QString());
        QVERIFY(!w.has_error());
    }
};

QTEST_MAIN(TestBaseWidgetError)
#include "test_base_widget_error.moc"
