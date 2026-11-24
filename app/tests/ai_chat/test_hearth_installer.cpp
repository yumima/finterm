// "Get hearth": the script that runs is the one this finterm knows, or none is.

#include "ai_chat/HearthInstaller.h"

#include <QCryptographicHash>
#include <QProcess>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using finterm::HearthInstaller;
using finterm::engine::install_script;

class TestHearthInstaller : public QObject {
    Q_OBJECT

  private slots:
    void init();
    void which_script();
    void without_a_release_there_is_nothing_to_run();
    void the_known_script_is_run_in_a_clean_environment();
    void another_script_is_not_run();
    void what_a_failed_one_said_is_shown();
    void it_can_be_stopped();
    void no_connection();

  private:
    /// Serve `body` once; the installer for it, with its real hash or another.
    void serve(const QByteArray& body, bool right_hash = true) {
        server_ = std::make_unique<QTcpServer>();
        QVERIFY(server_->listen(QHostAddress::LocalHost));
        connect(server_.get(), &QTcpServer::newConnection, this, [this, body]() {
            auto* s = server_->nextPendingConnection();
            connect(s, &QTcpSocket::readyRead, s, [s, body]() {
                s->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                s->disconnectFromHost();
            });
        });
        const QByteArray sum = QCryptographicHash::hash(right_hash ? body : body + "x", QCryptographicHash::Sha256).toHex();
        HearthInstaller::instance().use(
            {QStringLiteral("http://127.0.0.1:%1/install.sh").arg(server_->serverPort()), QString::fromLatin1(sum)});
    }
    /// {ok, message} of one run.
    QPair<bool, QString> run() {
        QSignalSpy spy(&HearthInstaller::instance(), &HearthInstaller::finished);
        HearthInstaller::instance().start();
        if (spy.isEmpty() && !spy.wait(20000))
            return {false, QStringLiteral("no end")};
        return {spy.first().at(0).toBool(), spy.first().at(1).toString()};
    }
    std::unique_ptr<QTcpServer> server_;
    QTemporaryDir dir_;
};

void TestHearthInstaller::init() {
#if defined(Q_OS_WIN)
    QSKIP("the scripts here are sh's");
#endif
}

void TestHearthInstaller::which_script() {
    const QString a(64, 'a'), b(64, 'b');
    QCOMPARE(install_script("0.2.0", a, b, false).url,
             QString("https://github.com/yumima/hearth/releases/download/v0.2.0/install.sh"));
    QCOMPARE(install_script("0.2.0", a, b, false).sha256, a);
    QVERIFY(install_script("0.2.0", a, b, true).url.endsWith("/v0.2.0/install.ps1"));
    QCOMPARE(install_script("0.2.0", a, b, true).sha256, b);
    QVERIFY(install_script("", a, b, false).url.isEmpty());
    QVERIFY(install_script("0.2.0", "", b, false).url.isEmpty());      // no hash, no script
    QVERIFY(install_script("0.2.0", "abc", b, false).url.isEmpty());   // nor with half of one
}

void TestHearthInstaller::without_a_release_there_is_nothing_to_run() {
    HearthInstaller::instance().use({});
    QVERIFY(!HearthInstaller::instance().available());
    HearthInstaller::instance().start();
    QVERIFY(!HearthInstaller::instance().running());
}

void TestHearthInstaller::the_known_script_is_run_in_a_clean_environment() {
    const QString out = dir_.filePath("out");
    qputenv("APPDIR", "/tmp/.mount_x");
    qputenv("LD_LIBRARY_PATH", "/tmp/.mount_x/usr/lib");
    serve("echo \"$1 [$APPDIR] [$LD_LIBRARY_PATH]\" > '" + out.toUtf8() + "'\necho first\necho last line\n");
    QSignalSpy changed(&HearthInstaller::instance(), &HearthInstaller::changed);
    const auto r = run();
    qunsetenv("APPDIR");
    qunsetenv("LD_LIBRARY_PATH");
    QVERIFY2(r.first, qPrintable(r.second));
    QFile f(out);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll().trimmed(), QByteArray("--no-open [] []"));
    QVERIFY(changed.size() >= 2 && !HearthInstaller::instance().running());
}

void TestHearthInstaller::another_script_is_not_run() {
    const QString out = dir_.filePath("evil");
    serve("touch '" + out.toUtf8() + "'\n", /*right_hash=*/false);
    const auto r = run();
    QVERIFY(!r.first && r.second.contains("was not run"));
    QVERIFY(!QFile::exists(out));
}

void TestHearthInstaller::what_a_failed_one_said_is_shown() {
    serve("echo working\necho 'hearth: there is no hearth for this' >&2\nexit 1\n");
    const auto r = run();
    QVERIFY(!r.first && r.second.contains("there is no hearth for this"));
}

void TestHearthInstaller::it_can_be_stopped() {
    const QByteArray mark = "61.0" + QByteArray::number(QCoreApplication::applicationPid());
    serve("sleep " + mark + " &\necho started\nwait\n"); // what it runs ends with it
    auto& i = HearthInstaller::instance();
    QSignalSpy spy(&i, &HearthInstaller::finished);
    i.start();
    QTRY_COMPARE_WITH_TIMEOUT(i.last_line(), QString("started"), 10000);
    i.cancel();
    QVERIFY(spy.wait(10000));
    QVERIFY(!spy.first().at(0).toBool() && spy.first().at(1).toString().startsWith("Stopped"));
    QProcess ps;
    ps.start("ps", {"-eo", "args"});
    QVERIFY(ps.waitForFinished(5000));
    QVERIFY(!ps.readAll().contains("sleep " + mark));
}

void TestHearthInstaller::no_connection() {
    HearthInstaller::instance().use({"http://127.0.0.1:1/install.sh", QString(64, 'a')});
    const auto r = run();
    QVERIFY(!r.first && r.second.contains("could not be fetched"));
}

QTEST_GUILESS_MAIN(TestHearthInstaller)
#include "test_hearth_installer.moc"
