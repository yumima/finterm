#include "services/agents/CapabilityRegistry.h"

#include "core/logging/Logger.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace fincept::services {

namespace {

constexpr const char* kCapTag = "CapabilityRegistry";

/// Resolve the capabilities.json path.  Tries (in order):
///   1. $FINCEPT_CAPABILITIES_JSON env var (CI / tests)
///   2. <app dir>/resources/ai/capabilities.json (installed)
///   3. <app dir>/../fincept-qt/resources/ai/capabilities.json (dev build)
///   4. <cwd>/fincept-qt/resources/ai/capabilities.json (repo-root run)
QString resolve_path() {
    if (const QByteArray env = qgetenv("FINCEPT_CAPABILITIES_JSON"); !env.isEmpty())
        return QString::fromUtf8(env);

    const QString app = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        app + "/resources/ai/capabilities.json",
        app + "/../fincept-qt/resources/ai/capabilities.json",
        QDir::currentPath() + "/fincept-qt/resources/ai/capabilities.json",
        QDir::currentPath() + "/resources/ai/capabilities.json",
    };
    for (const auto& p : candidates) {
        if (QFileInfo::exists(p))
            return p;
    }
    return {};
}

QHash<QString, QString> read_string_map(const QJsonObject& obj, const QStringList& keys) {
    QHash<QString, QString> out;
    for (const auto& k : keys) {
        const QString v = obj.value(k).toString();
        if (!v.isEmpty())
            out.insert(k, v);
    }
    return out;
}

} // anonymous namespace

CapabilityRegistry& CapabilityRegistry::instance() {
    static CapabilityRegistry r;
    return r;
}

CapabilityRegistry::CapabilityRegistry() {
    load();
}

void CapabilityRegistry::load() {
    const QString path = resolve_path();
    if (path.isEmpty()) {
        LOG_WARN(kCapTag, "capabilities.json not found in any expected location — "
                          "capability gating will return unsupported for everything");
        return;
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        LOG_WARN(kCapTag, QString("Failed to open %1: %2").arg(path, f.errorString()));
        return;
    }
    QJsonParseError perr;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        LOG_WARN(kCapTag, QString("Failed to parse %1: %2").arg(path, perr.errorString()));
        return;
    }

    const QJsonObject root = doc.object();
    for (const auto& v : root.value("runtimes").toArray()) {
        if (v.isString())
            runtimes_.append(v.toString());
    }
    if (runtimes_.isEmpty()) {
        LOG_WARN(kCapTag, "capabilities.json has empty runtimes[] — falling back to anthropic+local");
        runtimes_ = {"anthropic", "local"};
    }

    // Per-runtime keys we expect to see on each capability entry, plus
    // the notes_<runtime> shorthand.  Precomputing the list once.
    QStringList notes_keys = {"notes"};
    for (const auto& rt : runtimes_)
        notes_keys.append("notes_" + rt);

    for (const auto& v : root.value("capabilities").toArray()) {
        const QJsonObject obj = v.toObject();
        Capability c;
        c.id = obj.value("id").toString();
        if (c.id.isEmpty())
            continue;  // Malformed; skip.
        c.label = obj.value("label").toString();
        c.description = obj.value("description").toString();
        for (const auto& rt : runtimes_) {
            const QString st = obj.value(rt).toString();
            if (!st.isEmpty())
                c.runtime_status.insert(rt, st);
        }
        c.notes = read_string_map(obj, notes_keys);
        for (const auto& s : obj.value("ui_surface").toArray()) {
            if (s.isString())
                c.ui_surface.append(s.toString());
        }
        capabilities_.append(std::move(c));
    }

    LOG_INFO(kCapTag, QString("Loaded %1 capabilities across %2 runtime(s) from %3")
                          .arg(capabilities_.size())
                          .arg(runtimes_.size())
                          .arg(path));
}

bool CapabilityRegistry::supports(const QString& runtime, const QString& capability_id) const {
    const QString st = status(runtime, capability_id);
    return st == QStringLiteral("supported") || st == QStringLiteral("partial");
}

QString CapabilityRegistry::status(const QString& runtime, const QString& capability_id) const {
    for (const auto& c : capabilities_) {
        if (c.id == capability_id)
            return c.runtime_status.value(runtime);
    }
    return {};
}

QVector<Capability> CapabilityRegistry::all() const {
    return capabilities_;
}

Result<Capability> CapabilityRegistry::find(const QString& capability_id) const {
    for (const auto& c : capabilities_) {
        if (c.id == capability_id)
            return Result<Capability>::ok(c);
    }
    return Result<Capability>::err(("unknown capability: " + capability_id).toStdString());
}

QStringList CapabilityRegistry::runtimes() const {
    return runtimes_;
}

} // namespace fincept::services
