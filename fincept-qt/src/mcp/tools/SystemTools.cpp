// SystemTools.cpp — Auth status, cache, app info (Qt port)

#include "mcp/tools/SystemTools.h"

#include "auth/AuthManager.h"
#include "core/config/AppIdentity.h"
#include "core/logging/Logger.h"
#include "mcp/McpProvider.h"
#include "mcp/tools/ThreadHelper.h"
#include "python/PythonRunner.h"
#include "storage/cache/CacheManager.h"

#include <QCoreApplication>

namespace fincept::mcp::tools {

std::vector<ToolDef> get_system_tools() {
    std::vector<ToolDef> tools;

    // ── get_auth_status ────────────────────────────────────────────────
    {
        ToolDef t;
        t.name = "get_auth_status";
        t.description = "Check if the user is logged in and get basic account info.";
        t.category = "system";
        t.handler = [](const QJsonObject&) -> ToolResult {
            // Tool handlers run on a worker; the session belongs to the main
            // thread, so copy it there.
            bool authenticated = false;
            QString username, email;
            detail::run_on_target_thread_sync(&auth::AuthManager::instance(), [&]() {
                const auto& s = auth::AuthManager::instance().session();
                authenticated = s.authenticated;
                username = s.user_info.username;
                email = auth::displayable_email(s.user_info.email);
            });
            if (!authenticated)
                return ToolResult::ok_data(QJsonObject{{"authenticated", false}, {"user_type", "none"}});
            QJsonObject out{{"authenticated", true}, {"username", username}};
            if (!email.isEmpty())
                out["email"] = email;
            return ToolResult::ok_data(out);
        };
        tools.push_back(std::move(t));
    }

    // ── get_cache_stats ────────────────────────────────────────────────
    {
        ToolDef t;
        t.name = "get_cache_stats";
        t.description = "Get cache statistics: total number of cached entries.";
        t.category = "system";
        t.handler = [](const QJsonObject&) -> ToolResult {
            return ToolResult::ok_data(QJsonObject{{"total_entries", CacheManager::instance().entry_count()}});
        };
        tools.push_back(std::move(t));
    }

    // ── clear_cache ────────────────────────────────────────────────────
    {
        ToolDef t;
        t.name = "clear_cache";
        t.description = "Clear all cache entries.";
        t.category = "system";
        t.handler = [](const QJsonObject&) -> ToolResult {
            CacheManager::instance().clear();
            return ToolResult::ok("Cache cleared");
        };
        tools.push_back(std::move(t));
    }

    // ── get_app_info ───────────────────────────────────────────────────
    {
        ToolDef t;
        t.name = "get_app_info";
        t.description = "Get application version, platform, number of MCP tools, and Python availability.";
        t.category = "system";
        t.handler = [](const QJsonObject&) -> ToolResult {
            return ToolResult::ok_data(
                // Was a hardcoded "4.0.0" that matched no release. The build's
                // version is set on the QApplication from FINCEPT_VERSION_STRING.
                QJsonObject{{"name", QString::fromLatin1(AppIdentity::kDisplayName)},
                            {"version", QCoreApplication::applicationVersion()},
                            {"qt_version", QString::fromLatin1(qVersion())},
                            {"platform",
#ifdef _WIN32
                             "windows"
#elif defined(__APPLE__)
                             "macos"
#else
                             "linux"
#endif
                            },
                            {"internal_tools", static_cast<int>(McpProvider::instance().tool_count())},
                            {"python_available", python::PythonRunner::instance().is_available()}});
        };
        tools.push_back(std::move(t));
    }

    return tools;
}

} // namespace fincept::mcp::tools
