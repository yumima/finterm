#include "auth/SessionGuard.h"

#include "auth/AuthApi.h"
#include "auth/AuthManager.h"
#include "core/logging/Logger.h"

namespace fincept::auth {

SessionGuard::SessionGuard(QObject* parent) : QObject(parent) {
    timer_.setSingleShot(false);
    timer_.setInterval(PULSE_INTERVAL_MS);
    connect(&timer_, &QTimer::timeout, this, &SessionGuard::check_pulse);

    connect(&AuthManager::instance(), &AuthManager::auth_state_changed, this, [this]() {
        const auto& s = AuthManager::instance().session();
        if (s.authenticated && !s.api_key.isEmpty()) {
            start();
        } else {
            stop();
        }
    });
}

void SessionGuard::start() {
    if (!timer_.isActive()) {
        net_failures_ = 0;                      // fresh back-off budget for this session
        timer_.setInterval(PULSE_INTERVAL_MS);  // restore normal cadence (start() re-arms after a back-off)
        check_pulse();
        timer_.start();
    }
}

void SessionGuard::stop() {
    timer_.stop();
}

void SessionGuard::check_pulse() {
    const auto& s = AuthManager::instance().session();
    if (!s.authenticated || s.api_key.isEmpty())
        return;
    if (is_checking_)
        return;

    is_checking_ = true;

    AuthApi::instance().session_pulse([this](ApiResponse r) {
        is_checking_ = false;

        // 401/403 → session_token is stale. But the api_key may still be valid.
        // Attempt recovery before logging the user out.
        if (!r.success && (r.status_code == 401 || r.status_code == 403)) {
            LOG_WARN("SessionGuard",
                     QString("Pulse returned HTTP %1 — attempting session recovery").arg(r.status_code));
            is_checking_ = true;
            AuthManager::instance().attempt_session_recovery([this](bool recovered) {
                is_checking_ = false;
                if (recovered) {
                    LOG_INFO("SessionGuard", "Session recovered — api_key still valid");
                    return;
                }
                // api_key is truly invalid — must re-login
                LOG_WARN("SessionGuard", "Session recovery failed — api_key invalid, logging out");
                stop();
                emit session_expired();
                AuthManager::instance().logout();
            });
            return;
        }

        // Network/server error → don't log out. A connection-level failure
        // (status 0 = unreachable) means there's no session backend to talk to
        // — e.g. the localhost build with no :8765 server. After a couple of
        // tries, SLOW the pulse to 30 min rather than stopping outright: that
        // quiets the log/retries on a dead backend while still recovering if it
        // comes back (a later successful pulse restores the 5-min cadence). A
        // hard stop() would never re-arm mid-session, silently disabling session
        // monitoring after a transient blip.
        if (!r.success) {
            if (r.status_code == 0 && ++net_failures_ == 2) {
                LOG_INFO("SessionGuard", "Session backend unreachable — slowing pulse to 30 min");
                timer_.setInterval(SLOW_PULSE_INTERVAL_MS);
            } else {
                LOG_DEBUG("SessionGuard", "Pulse network error — skipping");
            }
            return;
        }
        // Healthy pulse — reset the budget and restore the normal cadence.
        if (net_failures_ != 0) {
            net_failures_ = 0;
            timer_.setInterval(PULSE_INTERVAL_MS);
        }

        // Explicit valid=false in response body — same recovery path
        auto data = r.data;
        if (data.contains("data") && data["data"].isObject())
            data = data["data"].toObject();
        if (data.contains("valid") && !data["valid"].toBool()) {
            LOG_WARN("SessionGuard", "Pulse returned valid=false — attempting session recovery");
            is_checking_ = true;
            AuthManager::instance().attempt_session_recovery([this](bool recovered) {
                is_checking_ = false;
                if (recovered) {
                    LOG_INFO("SessionGuard", "Session recovered after valid=false");
                    return;
                }
                LOG_WARN("SessionGuard", "Session recovery failed — logging out");
                stop();
                emit session_expired();
                AuthManager::instance().logout();
            });
        }
    });
}

} // namespace fincept::auth
