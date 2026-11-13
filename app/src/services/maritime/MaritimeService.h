// src/services/maritime/MaritimeService.h
#pragma once
#include "datahub/Producer.h"
#include "services/maritime/MaritimeTypes.h"

#include <QObject>

namespace finterm::services::maritime {

/// Singleton service for Maritime Intelligence — HTTP calls to /marine/ on the localhost stub.
class MaritimeService : public QObject, public finterm::datahub::Producer {
    Q_OBJECT
  public:
    static MaritimeService& instance();

    /// Register with the hub + install maritime:* policies. Idempotent.
    void ensure_registered_with_hub();

    // ── finterm::datahub::Producer ─────────────────────────────────────────
    QStringList topic_patterns() const override;
    void refresh(const QStringList& topics) override;
    int max_requests_per_sec() const override;

    /// False when no vessel-position API is configured (current state: the
    /// upstream endpoint was retired). Callers must show "unavailable".
    static bool is_available();
    static const char* unavailable_message();

    /// Search vessels in a geographic bounding box
    void search_vessels_by_area(const AreaSearchParams& params);

    /// Get single vessel position by IMO number
    void get_vessel_position(const QString& imo);

    /// Get multiple vessel positions by IMO array
    void get_multi_vessel_positions(const QStringList& imos);

    /// Get vessel history
    void get_vessel_history(const QString& imo);

    /// Health check for marine API module
    void check_health();

  signals:
    void vessels_loaded(QVector<finterm::services::maritime::VesselData> vessels, int total);
    void vessel_found(finterm::services::maritime::VesselData vessel);
    void vessel_history_loaded(QVector<finterm::services::maritime::VesselData> history);
    void health_loaded(QJsonObject data);
    void error_occurred(QString context, QString message);

  private:
    explicit MaritimeService(QObject* parent = nullptr);
    Q_DISABLE_COPY(MaritimeService)

    VesselData parse_vessel(const QJsonObject& obj) const;

    bool hub_registered_ = false;
};

} // namespace finterm::services::maritime
