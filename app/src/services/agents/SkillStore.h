#pragma once
// SkillStore.h — where the agents' SKILL.md files live.
//
// The shipped ones are part of the program: read-only in an AppImage, and
// replaced with it by an update. One the user changes is therefore kept as
// their own copy, under their profile's data, and wins over the shipped one.

#include <QByteArray>
#include <QString>

namespace finterm::services::skill_store {

/// The shipped skills tree, canonical. Empty when there is none.
QString shipped_root();
/// The user's copies, laid out as the shipped tree. May not exist.
QString user_root();

/// `path`, a file, relative to `root`. Empty when it isn't there or lies
/// outside, also by way of a symlink.
QString relative_to(const QString& root, const QString& path);

/// The file in use for a skill, by its path within the tree: the user's copy
/// where there is one, else the shipped one.
QString in_use(const QString& rel);
/// Where the user's copy of it is (or would be).
QString user_copy(const QString& rel);

/// Keep `content` as the user's copy. On failure `error` says why and what
/// was there before is untouched.
bool save(const QString& rel, const QByteArray& content, QString* error);

} // namespace finterm::services::skill_store
