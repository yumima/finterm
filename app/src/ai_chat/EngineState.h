#pragma once
// EngineState.h — what finterm makes of hearth: the local AI engine, installed
// and kept by itself. Pure rules; HearthService asks the questions, and
// EngineLinkRepository keeps the user's answer.

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace finterm::engine {

enum class State {
    Unknown,     // not looked yet
    Ready,       // serving, and its chat model is there
    NoModel,     // serving, with no chat model yet: its setup is not finished
    Stopped,     // installed, not serving
    Unsupported, // serving, speaking a contract this finterm does not know
    Absent,      // not here
    Foreign,     // another program answers at the address
};

/// The user's answer, per user.
enum class Link {
    Unasked,
    Connected, // finterm may use it
    Declined,  // finterm does not use it, and does not ask again
};

inline Link link_from(const QString& stored) {
    if (stored == QLatin1String("connected"))
        return Link::Connected;
    return stored == QLatin1String("declined") ? Link::Declined : Link::Unasked;
}

inline QString to_stored(Link link) {
    return link == Link::Connected ? QStringLiteral("connected")
           : link == Link::Declined ? QStringLiteral("declined")
                                    : QString();
}

/// The contracts this build works with: 0.1 (web search for every request that
/// does not say no, which finterm says) and later 0.x. hearth raises the major
/// number when a consumer has to change.
inline constexpr int kContractMajor = 0;
inline constexpr int kContractMinorFloor = 1;

inline bool contract_supported(const QString& contract) {
    const QStringList p = contract.trimmed().split(QLatin1Char('.'));
    bool major_ok = false, minor_ok = false;
    const int major = p.value(0).toInt(&major_ok), minor = p.value(1).toInt(&minor_ok);
    return p.size() == 2 && major_ok && minor_ok && major == kContractMajor && minor >= kContractMinorFloor;
}

/// Is the chat model there? From /admin/roles and /v1/models. A role bound to a
/// cloud model has nothing to install.
inline bool chat_model_present(const QJsonObject& roles_reply, const QJsonObject& models_reply) {
    const QJsonObject role = roles_reply.value("roles").toObject().value("primary_chat").toObject();
    const QString want = role.value("model").toString();
    if (want.isEmpty())
        return false;
    if (role.value("remote").toBool())
        return true;
    for (const auto& v : models_reply.value("data").toArray()) {
        const QJsonObject m = v.toObject();
        if (m.value("owned_by").toString() == QLatin1String("hearth-role"))
            continue; // the alias, listed whether or not its model is there
        const QString id = m.value("id").toString();
        if (id == want || id == want + QLatin1String(":latest"))
            return true;
    }
    return false;
}

struct Probe {
    bool answered = false; // something speaks HTTP at the address
    bool hearth = false;   // …and says it is hearth
    QString contract{};
    bool model = false;     // chat_model_present
    bool installed = false; // its program is on this computer
};

inline State classify(const Probe& p) {
    if (p.hearth) {
        if (!contract_supported(p.contract))
            return State::Unsupported;
        return p.model ? State::Ready : State::NoModel;
    }
    if (p.answered)
        return State::Foreign;
    return p.installed ? State::Stopped : State::Absent;
}

/// Ask, once, when there is something to connect to. One that is not here is
/// offered where the user looks for it (first-run setup, Settings), not brought up.
inline bool should_ask(State state, Link link) {
    return link == Link::Unasked &&
           (state == State::Ready || state == State::NoModel || state == State::Stopped || state == State::Unsupported);
}

} // namespace finterm::engine
