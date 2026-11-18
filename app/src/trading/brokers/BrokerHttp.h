#pragma once
// BrokerHttp — synchronous HTTP helper for broker API calls
// Uses QNetworkAccessManager with a local event loop for blocking calls.
// This keeps broker implementations simple (no callback chains).

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QString>

namespace finterm::trading {

struct BrokerHttpResponse {
    bool success = false;
    int status_code = 0;
    QJsonObject json;
    QString raw_body;
    QString error;
};

class BrokerHttp {
  public:
    static BrokerHttp& instance();

    BrokerHttpResponse get(const QString& url, const QMap<QString, QString>& headers = {});

    BrokerHttpResponse post_json(const QString& url, const QJsonObject& payload,
                                 const QMap<QString, QString>& headers = {});

    BrokerHttpResponse put_json(const QString& url, const QJsonObject& payload,
                                const QMap<QString, QString>& headers = {});

    BrokerHttpResponse patch_json(const QString& url, const QJsonObject& payload,
                                  const QMap<QString, QString>& headers = {});

    BrokerHttpResponse del(const QString& url, const QMap<QString, QString>& headers = {},
                           const QJsonObject& payload = {});

    // Raw body POST — caller provides pre-encoded body + headers
    BrokerHttpResponse post_raw(const QString& url, const QByteArray& body, const QMap<QString, QString>& headers = {});

    // Raw body PUT — caller provides pre-encoded body + headers (Tradier modify_order)
    BrokerHttpResponse put_raw(const QString& url, const QByteArray& body, const QMap<QString, QString>& headers = {});

  private:
    BrokerHttp() = default;
    BrokerHttpResponse execute(const QString& method, const QString& url, const QByteArray& body,
                               const QString& content_type, const QMap<QString, QString>& headers);
    int timeout_ms_ = 8000;
};

} // namespace finterm::trading
