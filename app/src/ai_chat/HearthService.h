#pragma once

#include "ai_chat/EngineState.h"

#include <QMutex>
#include <QSet>
#include <QObject>
#include <QString>

#include <optional>

class QProcess;

namespace finterm {

/// hearth, the local AI engine: where it is, what it is doing, and whether
/// this user lets finterm use it.
///
/// hearth is a program of its own, installed, updated and kept running by
/// itself; finterm finds it, asks, and connects. Until the user has said yes
/// (EngineLinkRepository) nothing is sent to it but the questions detect()
/// asks: is this hearth, which contract, is there a model.
///
/// `resolve()` is the deterministic, **I/O-free** base resolver used on the hot
/// path (never blocks a UI thread or a held lock):
///   1. env `FINTERM_ENGINE_BASE_URL`  (explicit override — remote/custom)
///   2. hearth's address, `http://127.0.0.1:11435` (bare root, no /v1)
class HearthService : public QObject {
    Q_OBJECT

  public:
    static HearthService& instance();

    struct Resolution {
        // Root URL without /v1 (e.g. "http://127.0.0.1:11435").
        // LlmService appends /v1/chat/completions; EmbeddingsClient appends /v1/embeddings.
        QString base_url;
        bool is_hearth; // base is hearth's (use role aliases like primary_chat)
    };
    /// Deterministic + I/O-free. Pure resolution; safe under any lock.
    Resolution resolve() const;
    QString engine_base_url() const { return resolve().base_url; }

    /// May finterm use it now? The user said so (their database: false before
    /// anybody is signed in), and it is not one known to speak another contract.
    /// Any thread.
    bool linked() const;
    engine::Link link() const;

    struct Status {
        engine::State state = engine::State::Unknown;
        QString version;  // engine build (when hearth)
        QString contract; // consumer contract (when hearth)
        QString base_url; // the base that was probed
        QString program;  // hearth's program on this computer, "" if not found
        QString model;    // what primary_chat is bound to
        bool remote = false; // …and it is a cloud model: prompts leave the computer
    };
    /// Kick off a non-blocking look. Emits detected() on the GUI thread when
    /// done. Coalesces concurrent calls.
    void detect();
    Status status() const;

    /// The user's answers. Each reloads the chat's configuration and emits
    /// link_changed(). Empty on success, else what went wrong.
    QString connect_engine(bool for_chat);
    /// Connected already, or it is an error: never the answer to a question
    /// nobody asked the user. (GUI thread, as all of these.)
    QString use_for_chat();
    /// Another provider for the chat, also over a start that is waited for.
    QString use_another_for_chat(const QString& provider);
    QString disconnect_engine();
    QString decline_engine();

    /// Start the installed hearth; it goes on when finterm ends. Emits
    /// started(ok, message), and detects. GUI thread.
    void start();
    /// The same, then connect it for the user who asked, once it is seen to be
    /// one finterm can use. The asking window may be gone by then.
    void start_and_connect(bool for_chat);
    bool starting() const { return starting_ != nullptr; }

    /// "Start hearth when finterm opens" — for a user who is signed in and
    /// connected; once each, so that one they stop stays stopped.
    void start_if_wanted();

    /// hearth's program: FINTERM_HEARTH_BIN, then where its installer puts it,
    /// then ~/.local/bin, then PATH. Empty when not found.
    static QString program();

    /// Where its installer puts everything (packages/hearth, installer/).
    static QString install_dir();

    static constexpr const char* kDefaultBase = "http://127.0.0.1:11435";
    static constexpr const char* kHomepage = "https://github.com/yumima/hearth";

  signals:
    void detected();
    void link_changed();
    void started(bool ok, const QString& message);

  private:
    explicit HearthService(QObject* parent = nullptr) : QObject(parent) {}
    void apply_status(const Status& s); // store + emit detected() (GUI thread)
    void link_was_changed();
    void finish_start(bool ok, const QString& message);
    void await_serving(int tries_left);
    void connect_now_started(engine::State state);

    mutable QMutex mutex_;
    Status status_;
    bool detecting_ = false;
    QProcess* starting_ = nullptr;
    std::optional<bool> connect_after_; ///< for_chat, while a start is to be followed by connecting
    int connect_tries_ = 0;
    QString connect_for_;               ///< whose database that was asked in
    QSet<QString> start_offered_;       ///< databases start_if_wanted() has been run for
};

} // namespace finterm
