#include "services/crypto/TotpService.h"

#include "core/logging/Logger.h"
#include "python/PythonRunner.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace finterm::services::crypto {

TotpService& TotpService::instance() {
    static TotpService s;
    return s;
}

void TotpService::generate(const QString& secret, TotpCallback cb) {
    if (secret.isEmpty()) {
        if (cb) {
            TotpResult r;
            r.error = "empty secret";
            cb(r);
        }
        return;
    }

    finterm::python::PythonRunner::instance().run(
        QStringLiteral("exchange/totp_gen.py"), {secret}, [cb = std::move(cb)](const finterm::python::PythonResult& result) {
            TotpResult out;
            if (!result.success) {
                out.error = result.output.isEmpty() ? QStringLiteral("totp_gen failed") : result.output;
                if (cb)
                    cb(out);
                return;
            }
            const auto doc = QJsonDocument::fromJson(result.output.toUtf8());
            if (!doc.isObject()) {
                out.error = "malformed totp_gen output";
                if (cb)
                    cb(out);
                return;
            }
            const auto obj = doc.object();
            if (!obj.contains("code")) {
                out.error = "totp_gen output missing 'code'";
                if (cb)
                    cb(out);
                return;
            }
            out.success = true;
            out.code = obj.value("code").toString();
            out.valid_for = obj.value("valid_for").toInt();
            if (cb)
                cb(out);
        });
}

} // namespace finterm::services::crypto
