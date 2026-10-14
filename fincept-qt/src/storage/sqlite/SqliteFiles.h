#pragma once
// Moving a SQLite database on disk, safely.
//
// A database is its main file plus whatever companions exist (-wal, -shm,
// -journal). They must move as one unit: a database that arrives without its
// WAL silently loses the transactions still in it. Companions move first and
// the main file last; if any rename fails, everything already moved is put
// back. QFile::rename copies across filesystems when it has to.

#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>

namespace fincept::sqlite_files {

inline const char* const kCompanions[] = {"-wal", "-shm", "-journal"};

/// True when no part of a database unit exists at `path`.
inline bool unit_free(const QString& path) {
    if (QFileInfo::exists(path))
        return false;
    for (const char* sfx : kCompanions)
        if (QFileInfo::exists(path + sfx))
            return false;
    return true;
}

/// Move the unit at `src` to `dst` (which must be free). False — with the
/// unit back at `src` — when any rename fails. If even putting a part back
/// fails, `*split` is set: the database and a companion are now in different
/// places, which the caller must report rather than call "left in place".
inline bool move_unit(const QString& src, const QString& dst, bool* split = nullptr) {
    QStringList done;
    for (const char* sfx : {"-wal", "-shm", "-journal", ""}) {
        if (!QFileInfo::exists(src + sfx))
            continue;
        if (!QFile::rename(src + sfx, dst + sfx)) {
            for (const QString& d : done)
                if (!QFile::rename(dst + d, src + d) && split)
                    *split = true;
            return false;
        }
        done.append(QString::fromLatin1(sfx));
    }
    return true;
}

} // namespace fincept::sqlite_files
