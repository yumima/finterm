// src/ai_chat/ElicitPrompter.cpp
#include "ai_chat/ElicitPrompter.h"

#include "services/agents/ElicitBridge.h"

#include <QApplication>
#include <QInputDialog>
#include <QJsonArray>
#include <QMessageBox>

namespace fincept::ai_chat {

void ElicitPrompter::ensure_mounted() {
    // One live subscriber per process, owned by qApp rather than a window: a
    // window-owned prompter died with its window, and a static "already
    // mounted" flag then left the bridge subscriber-less for the rest of the
    // session — every destructive tool silently denied, no modal ever shown.
    static ElicitPrompter* instance = nullptr;
    if (instance)
        return;
    instance = new ElicitPrompter();
    instance->setParent(qApp);
}

ElicitPrompter::ElicitPrompter() {
    // Queued: requests originate on McpProvider's worker pool; the bridge
    // blocks that worker until resolve() is called from the modal below.
    connect(&fincept::services::ElicitBridge::instance(),
            &fincept::services::ElicitBridge::request_received, this, &ElicitPrompter::on_request,
            Qt::QueuedConnection);
}

void ElicitPrompter::on_request(const QString& token, const fincept::mcp::ElicitRequest& req) {
    // Modal shape based on the request's JSON-Schema. Three common shapes are
    // recognised; everything else falls back to free text that the schema
    // validator on the caller's side enforces. Anchored to whichever window is
    // active NOW — the prompter belongs to the process, not to a window.
    QWidget* anchor = QApplication::activeWindow();
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
