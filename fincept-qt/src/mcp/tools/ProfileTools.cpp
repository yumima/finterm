// ProfileTools.cpp — Profile tab MCP tools (7 tools)
// Covers: local account profile, session, local API key, notifications.
// finterm is local-only: the account lives in auth.db (AuthService), so there
// is no usage, credits, subscription, billing, login-history or MFA to expose.

#include "mcp/tools/ProfileTools.h"

#include "auth/AuthManager.h"
#include "auth/AuthService.h"
#include "auth/AuthTypes.h"
#include "core/config/ProfileManager.h"
#include "mcp/tools/ThreadHelper.h"
#include "services/notifications/NotificationService.h"

#include <QJsonArray>
#include <QJsonObject>

namespace fincept::mcp::tools {

using namespace fincept::auth;

// The fields worth reporting from AuthService's profile JSON.
static QJsonObject local_profile_fields(const QJsonObject& p) {
    QJsonObject out;
    out["username"] = p["username"];
    const QString email = displayable_email(p["email"].toString());
    if (!email.isEmpty())
        out["email"] = email;
    out["created_at"] = p["created_at"];
    out["last_login_at"] = p["last_login_at"];
    return out;
}

// ── Tool registration ─────────────────────────────────────────────────────────

std::vector<ToolDef> get_profile_tools() {
    std::vector<ToolDef> tools;

    // ── profile_get ──────────────────────────────────────────────────────────
    {
        ToolDef t;
        t.name = "profile_get";
        t.description = "Get the signed-in local user's profile: username, email (if one is stored), "
                        "created_at, last_login_at, and the active data profile.";
        t.category = "profile";
        t.input_schema.properties = QJsonObject{};
        t.handler = [](const QJsonObject&) -> ToolResult {
            ApiResponse resp{false, {}, QStringLiteral("Not authenticated"), 401};
            // auth.db's connection belongs to the main thread; tool handlers
            // run on a worker. AuthService answers synchronously.
            detail::run_on_target_thread_sync(&AuthService::instance(), [&resp]() {
                const auto& sess = AuthManager::instance().session();
                if (!sess.authenticated)
                    return;
                AuthService::instance().get_user_profile(sess.api_key, [&resp](ApiResponse r) { resp = r; });
            });
            if (!resp.success)
                return ToolResult::fail(resp.error.isEmpty() ? "Could not read the local profile" : resp.error);
            QJsonObject out = local_profile_fields(resp.data);
            out["data_profile"] = fincept::ProfileManager::instance().active();
            return ToolResult::ok_data(out);
        };
        tools.push_back(std::move(t));
    }

    // ── profile_get_session ──────────────────────────────────────────────────
    {
        ToolDef t;
        t.name = "profile_get_session";
        t.description = "Get current session information: whether a local user is signed in, "
                        "their username and email (if stored), and the active data profile.";
        t.category = "profile";
        t.input_schema.properties = QJsonObject{};
        t.handler = [](const QJsonObject&) -> ToolResult {
            QJsonObject out;
            bool authenticated = false;
            detail::run_on_target_thread_sync(&AuthManager::instance(), [&]() {
                const auto& sess = AuthManager::instance().session();
                authenticated = sess.authenticated;
                QJsonObject p;
                p["username"] = sess.user_info.username;
                p["email"] = sess.user_info.email;
                p["created_at"] = sess.user_info.created_at;
                p["last_login_at"] = sess.user_info.last_login_at;
                out = local_profile_fields(p);
            });
            if (!authenticated)
                return ToolResult::fail("Not authenticated");
            out["authenticated"] = true;
            out["data_profile"] = fincept::ProfileManager::instance().active();
            return ToolResult::ok_data(out);
        };
        tools.push_back(std::move(t));
    }

    // ── profile_get_api_key ──────────────────────────────────────────────────
    {
        ToolDef t;
        t.name = "profile_get_api_key";
        t.description = "Get the signed-in user's local API key from the active session.";
        t.category = "profile";
        t.input_schema.properties = QJsonObject{};
        t.handler = [](const QJsonObject&) -> ToolResult {
            // The session is owned by the main thread (regenerate_api_key
            // rewrites it there); copy what we need on that thread.
            bool authenticated = false;
            QString api_key;
            detail::run_on_target_thread_sync(&AuthManager::instance(), [&]() {
                const auto& sess = AuthManager::instance().session();
                authenticated = sess.authenticated;
                api_key = sess.api_key;
            });
            if (!authenticated)
                return ToolResult::fail("Not authenticated");
            if (api_key.isEmpty())
                return ToolResult::fail("No API key in session");
            return ToolResult::ok_data(QJsonObject{{"api_key", api_key}});
        };
        tools.push_back(std::move(t));
    }

    // ── profile_regenerate_api_key ───────────────────────────────────────────
    {
        ToolDef t;
        t.name = "profile_regenerate_api_key";
        t.description = "Regenerate the local API key. WARNING: the current API key stops working "
                        "immediately. Returns the new API key.";
        t.category = "profile";
        t.input_schema.properties = QJsonObject{};
        t.handler = [](const QJsonObject&) -> ToolResult {
            bool ok = false;
            QString err;
            QString new_key;
            detail::run_on_target_thread_sync(&AuthManager::instance(), [&]() {
                AuthManager::instance().regenerate_api_key([&](bool success, const QString& e) {
                    ok = success;
                    err = e;
                    if (success)
                        new_key = AuthManager::instance().session().api_key;
                });
            });
            if (!ok)
                return ToolResult::fail(err.isEmpty() ? "Could not regenerate the API key" : err);
            return ToolResult::ok_data(QJsonObject{{"api_key", new_key}});
        };
        tools.push_back(std::move(t));
    }

    // ── profile_get_notifications ────────────────────────────────────────────
    {
        ToolDef t;
        t.name = "profile_get_notifications";
        t.description = "Get in-app notification history. Can filter to unread only.";
        t.category = "profile";
        t.input_schema.properties = QJsonObject{
            {"limit", QJsonObject{{"type", "integer"}, {"description", "Max notifications (default: 20)"}}},
            {"offset", QJsonObject{{"type", "integer"}, {"description", "Pagination offset (default: 0)"}}},
            {"unread_only",
             QJsonObject{{"type", "boolean"}, {"description", "Only return unread notifications (default: false)"}}},
        };
        t.handler = [](const QJsonObject& args) -> ToolResult {
            int limit = args["limit"].toInt(20);
            int offset = args["offset"].toInt(0);
            bool unread = args["unread_only"].toBool(false);

            using namespace fincept::notifications;
            const auto& history = NotificationService::instance().history();

            QJsonArray arr;
            int count = 0;
            int skipped = 0;
            for (const auto& rec : history) {
                if (unread && rec.read)
                    continue;
                if (skipped < offset) {
                    ++skipped;
                    continue;
                }
                if (count >= limit)
                    break;

                QJsonObject obj;
                obj["id"] = rec.id;
                obj["title"] = rec.request.title;
                obj["message"] = rec.request.message;
                obj["read"] = rec.read;
                obj["time"] = rec.received_at.toString(Qt::ISODate);
                arr.append(obj);
                ++count;
            }

            QJsonObject result;
            result["notifications"] = arr;
            result["total"] = arr.size();
            return ToolResult::ok("OK", QJsonValue(result));
        };
        tools.push_back(std::move(t));
    }

    // ── profile_mark_notification_read ───────────────────────────────────────
    {
        ToolDef t;
        t.name = "profile_mark_notification_read";
        t.description = "Mark a specific in-app notification as read by its ID.";
        t.category = "profile";
        t.input_schema.properties = QJsonObject{
            {"id", QJsonObject{{"type", "integer"}, {"description", "Notification ID to mark as read"}}},
        };
        t.input_schema.required = {"id"};
        t.handler = [](const QJsonObject& args) -> ToolResult {
            int id = args["id"].toInt(-1);
            if (id < 0)
                return ToolResult::fail("Missing or invalid 'id'");
            fincept::notifications::NotificationService::instance().mark_read(id);
            QJsonObject result;
            result["success"] = true;
            result["id"] = id;
            return ToolResult::ok("Marked as read", QJsonValue(result));
        };
        tools.push_back(std::move(t));
    }

    // ── profile_mark_all_notifications_read ──────────────────────────────────
    {
        ToolDef t;
        t.name = "profile_mark_all_notifications_read";
        t.description = "Mark all in-app notifications as read at once.";
        t.category = "profile";
        t.input_schema.properties = QJsonObject{};
        t.handler = [](const QJsonObject&) -> ToolResult {
            fincept::notifications::NotificationService::instance().mark_all_read();
            return ToolResult::ok("All notifications marked as read");
        };
        tools.push_back(std::move(t));
    }

    return tools;
}

} // namespace fincept::mcp::tools
