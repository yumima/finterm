#pragma once

#include "screens/knowledge/KnowledgeTypes.h"

#include <QHash>
#include <QObject>
#include <QString>

namespace fincept::knowledge {

/// Loads the  KNOWLEDGE content tree from disk and provides lookup, search,
/// and lazy markdown body loading. Singleton — owned by main app.
class ContentLoader : public QObject {
    Q_OBJECT
  public:
    static ContentLoader& instance();

    /// Walk root_dir/<category>/_index.json files and build the in-memory index.
    /// Also loads root_dir/abbreviations.json. Idempotent.
    void load(const QString& root_dir);

    bool is_loaded() const { return loaded_; }
    QString root_dir() const { return root_; }

    QVector<KnowledgeCategory> categories() const;
    const KnowledgeCategory* category(const QString& id) const;
    const KnowledgeEntry* entry(const QString& id) const;

    /// Resolve user-typed alias (e.g. "p/e", "price to earnings") to a canonical entry id.
    QString resolve_alias(const QString& alias_or_id) const;

    /// Fuzzy search across title + aliases + tags + abbreviation. Returns ids ranked by relevance.
    QStringList search(const QString& query, int limit = 25) const;

    /// Read the markdown body for an entry. Cached after first read.
    QString load_body(const QString& entry_id) const;

    /// Flat abbreviation lookup (acronym → expansion).
    const QHash<QString, QString>& abbreviations() const { return abbreviations_; }

  signals:
    void loaded();

  private:
    explicit ContentLoader(QObject* parent = nullptr) : QObject(parent) {}

    bool load_category(const QString& category_dir);
    static KnowledgeEntry parse_entry(const QJsonObject& obj, const QString& category_id);

    QString root_;
    bool loaded_ = false;
    QHash<QString, KnowledgeCategory> categories_;
    QStringList category_order_;
    QHash<QString, const KnowledgeEntry*> entry_index_;   // id → entry*
    QHash<QString, QString> alias_index_;                // alias (lower) → id
    QHash<QString, QString> abbreviations_;              // acronym → expansion
    mutable QHash<QString, QString> body_cache_;
};

} // namespace fincept::knowledge
