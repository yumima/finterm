#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>

/// The update service's decisions, kept apart from its network and dialogs so
/// they can be tested: which version is newer, what the manifest offers this
/// platform, and when the next automatic check is due.
namespace finterm::services::update_policy {

/// How often an automatic check runs. The time of the last successful check is
/// stored, so this is measured across restarts: relaunching often does not
/// mean checking often.
inline constexpr qint64 kCheckIntervalSecs = 24 * 60 * 60;

/// A release: its version, and which build of it — a version can be released
/// again from a newer commit. Releases before 4.1.1 name no commit.
struct Release {
    QString version;
    QString commit;      ///< full hash; empty when unknown
    QDateTime committed; ///< the commit's time; invalid when unknown

    /// What tells one release from another, everywhere but in what people
    /// read: "4.1.0+023279470ab1", or the version where the commit is unknown.
    QString id() const;
};

/// The version of a Release::id().
QString version_of(const QString& id);

/// Two ids name the same release, as far as both say: an updater that knew no
/// commits wrote the version alone.
bool same_release(const QString& a, const QString& b);

/// `remote` should replace `local`: a newer version, or the same version
/// released again from a newer commit (both commits known).
bool supersedes(const Release& remote, const Release& local);

/// What the manifest offers the running platform.
struct Offer : Release {
    QString download_url;
    QString sha256; ///< lower-case hex; empty when the manifest carries none
    QString open_url;
    QString changelog;
    /// The package that replaces an install in place, where that is not the
    /// download itself (Windows: a zip of the app beside the installer).
    QString inplace_url;
    QString inplace_sha256;
};

enum class ManifestResult {
    Ok,         ///< `out` is filled
    Malformed,  ///< not a JSON object
    NoEntry,    ///< no entry for this platform — nothing on offer here
    Incomplete, ///< the entry lacks latest-version or download-url
};

/// Strict "major.minor.patch". Anything else (dev builds, suffixes) is false.
bool parse_version(const QString& s, int& major, int& minor, int& patch);

/// True iff `remote` is strictly newer than `local`. A version that doesn't
/// parse on either side is never newer.
bool is_newer(const QString& local, const QString& remote);

/// Read the entry for `platform_key` from an updates.json body. A JSON object
/// without an "updates" object (a proxy's or GitHub's error JSON served with
/// 200) is Malformed, not "nothing for this platform".
ManifestResult parse_manifest(const QByteArray& body, const QString& platform_key, Offer& out);

/// The offer as stored between runs (and read by a second running profile,
/// which shares the daily check). from_json yields an empty version for
/// anything unusable.
QByteArray offer_to_json(const Offer& o);
Offer offer_from_json(const QByteArray& json);

/// Whether an automatic check is due at `now`, given the last successful one.
/// Never checked (invalid `last`) is due. A `last` in the future means the
/// clock was set back; that is due too, or the check could stop for as long as
/// the clock was wrong.
bool check_due(const QDateTime& last, const QDateTime& now);

} // namespace finterm::services::update_policy
