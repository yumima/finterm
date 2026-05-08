// src/services/pre_ipo/PreIpoService.cpp
#include "services/pre_ipo/PreIpoService.h"

#include "core/logging/Logger.h"
#include "python/PythonRunner.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>

namespace fincept::services {

using namespace fincept::pre_ipo;

// ── Singleton ────────────────────────────────────────────────────────────────

PreIpoService& PreIpoService::instance() {
    static PreIpoService inst;
    return inst;
}

PreIpoService::PreIpoService(QObject* parent) : QObject(parent) {}

// ── Public API ────────────────────────────────────────────────────────────────

void PreIpoService::load_data() {
    if (loaded_) return;
    load_from_sec();
}

void PreIpoService::load_from_sec() {
    if (loading_) return;   // prevent concurrent spawns

    if (!python::PythonRunner::instance().is_available()) {
        LOG_WARN("PreIpo", "Python not available — cannot load SEC EDGAR data");
        emit error_occurred(QStringLiteral("Python runtime unavailable."));
        return;  // loading_ stays false — retry when Python becomes available
    }

    loading_ = true;

    QPointer<PreIpoService> self = this;
    python::PythonRunner::instance().run(
        QStringLiteral("sec_form_d_data.py"),
        {QStringLiteral("all_data"), QStringLiteral("{}")},
        [self](python::PythonResult result) {
            if (!self) return;
            self->loading_ = false;  // always clear the guard

            if (!result.success || result.output.trimmed().isEmpty()) {
                LOG_WARN("PreIpo", "sec_form_d_data.py failed: " + result.error.left(200));
                emit self->error_occurred(
                    QStringLiteral("SEC EDGAR data unavailable. Check network connection."));
                return;  // loaded_ stays false so a retry is possible
            }
            const QString json_str = python::extract_json(result.output);
            const auto    doc      = QJsonDocument::fromJson(json_str.toUtf8());
            if (!doc.isObject()) {
                LOG_WARN("PreIpo", "Invalid JSON from sec_form_d_data.py");
                emit self->error_occurred(QStringLiteral("Invalid SEC data format."));
                return;
            }
            self->parse_sec_summary(doc.object());
        });
}

void PreIpoService::parse_sec_summary(const QJsonObject& root) {
    companies_.clear();
    form_d_.clear();
    pipeline_.clear();

    // ── Form D companies ──────────────────────────────────────────────────────
    for (const auto& v : root.value(QStringLiteral("form_d_companies")).toArray()) {
        const auto o = v.toObject();
        PrivateCompany c;
        c.id          = o[QStringLiteral("id")].toString();
        c.name        = o[QStringLiteral("name")].toString();
        c.sector      = o[QStringLiteral("sector")].toString();
        c.hq_state    = o[QStringLiteral("state")].toString();  // two-letter US state from Form D
        c.hq_country  = QStringLiteral("USA");
        c.ipo_status  = IpoStatus::Unknown;
        c.description = QStringLiteral("Source: SEC EDGAR Form D");

        // Only fields actually in SEC Form D — no invented valuations
        for (const auto& rv : o[QStringLiteral("rounds")].toArray()) {
            const auto ro = rv.toObject();
            FundingRound r;
            r.round_name   = QStringLiteral("Form D");
            r.date         = QDate::fromString(ro[QStringLiteral("date")].toString(), Qt::ISODate);
            r.amount_usd   = ro[QStringLiteral("amount_m")].toDouble();
            r.lead_investors = {QStringLiteral("SEC EDGAR")};
            c.rounds.append(r);
        }
        if (!c.name.isEmpty())
            companies_.append(c);
    }

    // ── IPO pipeline (S-1 filers) ─────────────────────────────────────────────
    for (const auto& v : root.value(QStringLiteral("s1_filings")).toArray()) {
        const auto o = v.toObject();
        S1Filing f;
        f.company_name  = o[QStringLiteral("company_name")].toString();
        f.filed_date    = QDate::fromString(o[QStringLiteral("filed_date")].toString(), Qt::ISODate);
        f.edgar_url     = o[QStringLiteral("edgar_url")].toString();
        if (!f.company_name.isEmpty())
            pipeline_.append(f);
    }

    // ── Recent Form D filings ─────────────────────────────────────────────────
    for (const auto& v : root.value(QStringLiteral("recent_form_d")).toArray()) {
        const auto o = v.toObject();
        FormDFiling f;
        f.company_name  = o[QStringLiteral("company_name")].toString();
        f.filed_date    = QDate::fromString(o[QStringLiteral("filed_date")].toString(), Qt::ISODate);
        f.amount_raised = o[QStringLiteral("amount_raised")].toDouble();
        f.exemption     = o[QStringLiteral("exemption")].toString();
        f.offering_type = o[QStringLiteral("offering_type")].toString();
        f.state         = o[QStringLiteral("state")].toString();
        f.edgar_url     = o[QStringLiteral("edgar_url")].toString();
        if (!f.company_name.isEmpty())
            form_d_.append(f);
    }

    if (companies_.isEmpty() && pipeline_.isEmpty()) {
        // Both empty after a successful parse means no SEC data in the date range.
        // Show an error rather than fake data.
        emit error_occurred(
            QStringLiteral("No SEC EDGAR data found in the selected date range."));
        return;  // loaded_ stays false — retry will re-fetch
    }
    emit_loaded();
}

void PreIpoService::emit_loaded() {
    loaded_ = true;
    PreIpoSummary summary;
    summary.companies     = companies_;
    summary.recent_form_d = form_d_;
    summary.ipo_pipeline  = pipeline_;
    summary.last_updated  = QDateTime::currentDateTime();
    summary.loaded        = true;
    emit data_loaded(summary);
}

QVector<PrivateCompany> PreIpoService::companies() const {
    return companies_;
}

PrivateCompany PreIpoService::company(const QString& id) const {
    for (const auto& c : companies_) {
        if (c.id == id)
            return c;
    }
    return {};
}

void PreIpoService::toggle_watch(const QString& id) {
    for (auto& c : companies_) {
        if (c.id == id) {
            c.watched = !c.watched;
            emit company_updated(id);
            return;
        }
    }
}

QVector<FormDFiling> PreIpoService::recent_form_d() const {
    return form_d_;
}

QVector<S1Filing> PreIpoService::ipo_pipeline() const {
    return pipeline_;
}

} // namespace fincept::services
