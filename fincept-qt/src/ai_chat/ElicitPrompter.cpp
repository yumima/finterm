// src/ai_chat/ElicitPrompter.cpp
#include "ai_chat/ElicitPrompter.h"

#include "services/agents/ElicitBridge.h"

#include <QInputDialog>
#include <QJsonArray>
#include <QMessageBox>

namespace fincept::ai_chat {

ElicitPrompter::ElicitPrompter(QWidget* dialog_parent, QObject* parent)
    : QObject(parent), dialog_parent_(dialog_parent) {
    // ONE subscriber per process, whatever the window count. Every MainWindow
    // constructs a prompter, but the bridge broadcasts each request to all
    // subscribers — a second one would show the identical modal again after
    // the first was answered, and its resolve() of the spent token would be
    // dropped. First window wins; secondary windows' prompters stay inert.
    static bool mounted = false;
    if (mounted)
        return;
    mounted = true;
    // Queued: requests originate on McpProvider's worker pool; the bridge
    // blocks that worker until resolve() is called from the modal below.
    connect(&fincept::services::ElicitBridge::instance(),
            &fincept::services::ElicitBridge::request_received, this, &ElicitPrompter::on_request,
            Qt::QueuedConnection);
}

void ElicitPrompter::on_request(const QString& token, const fincept::mcp::ElicitRequest& req) {
    // Modal shape based on the request's JSON-Schema. Three common shapes are
    // recognised; everything else falls back to free text that the schema
    // validator on the caller's side enforces.
    QWidget* anchor = dialog_parent_.data();
    const QString type = req.schema.value(QStringLiteral("type")).toString();
    fincept::mcp::ElicitResponse response;

    if (type == QStringLiteral("boolean")) {
        const auto btn = QMessageBox::question(anchor, QStringLiteral("Tool prompt"), req.prompt,
                                               QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
                                               QMessageBox::Cancel);
        if (btn == QMessageBox::Cancel)
            response.declined = true;
        else
            response.value = (btn == QMessageBox::Yes);
    } else if (req.schema.contains(QStringLiteral("enum"))) {
        QStringList items;
        for (const auto& v : req.schema.value(QStringLiteral("enum")).toArray())
            items.append(v.toString());
        bool ok = false;
        const QString choice =
            QInputDialog::getItem(anchor, QStringLiteral("Tool prompt"), req.prompt, items, 0, false, &ok);
        if (!ok || choice.isEmpty())
            response.declined = true;
        else
            response.value = choice;
    } else {
        bool ok = false;
        const QString text =
            QInputDialog::getMultiLineText(anchor, QStringLiteral("Tool prompt"), req.prompt, QString(), &ok);
        if (!ok)
            response.declined = true;
        else
            response.value = text;
    }

    fincept::services::ElicitBridge::instance().resolve(token, response);
}

} // namespace fincept::ai_chat
