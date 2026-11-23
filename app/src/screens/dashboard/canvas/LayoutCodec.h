#pragma once
// LayoutCodec.h — the ONE on-disk form of a grid canvas layout (tiles,
// positions and each tile's config), shared by every canvas that is saved:
// DASHBOARD and Markets ▸ OVERVIEW. Base64 of a QDataStream blob.

#include "screens/dashboard/canvas/GridLayout.h"

#include <QByteArray>
#include <QDataStream>
#include <QIODevice>
#include <QString>

#include <algorithm>
#include <optional>

namespace finterm::screens::layout_codec {

// Header for the v1 blob that also persists per-widget config. The magic
// distinguishes it from the legacy (config-less, header-less) format so old
// saved layouts still load.
inline constexpr quint32 kMagic = 0xDA58B0A7;
inline constexpr qint32 kVersion = 2; // 2: + rows (fit-to-view canvases), after the tiles

inline QString encode(const GridLayout& layout) {
    QByteArray buf;
    QDataStream stream(&buf, QIODevice::WriteOnly);
    stream << kMagic << kVersion;
    stream << layout.cols << layout.row_h << layout.margin;
    stream << static_cast<int>(layout.items.size());
    for (const auto& item : layout.items) {
        stream << item.id << item.instance_id;
        stream << item.cell.x << item.cell.y << item.cell.w << item.cell.h;
        stream << item.cell.min_w << item.cell.min_h;
        stream << item.config; // per-instance config (broker/account/symbol/…)
    }
    // v2: after the tiles, so a build that reads v1 stops before it.
    stream << qint32(layout.rows);
    return QString::fromLatin1(buf.toBase64());
}

/// nullopt for an empty, truncated or corrupt blob — the caller falls back to
/// its default layout rather than tiles half-read at (0,0).
inline std::optional<GridLayout> decode(const QString& b64) {
    if (b64.isEmpty())
        return std::nullopt;
    QByteArray data = QByteArray::fromBase64(b64.toUtf8());
    QDataStream stream(&data, QIODevice::ReadOnly);
    quint32 magic = 0;
    stream >> magic;
    const bool has_config = magic == kMagic;
    qint32 version = 0;
    if (has_config) {
        stream >> version;
    } else {
        stream.device()->seek(0); // the legacy format — re-read from the start
    }
    GridLayout layout;
    int count = 0;
    stream >> layout.cols >> layout.row_h >> layout.margin >> count;
    if (count <= 0 || count > 100)
        return std::nullopt;
    for (int i = 0; i < count; ++i) {
        GridItem item;
        stream >> item.id >> item.instance_id;
        stream >> item.cell.x >> item.cell.y >> item.cell.w >> item.cell.h;
        stream >> item.cell.min_w >> item.cell.min_h;
        if (has_config)
            stream >> item.config;
        layout.items.append(item);
    }
    if (version >= 2) {
        qint32 rows = 0;
        stream >> rows;
        layout.rows = std::max(0, int(rows));
    }
    if (stream.status() != QDataStream::Ok)
        return std::nullopt;
    return layout;
}

} // namespace finterm::screens::layout_codec
