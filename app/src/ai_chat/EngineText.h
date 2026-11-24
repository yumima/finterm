#pragma once
// EngineText.h — what finterm says about hearth, and what it offers to do, for
// each state. Pure, so every state's words and actions are in one place and tested.

#include "ai_chat/EngineState.h"

namespace finterm::engine {

enum class Action { None, Connect, StartAndConnect, Start, FinishSetup, HowToUpdate, Get, Recheck };

struct View {
    QString headline;
    QString detail;
    Action primary = Action::None;
    QString primary_text;
    bool can_disconnect = false;
    bool good = false; // connected, and usable now
};

struct Facts {
    State state = State::Unknown;
    Link link = Link::Unasked;
    QString version, contract, base_url, model;
    bool remote = false; // the model is in a cloud
};

inline View view_for(const Facts& f) {
    const bool linked = f.link == Link::Connected;
    View v;
    v.can_disconnect = linked;
    switch (f.state) {
    case State::Unknown:
        v.headline = QStringLiteral("Looking for hearth…");
        v.detail = QStringLiteral("hearth runs AI models on this computer.");
        break;
    case State::Ready:
        v.headline = linked ? QStringLiteral("hearth %1 is connected").arg(f.version)
                            : QStringLiteral("hearth %1 is running").arg(f.version);
        v.detail = f.remote ? QStringLiteral("Its chat model, %1, is a cloud model: what you ask leaves this computer.").arg(f.model)
                            : QStringLiteral("Chat model: %1, on this computer.").arg(f.model);
        v.good = linked;
        if (!linked) {
            v.primary = Action::Connect;
            v.primary_text = QStringLiteral("Connect");
        }
        break;
    case State::NoModel:
        v.headline = QStringLiteral("hearth %1 is running, without a chat model").arg(f.version);
        v.detail = QStringLiteral("Finish its setup to choose a model that fits this computer.");
        v.primary = Action::FinishSetup;
        v.primary_text = QStringLiteral("Finish setup in hearth");
        break;
    case State::Stopped:
        v.headline = QStringLiteral("hearth is installed, and not running");
        v.detail = QStringLiteral("finterm can start it. It goes on running when finterm closes.");
        v.primary = linked ? Action::Start : Action::StartAndConnect;
        v.primary_text = linked ? QStringLiteral("Start") : QStringLiteral("Start and connect");
        break;
    case State::Unsupported:
        v.headline = QStringLiteral("This hearth does not match this finterm");
        v.detail = QStringLiteral("hearth %1 speaks contract %2; this finterm knows %3.x from %3.%4 on. Update the older of the two.")
                       .arg(f.version, f.contract.isEmpty() ? QStringLiteral("?") : f.contract)
                       .arg(kContractMajor)
                       .arg(kContractMinorFloor);
        v.primary = Action::HowToUpdate;
        v.primary_text = QStringLiteral("Open hearth's page");
        break;
    case State::Absent:
        v.headline = linked ? QStringLiteral("hearth is connected, but was not found") : QStringLiteral("hearth is not installed");
        v.detail = QStringLiteral("hearth is a free program that runs AI models on this computer: chat, memory and "
                                  "read-aloud without a cloud account. finterm works without it.");
        v.primary = Action::Get;
        v.primary_text = QStringLiteral("Get hearth");
        break;
    case State::Foreign:
        v.headline = QStringLiteral("Another program answers where hearth should");
        v.detail = QStringLiteral("%1 is in use by something that is not hearth.").arg(f.base_url);
        v.primary = Action::Recheck;
        v.primary_text = QStringLiteral("Check again");
        break;
    }
    return v;
}

} // namespace finterm::engine
