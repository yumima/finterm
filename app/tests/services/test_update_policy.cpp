// tests/services/test_update_policy.cpp
//
// The updater's decisions: which version is newer, what the manifest offers
// this platform, and when the daily automatic check is due. The manifest
// fixture has the shape release.yml writes into updates.json.

#include "services/updater/UpdatePolicy.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest/QtTest>

using namespace finterm::services::update_policy;

namespace {

/// An updates.json body with a Windows entry and `linux_entry` for linux-x64.
QByteArray manifest(const QJsonObject& linux_entry) {
    const QJsonObject win{{"latest-version", "4.0.10"},
                          {"download-url", "https://example.invalid/w.exe"},
                          {"sha256", "AB"}};
    const QJsonObject updates{{"windows-x64", win}, {"linux-x64", linux_entry}};
    return QJsonDocument(QJsonObject{{"schema-version", 2}, {"updates", updates}}).toJson();
}

} // namespace

class TestUpdatePolicy : public QObject {
    Q_OBJECT
  private slots:
    void newer_compares_numerically() {
        QVERIFY(is_newer("4.0.9", "4.0.10")); // not a string compare
        QVERIFY(is_newer("4.0.10", "4.1.0"));
        QVERIFY(is_newer("4.9.9", "5.0.0"));
        QVERIFY(!is_newer("4.0.10", "4.0.10"));
        QVERIFY(!is_newer("4.0.10", "4.0.9"));
        QVERIFY(!is_newer("5.0.0", "4.99.99"));
    }

    void unparsable_versions_are_never_newer() {
        QVERIFY(!is_newer("4.0.10-dev", "9.0.0")); // a dev build never offers
        QVERIFY(!is_newer("dev", "9.0.0"));
        QVERIFY(!is_newer("4.0.10", "4.0.11-rc1"));
        QVERIFY(!is_newer("4.0.10", ""));
        QVERIFY(!is_newer("4.0.10", "v4.0.11"));
    }

    void manifest_entry_for_this_platform() {
        Offer o;
        const auto r = parse_manifest(manifest({{"latest-version", " 4.0.11 "},
                                                {"download-url", "https://example.invalid/l.run"},
                                                {"sha256", " 34D8 "},
                                                {"open-url", "https://example.invalid/tag"},
                                                {"changelog", "notes"}}),
                                      "linux-x64", o);
        QCOMPARE(r, ManifestResult::Ok);
        QCOMPARE(o.version, QString("4.0.11"));
        QCOMPARE(o.download_url, QString("https://example.invalid/l.run"));
        QCOMPARE(o.sha256, QString("34d8")); // compared lower-case to the computed hex
        QCOMPARE(o.open_url, QString("https://example.invalid/tag"));
        QCOMPARE(o.changelog, QString("notes"));
    }

    void manifest_without_this_platform() {
        Offer o;
        QCOMPARE(parse_manifest(manifest({{"latest-version", "4.0.11"}, {"download-url", "x"}}), "macos-x64", o),
                 ManifestResult::NoEntry);
        QCOMPARE(parse_manifest(manifest({{"latest-version", "4.0.11"}, {"download-url", "x"}}), "", o),
                 ManifestResult::NoEntry);
        QVERIFY(o.version.isEmpty()); // untouched on failure
    }

    void manifest_incomplete_or_malformed() {
        Offer o;
        QCOMPARE(parse_manifest(manifest({{"latest-version", "4.0.11"}}), "linux-x64", o),
                 ManifestResult::Incomplete);
        QCOMPARE(parse_manifest(manifest({{"download-url", "x"}}), "linux-x64", o), ManifestResult::Incomplete);
        QCOMPARE(parse_manifest("<html>rate limited</html>", "linux-x64", o), ManifestResult::Malformed);
        QCOMPARE(parse_manifest("[]", "linux-x64", o), ManifestResult::Malformed);
        QVERIFY(o.version.isEmpty());
    }

    void daily_check_due() {
        const QDateTime now(QDate(2026, 11, 23), QTime(9, 0), QTimeZone::UTC);
        QVERIFY(check_due(QDateTime(), now));                         // never checked
        QVERIFY(!check_due(now.addSecs(-60), now));                   // just checked
        QVERIFY(!check_due(now.addSecs(-kCheckIntervalSecs + 1), now)); // a second short of a day
        QVERIFY(check_due(now.addSecs(-kCheckIntervalSecs), now));      // a day
        QVERIFY(check_due(now.addDays(-9), now));                     // laptop shut for a week
        QVERIFY(check_due(now.addSecs(3600), now)); // clock set back: due, not stuck for the gap
    }
};

QTEST_GUILESS_MAIN(TestUpdatePolicy)
#include "test_update_policy.moc"
