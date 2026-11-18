#pragma once
#include "storage/repositories/BaseRepository.h"

namespace finterm {

struct FinancialNote {
    int id = 0;
    QString title;
    QString content;
    QString category;
    QString priority;  // LOW, MEDIUM, HIGH, CRITICAL
    QString tags;      // comma-separated
    QString tickers;   // comma-separated
    QString sentiment; // BULLISH, BEARISH, NEUTRAL
    bool is_favorite = false;
    bool is_archived = false;
    QString color_code;
    QString attachments;
    QString created_at;
    QString updated_at;
    QString reminder_date;
    int word_count = 0;
};

class NotesRepository : public BaseRepository<FinancialNote> {
  public:
    static NotesRepository& instance();

    // Notes CRUD
    Result<qint64> create(const FinancialNote& note);
    Result<FinancialNote> get(int id);
    Result<QVector<FinancialNote>> list_all(bool include_archived = false);
    Result<QVector<FinancialNote>> list_by_category(const QString& category);
    Result<QVector<FinancialNote>> search(const QString& query);
    Result<void> update(const FinancialNote& note);
    Result<void> remove(int id);
    Result<void> toggle_favorite(int id);
    Result<void> toggle_archive(int id);

  private:
    NotesRepository() = default;
    static FinancialNote map_note(QSqlQuery& q);
};

} // namespace finterm
