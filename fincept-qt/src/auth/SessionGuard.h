#pragma once
#include <QObject>
#include <QTimer>

namespace fincept::auth {

/// Polls /user/validate-session every 30s.
/// On 401/403 or valid=false → triggers auto-logout.
class SessionGuard : public QObject {
    Q_OBJECT
  public:
    explicit SessionGuard(QObject* parent = nullptr);

    void start();
    void stop();

  signals:
    void session_expired();

  private slots:
    void check_pulse();

  private:
    QTimer timer_;
    bool is_checking_ = false;
    int net_failures_ = 0;  ///< consecutive connection failures; slow the pulse when the backend is unreachable

    static constexpr int PULSE_INTERVAL_MS = 5 * 60 * 1000;        // 5 minutes (normal cadence)
    static constexpr int SLOW_PULSE_INTERVAL_MS = 30 * 60 * 1000;  // 30 minutes (backed off, still recovers)
};

} // namespace fincept::auth
