// tests/services/test_update_policy.cpp
//
// The updater's decisions: which version is newer, what the manifest offers
// this platform, and when the daily automatic check is due. The manifest
// fixture has the shape release.yml writes into updates.json.

#include "services/updater/UpdatePolicy.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTimeZone>
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

    void a_version_released_again_supersedes_the_older_build() {
        const auto t = [](int h) { return QDateTime(QDate(2026, 11, 24), QTime(h, 0), QTimeZone::UTC); };
        const Release old_build{"4.1.0", "aaaaaaaaaaaaaaaa", t(5)};
        const Release new_build{"4.1.0", "bbbbbbbbbbbbbbbb", t(16)};
        QVERIFY(supersedes(new_build, old_build));
        QVERIFY(!supersedes(old_build, new_build)); // never back to an older one
        QVERIFY(!supersedes(old_build, old_build));
        QVERIFY(!supersedes({"4.1.0", "bbbb", t(5)}, old_build)); // no later
        QVERIFY(!supersedes({"4.1.0", {}, {}}, old_build));       // a manifest without commits
        QVERIFY(!supersedes(new_build, {"4.1.0", {}, {}}));       // a build that can't say which it is
        QVERIFY(!supersedes({"4.1.0", "bbbb", {}}, old_build));
        QVERIFY(supersedes({"4.1.1", {}, {}}, new_build)); // a newer version needs no commit
        QVERIFY(!supersedes({"4.0.9", "cccc", t(20)}, new_build));
        QVERIFY(!supersedes({"4.1.0-dev", "bbbb", t(16)}, {"4.1.0-dev", "aaaa", t(5)}));
    }

    void releases_are_told_apart_by_id() {
        const Release r{"4.1.0", "023279470ab1cdef0123", {}};
        QCOMPARE(r.id(), QString("4.1.0+023279470ab1"));
        QCOMPARE((Release{"4.1.0", {}, {}}).id(), QString("4.1.0"));
        QCOMPARE(version_of(r.id()), QString("4.1.0"));
        QCOMPARE(version_of("4.1.0"), QString("4.1.0"));
        QVERIFY(same_release("4.1.0+aa", "4.1.0+aa"));
        QVERIFY(!same_release("4.1.0+aa", "4.1.0+bb")); // an update and what it replaced
        QVERIFY(same_release("4.1.1", "4.1.1+bb"));     // put in by an updater that knew no commits
        QVERIFY(!same_release("4.1.0", "4.1.1+bb"));
    }

    void the_build_of_an_offer_is_read_and_kept() {
        Offer o;
        QCOMPARE(parse_manifest(manifest({{"latest-version", "4.1.0"},
                                          {"download-url", "https://example.invalid/l.run"},
                                          {"commit", "023279470AB1"},
                                          {"committed", "2026-11-24T16:12:00Z"}}),
                                "linux-x64", o),
                 ManifestResult::Ok);
        QCOMPARE(o.id(), QString("4.1.0+023279470ab1"));
        QCOMPARE(o.committed, QDateTime(QDate(2026, 11, 24), QTime(16, 12), QTimeZone::UTC));
        const Offer kept = offer_from_json(offer_to_json(o));
        QCOMPARE(kept.id(), o.id());
        QCOMPARE(kept.committed, o.committed);
        // An offer stored, or a manifest written, before commits were in them.
        const Offer before = offer_from_json("{\"version\":\"4.1.1\",\"download-url\":\"x\"}");
        QCOMPARE(before.id(), QString("4.1.1"));
        QVERIFY(!before.committed.isValid());
    }

    void manifest_entry_for_this_platform() {
        Offer o;
        const auto r = parse_manifest(manifest({{"latest-version", " 4.0.11 "},
                                                {"download-url", "https://example.invalid/l.run"},
                                                {"sha256", " 34D8 "},
                                                {"open-url", "https://example.invalid/tag"},
                                                {"inplace-url", "https://example.invalid/l.zip"},
                                                {"inplace-sha256", "AB12 "},
                                                {"changelog", "notes"}}),
                                      "linux-x64", o);
        QCOMPARE(r, ManifestResult::Ok);
        QCOMPARE(o.version, QString("4.0.11"));
        QCOMPARE(o.download_url, QString("https://example.invalid/l.run"));
        QCOMPARE(o.sha256, QString("34d8")); // compared lower-case to the computed hex
        QCOMPARE(o.open_url, QString("https://example.invalid/tag"));
        QCOMPARE(o.changelog, QString("notes"));
        QCOMPARE(o.inplace_url, QString("https://example.invalid/l.zip"));
        QCOMPARE(o.inplace_sha256, QString("ab12"));
        QCOMPARE(offer_from_json(offer_to_json(o)).inplace_url, o.inplace_url);
        QCOMPARE(offer_from_json(offer_to_json(o)).inplace_sha256, o.inplace_sha256);
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
        // An error body served with 200 is not "nothing for this platform".
        QCOMPARE(parse_manifest("{\"message\":\"Not Found\"}", "linux-x64", o), ManifestResult::Malformed);
        QCOMPARE(parse_manifest("{\"updates\":[]}", "linux-x64", o), ManifestResult::Malformed);
        QVERIFY(o.version.isEmpty());
    }

    void stored_offer_round_trips() {
        Offer o{{"4.0.11", {}, {}}, "https://example.invalid/l.run", "34d8", "https://example.invalid/tag", "notes", {}, {}};
        const Offer back = offer_from_json(offer_to_json(o));
        QCOMPARE(back.version, o.version);
        QCOMPARE(back.download_url, o.download_url);
        QCOMPARE(back.sha256, o.sha256);
        QCOMPARE(back.open_url, o.open_url);
        QCOMPARE(back.changelog, o.changelog);
        QVERIFY(offer_from_json("").version.isEmpty());
        QVERIFY(offer_from_json("garbage").version.isEmpty());
        Offer bad = o;
        bad.version = "4.0.11-dev";
        QVERIFY(offer_from_json(offer_to_json(bad)).version.isEmpty());
        bad = o;
        bad.download_url.clear();
        QVERIFY(offer_from_json(offer_to_json(bad)).version.isEmpty());
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
