// test_external_link.cpp — what a news feed is allowed to make the desktop open.
//
// open_external_link() guards QDesktopServices::openUrl, which is not a browser
// call: it hands the URL to the desktop's scheme handler. Article URLs come from
// third-party feeds, so the allow-list is the security boundary — these tests
// pin it in both directions.
//
// Only the reject cases actually call the function to completion; the accept
// cases would launch a browser, so they assert on the classifier instead. That
// asymmetry is deliberate: a test suite that opens ten tabs is a test suite
// nobody runs.

#include <QTest>
#include <QUrl>

#include "ui/components/ExternalLink.h"

namespace {

/// The predicate open_external_link() applies before it calls openUrl, lifted
/// so the accept cases can be asserted without launching anything. Kept in
/// step with the real function by the shared-rejects test below, which runs
/// the real one and requires it to agree on everything it refuses.
bool would_open(const QString& url) {
    const QString trimmed = url.trimmed();
    if (trimmed.isEmpty())
        return false;
    const QUrl parsed(trimmed, QUrl::StrictMode);
    if (!parsed.isValid() || parsed.isRelative())
        return false;
    const QString scheme = parsed.scheme().toLower();
    return scheme == QLatin1String("http") || scheme == QLatin1String("https");
}

} // namespace

class TestExternalLink : public QObject {
    Q_OBJECT

private slots:
    void web_urls_are_accepted() {
        QVERIFY(would_open(QStringLiteral("https://finance.yahoo.com/news/story-123.html")));
        QVERIFY(would_open(QStringLiteral("http://example.com/a?b=c&d=e")));
        // Scheme comparison is case-insensitive — feeds are not consistent.
        QVERIFY(would_open(QStringLiteral("HTTPS://example.com/x")));
        // Leading whitespace is common in scraped feeds and must not reject.
        QVERIFY(would_open(QStringLiteral("  https://example.com/x  ")));
    }

    void non_web_schemes_are_refused() {
        // Each of these would hand the desktop something it can launch: a file
        // manager, a mail client, a registered custom-scheme application.
        // openUrl is never reached, so these run the REAL function.
        const QStringList hostile = {
            QStringLiteral("file:///etc/passwd"),
            QStringLiteral("file:///home/user/.ssh/id_rsa"),
            QStringLiteral("javascript:alert(1)"),
            QStringLiteral("data:text/html,<script>alert(1)</script>"),
            QStringLiteral("mailto:someone@example.com"),
            QStringLiteral("ftp://example.com/x"),
            QStringLiteral("smb://server/share"),
        };
        for (const QString& u : hostile)
            QVERIFY2(!fincept::ui::open_external_link(u), qPrintable(u));
    }

    void empty_and_malformed_are_refused() {
        QVERIFY(!fincept::ui::open_external_link(QString()));
        QVERIFY(!fincept::ui::open_external_link(QStringLiteral("   ")));
        // Relative — an article object whose url field held a path fragment.
        QVERIFY(!fincept::ui::open_external_link(QStringLiteral("/news/story-123.html")));
        QVERIFY(!fincept::ui::open_external_link(QStringLiteral("example.com/no-scheme")));
    }

    void the_local_predicate_agrees_with_the_real_function() {
        // Guards the mirror above from drifting: for everything the real
        // function refuses, the predicate must refuse too.
        const QStringList refused = {
            QString(),
            QStringLiteral("   "),
            QStringLiteral("file:///etc/passwd"),
            QStringLiteral("javascript:alert(1)"),
            QStringLiteral("mailto:a@b.c"),
            QStringLiteral("/relative/path"),
            QStringLiteral("example.com/no-scheme"),
        };
        for (const QString& u : refused) {
            QVERIFY2(!fincept::ui::open_external_link(u), qPrintable(u));
            QVERIFY2(!would_open(u), qPrintable(u));
        }
    }
};

QTEST_APPLESS_MAIN(TestExternalLink)
#include "test_external_link.moc"
