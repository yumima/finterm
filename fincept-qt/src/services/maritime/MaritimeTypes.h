// src/services/maritime/MaritimeTypes.h
#pragma once
#include <QColor>
#include <QString>
#include <QStringList>
#include <QVector>

namespace fincept::services::maritime {

// ── Vessel data from API ────────────────────────────────────────────────────

struct VesselData {
    int id = 0;
    QString imo;
    QString name;
    double latitude = 0;
    double longitude = 0;
    double speed = 0;
    double angle = 0;
    QString from_port;
    QString to_port;
    QString from_date;
    QString to_date;
    double route_progress = 0;
    double draught = 0;
    QString last_updated;
    QString fetched_at;
};

// ── Trade route corridor ────────────────────────────────────────────────────

struct TradeRoute {
    QString name;
    QString value;  // e.g. "$45B"
    QString status; // active, delayed, critical
    int vessels = 0;
    double start_lat = 0, start_lng = 0;
    double end_lat = 0, end_lng = 0;
};

inline QColor route_status_color(const QString& status) {
    if (status == "critical")
        return QColor("#FF0000");
    if (status == "delayed")
        return QColor("#FFD700");
    if (status == "active")
        return QColor("#00FF00");
    return QColor("#808080"); // unknown — never imply "active"
}

// ── Intelligence stats ──────────────────────────────────────────────────────

struct IntelligenceData {
    QString threat_level; // low, medium, high, critical
    int active_vessels = 0;
    int monitored_routes = 0;  // 0 = unknown (no data source)
    QString trade_volume;      // empty = unavailable
};

inline QColor threat_color(const QString& level) {
    if (level == "critical")
        return QColor("#FF0000");
    if (level == "high")
        return QColor("#FF6600");
    if (level == "medium")
        return QColor("#FFD700");
    return QColor("#00FF00"); // low
}

// ── Preset port locations ───────────────────────────────────────────────────

struct PresetPort {
    QString name;
    double lat;
    double lng;
};

inline QVector<PresetPort> preset_ports() {
    return {
        {"Mumbai Port", 18.9388, 72.8354},    {"Shanghai Port", 31.3548, 121.6431},
        {"Singapore Port", 1.2644, 103.8224}, {"Hong Kong Port", 22.2855, 114.1577},
        {"Rotterdam Port", 51.9553, 4.1392},  {"Dubai Port", 24.9857, 55.0272},
    };
}

// No default trade routes: the corridor list that used to live here carried
// invented trade values, statuses and vessel counts with no data source.
// Corridors are only shown when a real source populates them.

// ── Area search params ──────────────────────────────────────────────────────

struct AreaSearchParams {
    double min_lat = 18.5;
    double max_lat = 19.5;
    double min_lng = 72.0;
    double max_lng = 73.5;
    int days_ago = 0;
};

} // namespace fincept::services::maritime

#include <QMetaType>
Q_DECLARE_METATYPE(fincept::services::maritime::VesselData)
Q_DECLARE_METATYPE(QVector<fincept::services::maritime::VesselData>)
