#pragma once
// CapabilityRegistry.h — declarative AI feature gating.
//
// Loads `resources/ai/capabilities.json` at startup and exposes:
//
//   - `supports(runtime, capability_id)` — boolean gate for UI
//   - `status(runtime, capability_id)` — "supported" / "unsupported" /
//     "partial" / "planned" / "" (unknown)
//   - `all()` — full matrix, for the docs-generator and the planned
//     Workbench capability tab.
//
// The matrix file is the single source of truth; the C++ side, the
// Python doc-generator, and `docs/how-to/capabilities.md` all read
// from it.  Adding a capability is a one-file change.
//
// When the JSON file is missing (e.g. a stripped runtime install
// without resources/) every `supports()` returns false — UI features
// gracefully degrade rather than crashing.

#include "core/result/Result.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace fincept::services {

struct Capability {
    QString id;
    QString label;
    QString description;
    /// runtime → status (e.g. "anthropic" → "supported"). Missing
    /// entries mean unknown; treat as unsupported by default.
    QHash<QString, QString> runtime_status;
    /// Per-runtime notes that go alongside the status (caveats,
    /// SDK-version dependencies).  Keys: "notes_<runtime>" plus a
    /// generic "notes" for cross-runtime caveats.
    QHash<QString, QString> notes;
    /// Free-form list of UI surfaces / code paths that hang off this
    /// capability.  Used by the docs-generator.
    QStringList ui_surface;
};

class CapabilityRegistry {
  public:
    static CapabilityRegistry& instance();

    /// Convenience: returns true iff status is "supported" or "partial".
    bool supports(const QString& runtime, const QString& capability_id) const;

    /// Raw status string for (runtime, capability).  Empty when the
    /// capability or runtime is unknown.
    QString status(const QString& runtime, const QString& capability_id) const;

    QVector<Capability> all() const;

    /// Lookup by id.  Returns Result::err when not found.
    Result<Capability> find(const QString& capability_id) const;

    /// All runtime names declared in the matrix.
    QStringList runtimes() const;

    CapabilityRegistry(const CapabilityRegistry&) = delete;
    CapabilityRegistry& operator=(const CapabilityRegistry&) = delete;

  private:
    CapabilityRegistry();
    void load();

    QVector<Capability> capabilities_;
    QStringList runtimes_;
};

} // namespace fincept::services
