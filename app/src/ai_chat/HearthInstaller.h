#pragma once

#include "ai_chat/EngineRelease.h"

#include <QObject>
#include <QString>
#include <QTemporaryDir>

#include <memory>

class QNetworkAccessManager;
class QNetworkReply;
class QProcess;

namespace finterm {

/// "Get hearth": fetch hearth's own install script, check that it is the one
/// this finterm knows, and run it. What it does is hearth's business; finterm
/// shows what it says and looks for hearth when it has ended. GUI thread.
class HearthInstaller : public QObject {
    Q_OBJECT
  public:
    static HearthInstaller& instance();

    /// Is there a script to run (else the button opens hearth's page)?
    bool available() const { return !script_.url.isEmpty(); }
    bool running() const { return running_; }
    QString last_line() const { return line_; }

    void start();
    void cancel();

    /// For tests: another script, at another address.
    void use(const engine::InstallScript& script) { script_ = script; }

  signals:
    void changed(); ///< running(), last_line()
    void finished(bool ok, const QString& message);

  private:
    explicit HearthInstaller(QObject* parent = nullptr);
    void run(const QByteArray& script);
    void end(bool ok, const QString& message);

    engine::InstallScript script_;
    QNetworkAccessManager* nam_ = nullptr;
    QNetworkReply* reply_ = nullptr;
    QProcess* proc_ = nullptr;
    std::unique_ptr<QTemporaryDir> dir_;
    QString line_, tail_;
    bool running_ = false;
    bool cancelled_ = false;
};

} // namespace finterm
