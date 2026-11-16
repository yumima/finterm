#pragma once
// SelfTest.h — `finterm --selftest [report.json]`: does this install get data?
//
// A headless end-to-end check of the one chain a new install depends on and no
// unit test can cover: set up the Python environment the way first launch does
// (download uv, install Python, build both venvs), start the data daemon over
// this platform's transport, and fetch one real quote through it. No window,
// no account, no PIN.
//
// It exists because Windows shipped with a data daemon that could not start at
// all (its socket type does not exist there) and nothing between the build and
// a user's desktop could notice. The release workflow now installs the built
// Windows installer and runs this before publishing.
//
// Exit code: 0 data fetched · 2 Python setup failed · 3 no data from the
// daemon · 4 timed out. The report (stdout, and the file if a path is given)
// says which stage, and why.

#include "core/logging/Logger.h"
#include "python/PythonSetupManager.h"
#include "python/PythonWorker.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include <cmath>
#include <cstdio>
#include <memory>

namespace finterm::selftest {

inline int run(QCoreApplication& app, const QString& report_path) {
    auto clock = std::make_shared<QElapsedTimer>();
    clock->start();
    auto done = std::make_shared<bool>(false);

    auto say = [](const QString& line) {
        std::fprintf(stdout, "[selftest] %s\n", line.toUtf8().constData());
        std::fflush(stdout);
        LOG_INFO("SelfTest", line);
    };

    auto finish = [&app, clock, done, report_path, say](int code, const QString& stage, const QString& detail,
                                                        const QJsonObject& extra = {}) {
        if (*done)
            return;
        *done = true;
        QJsonObject report = extra;
        report["ok"] = code == 0;
        report["exit_code"] = code;
        report["stage"] = stage;
        report["detail"] = detail;
        report["seconds"] = qint64(clock->elapsed() / 1000);
        report["version"] = QCoreApplication::applicationVersion();
#if defined(Q_OS_WIN)
        report["platform"] = QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
        report["platform"] = QStringLiteral("macos");
#else
        report["platform"] = QStringLiteral("linux");
#endif
        const QByteArray json = QJsonDocument(report).toJson(QJsonDocument::Indented);
        if (!report_path.isEmpty()) {
            QFile f(report_path);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
                f.write(json);
        }
        say(QStringLiteral("%1 — %2: %3").arg(code == 0 ? "PASS" : "FAIL", stage, detail));
        std::fprintf(stdout, "%s\n", json.constData());
        std::fflush(stdout);
        // Let the daemon be told to shut down before the process goes.
        python::PythonWorker::instance().stop();
        app.exit(code);
    };

    // The whole chain, bounded: a first-run setup is minutes, never hours.
    QTimer::singleShot(40 * 60 * 1000, &app, [finish]() { finish(4, "timeout", "no result within 40 minutes"); });

    auto ask_for_data = [say, finish]() {
        say("starting the data daemon and asking for one quote (AAPL)…");
        python::PythonWorker::instance().submit(
            QStringLiteral("quote"), QJsonObject{{"symbol", "AAPL"}},
            [finish](bool ok, QJsonObject result, QString error) {
                const double price = result.value("price").toDouble(std::nan(""));
                QJsonObject extra{{"quote", result}};
                if (ok && std::isfinite(price) && price > 0.0)
                    finish(0, "data", QStringLiteral("AAPL %1 via the data daemon").arg(price), extra);
                else
                    finish(3, "data",
                           error.isEmpty() ? QStringLiteral("the daemon answered without a price") : error, extra);
            },
            180'000);
    };

    auto& setup = python::PythonSetupManager::instance();
    const python::SetupStatus st = setup.check_status();
    say(QStringLiteral("python environment: uv=%1 python=%2 venv1=%3 venv2=%4 (data root %5)")
            .arg(st.uv_installed)
            .arg(st.python_installed)
            .arg(st.venv_numpy1_ready)
            .arg(st.venv_numpy2_ready)
            .arg(st.install_dir));

    if (st.needs_setup || st.needs_package_sync) {
        say("running first-launch setup (uv, Python, two environments) — several minutes…");
        QObject::connect(&setup, &python::PythonSetupManager::progress_changed, &app,
                         [say](const python::SetupProgress& p) {
                             if (p.is_error || p.progress == 0 || p.progress == 100)
                                 say(QStringLiteral("setup %1 %2% %3").arg(p.step).arg(p.progress).arg(p.message));
                         });
        QObject::connect(&setup, &python::PythonSetupManager::setup_complete, &app,
                         [finish, ask_for_data](bool success, const QString& error) {
                             if (!success)
                                 finish(2, "setup", error.isEmpty() ? QStringLiteral("setup failed") : error);
                             else
                                 ask_for_data();
                         });
        setup.run_setup();
    } else {
        ask_for_data();
    }
    return app.exec();
}

} // namespace finterm::selftest
