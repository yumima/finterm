#pragma once

#include "screens/knowledge/KnowledgeTypes.h"

#include <QHash>
#include <QObject>
#include <QString>

namespace finterm::knowledge {

/// Loads the  KNOWLEDGE content tree from disk and provides lookup, search,
/// and lazy markdown body loading. Singleton — owned by main app.
class ContentLoader : public QObject {
    Q_OBJECT
  public:
    static ContentLoader& instance();

    /// The content shipped with the app (resources/knowledge beside the
    /// binary, or share/finterm/knowledge in an install).
    static QString default_root();

    /// Walk root_dir/<category>/_index.json files and build the in-memory index.
    /// Also loads root_dir/abbreviations.json. instance() has already loaded
    /// default_root(): this is for reloading from somewhere else.
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

    /// Ask the KNOWLEDGE screen to show an entry (a HelpHint's "Open in
    /// KNOWLEDGE"). Held until a screen takes it: the screen may not exist
    /// yet when this is asked — switching to it builds it.
    void request_open(const QString& entry_id);
    QString take_pending_open();

  signals:
    void loaded();
    void open_requested(const QString& entry_id);

  private:
    explicit ContentLoader(QObject* parent = nullptr) : QObject(parent) {}

    bool load_category(const QString& category_dir);
    static KnowledgeEntry parse_entry(const QJsonObject& obj, const QString& category_id);

    QString root_;
    QString pending_open_;
    bool loaded_ = false;
    QHash<QString, KnowledgeCategory> categories_;
    QStringList category_order_;
    QHash<QString, const KnowledgeEntry*> entry_index_; // id → entry*
    QHash<QString, QString> alias_index_;               // alias (lower) → id
    QHash<QString, QString> abbreviations_;             // acronym → expansion
    mutable QHash<QString, QString> body_cache_;
};

} // namespace finterm::knowledge
