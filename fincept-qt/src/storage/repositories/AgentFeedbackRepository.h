#pragma once
// AgentFeedbackRepository.h — CRUD over agent_feedback (v035).
//
// User-verdict captures keyed by agent_traces.request_id.  Read
// by the trace-detail dialog (to show prior marks) + the future
// SKILL.md proposal flow (Track 7C).

#include "core/result/Result.h"

#include <QString>
#include <QVector>

namespace fincept {

struct AgentFeedbackRow {
    qint64 id = 0;
    QString request_id;
    QString verdict;     ///< "wrong" | "right" | "unsure"
    QString note;
    QString created_at;
};

class AgentFeedbackRepository {
  public:
    static AgentFeedbackRepository& instance();

    Result<void> create(const QString& request_id,
                        const QString& verdict,
                        const QString& note = {});

    Result<QVector<AgentFeedbackRow>> list_recent(int limit = 100);
    Result<QVector<AgentFeedbackRow>> list_by_request(const QString& request_id);
    Result<int> count_by_verdict(const QString& verdict);

    AgentFeedbackRepository(const AgentFeedbackRepository&) = delete;
    AgentFeedbackRepository& operator=(const AgentFeedbackRepository&) = delete;

  private:
    AgentFeedbackRepository() = default;
};

} // namespace fincept
