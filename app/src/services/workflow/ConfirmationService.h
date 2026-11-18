#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>

#include <functional>

namespace finterm::workflow {

/// Confirmation types that require user approval.
enum class ConfirmationType {
    Trade,
    WorkflowStart,
};

/// Risk level for confirmation dialogs.
enum class RiskLevel { Low, Medium, High, Critical };

/// Confirmation request data.
struct ConfirmationRequest {
    QString id;
    ConfirmationType type;
    RiskLevel risk = RiskLevel::Low;
    QString title;
    QString message;
    QJsonObject details;
    bool paper_trading = true;
};

/// Dialog-based confirmation service for trading safety.
/// Auto-approves in paper trading mode for non-critical actions.
class ConfirmationService : public QObject {
    Q_OBJECT
  public:
    static ConfirmationService& instance();

    /// Request user confirmation. Calls callback with (approved, notes).
    void request(const ConfirmationRequest& req, std::function<void(bool approved, const QString& notes)> callback);

  signals:
    void confirmation_requested(const ConfirmationRequest& req);
    void confirmation_resolved(const QString& id, bool approved);

  private:
    ConfirmationService();

    struct PendingConfirmation {
        ConfirmationRequest request;
        std::function<void(bool, const QString&)> callback;
    };
    QMap<QString, PendingConfirmation> pending_;

  public:
    /// Called by UI to resolve a pending confirmation.
    void resolve(const QString& id, bool approved, const QString& notes = {});
};

} // namespace finterm::workflow
