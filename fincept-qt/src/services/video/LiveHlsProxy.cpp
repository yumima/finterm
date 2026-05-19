// src/services/video/LiveHlsProxy.cpp
#include "services/video/LiveHlsProxy.h"

#include "core/logging/Logger.h"

#include <QByteArrayList>
#include <QNetworkRequest>

namespace fincept::services::video {

LiveHlsProxy::LiveHlsProxy(QObject* parent) : QObject(parent) {
    server_        = new QTcpServer(this);
    netman_        = new QNetworkAccessManager(this);
    refresh_timer_ = new QTimer(this);
    refresh_timer_->setInterval(kRefreshIntervalMs);

    connect(server_, &QTcpServer::newConnection,
            this, &LiveHlsProxy::on_new_connection);
    connect(refresh_timer_, &QTimer::timeout,
            this, &LiveHlsProxy::on_refresh_timer);
}

LiveHlsProxy::~LiveHlsProxy() {
    stop();
}

bool LiveHlsProxy::start(const QUrl& upstream) {
    if (!upstream.isValid() || upstream.scheme().isEmpty()) {
        last_error_ = QStringLiteral("upstream URL is not valid");
        return false;
    }
    stop();  // idempotent reset

    upstream_url_ = upstream;
    trimmed_cache_.clear();
    cache_age_.invalidate();
    first_ready_emitted_ = false;
    stale_error_emitted_ = false;
    last_error_.clear();

    // Bind ephemeral port on loopback. Letting the OS pick the port
    // avoids hardcoded-port collisions between multiple finterm instances
    // or back-to-back relays in the same instance.
    if (!server_->listen(QHostAddress::LocalHost, 0)) {
        last_error_ = QStringLiteral("could not bind localhost port: ") + server_->errorString();
        return false;
    }
    local_url_ = QUrl(QStringLiteral("http://127.0.0.1:%1/play.m3u8")
                          .arg(server_->serverPort()));

    LOG_DEBUG("LiveHlsProxy",
              "started: upstream=" + upstream_url_.toString(QUrl::RemoveQuery).left(80) +
              " local=" + local_url_.toString());

    // Kick off the first upstream fetch immediately so the ready signal
    // can fire ASAP — caller typically waits for ready before calling
    // setSource on QMediaPlayer.
    on_refresh_timer();
    refresh_timer_->start();
    return true;
}

void LiveHlsProxy::stop() {
    if (server_->isListening())
        server_->close();
    refresh_timer_->stop();
    if (in_flight_) {
        // Detach our slot before abort so on_upstream_finished doesn't run
        // on a half-destroyed reply during widget teardown.
        disconnect(in_flight_, nullptr, this, nullptr);
        in_flight_->abort();
        in_flight_->deleteLater();
        in_flight_ = nullptr;
    }
    trimmed_cache_.clear();
    cache_age_.invalidate();
    local_url_.clear();
}

bool LiveHlsProxy::cache_is_fresh() const {
    return !trimmed_cache_.isEmpty()
        && cache_age_.isValid()
        && cache_age_.elapsed() < kMaxCacheAgeMs;
}

void LiveHlsProxy::on_new_connection() {
    while (auto* client = server_->nextPendingConnection()) {
        // We don't actually parse the request beyond reading the line
        // (QMediaPlayer sends a normal HTTP/1.1 GET). Single-shot
        // response then close — no keep-alive.
        connect(client, &QTcpSocket::readyRead, this, [this, client]() {
            // Drain the request bytes; we don't care about the contents,
            // we only care that the request was received so we know to
            // respond. Idempotent — if Qt sends the request in multiple
            // reads we just discard whatever arrives.
            client->readAll();
            // Send response once. Use a flag to avoid double-send if
            // readyRead fires again before disconnect.
            if (!client->property("served").toBool()) {
                client->setProperty("served", true);
                serve_request(client);
            }
        });
        connect(client, &QTcpSocket::disconnected, client, &QObject::deleteLater);
    }
}

void LiveHlsProxy::serve_request(QTcpSocket* client) {
    if (!cache_is_fresh()) {
        // Either first fetch hasn't completed yet, or the upstream has
        // been unreachable for kMaxCacheAgeMs+ and our cached segment
        // URLs are probably stale (YouTube signed-URL TTL on segments).
        // Serve 503 + Retry-After. If the cache went stale (was once
        // populated, now expired), latch an upstream_error so the widget
        // can surface "live stream lost" to the user.
        if (!trimmed_cache_.isEmpty() && !stale_error_emitted_) {
            stale_error_emitted_ = true;
            LOG_WARN("LiveHlsProxy",
                     "cache aged out (>"
                     + QString::number(kMaxCacheAgeMs / 1000) + "s without successful refresh)");
            // Drop the now-untrustworthy cache so future requests get a
            // clean 503 instead of risking 6-hour-old segment URLs.
            trimmed_cache_.clear();
            cache_age_.invalidate();
            emit upstream_error(QStringLiteral("Live stream lost — upstream unreachable for over %1s")
                                    .arg(kMaxCacheAgeMs / 1000));
        }
        const QByteArray msg =
            "HTTP/1.1 503 Service Unavailable\r\n"
            "Content-Type: text/plain\r\n"
            "Retry-After: 1\r\n"
            "Content-Length: 14\r\n"
            "Connection: close\r\n\r\n"
            "not ready yet";
        client->write(msg);
        client->flush();
        client->disconnectFromHost();
        return;
    }
    QByteArray response;
    response.reserve(trimmed_cache_.size() + 200);
    response += "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/vnd.apple.mpegurl\r\n"
                "Cache-Control: no-cache, no-store, must-revalidate\r\n"
                "Connection: close\r\n"
                "Content-Length: ";
    response += QByteArray::number(trimmed_cache_.size());
    response += "\r\n\r\n";
    response += trimmed_cache_;
    client->write(response);
    client->flush();
    client->disconnectFromHost();
}

void LiveHlsProxy::on_refresh_timer() {
    if (in_flight_) {
        // Previous fetch hasn't finished — skip this tick rather than
        // pile up parallel requests. Upstream is slow; that's the whole
        // reason this proxy exists.
        return;
    }
    QNetworkRequest req(upstream_url_);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36"));
    req.setTransferTimeout(kFetchTimeoutMs);
    // Browsers send these headers; helps with CDN sticky-edge routing.
    req.setRawHeader("Accept", "*/*");

    in_flight_ = netman_->get(req);
    connect(in_flight_, &QNetworkReply::finished,
            this, &LiveHlsProxy::on_upstream_finished);
}

void LiveHlsProxy::on_upstream_finished() {
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    // The in_flight_ guard pairs with this — release it before doing any
    // work so the next refresh tick can spawn a new fetch if needed.
    if (reply == in_flight_) in_flight_ = nullptr;

    if (reply->error() != QNetworkReply::NoError) {
        const QString err = reply->errorString();
        LOG_WARN("LiveHlsProxy", "upstream fetch failed: " + err.left(180));
        // Only surface the error if we don't have a cached playlist to
        // serve. Once we've succeeded once, transient refresh failures
        // are silent and the client keeps playing from the previous cache
        // for one or two cycles.
        if (trimmed_cache_.isEmpty()) {
            last_error_ = err;
            emit upstream_error(err);
        }
        reply->deleteLater();
        return;
    }
    const QByteArray raw = reply->readAll();
    reply->deleteLater();

    const QByteArray trimmed = trim_playlist(raw, kEdgeSegmentsToKeep);
    if (trimmed.isEmpty()) {
        LOG_WARN("LiveHlsProxy", "trim returned empty; ignoring this refresh");
        return;
    }
    trimmed_cache_ = trimmed;
    cache_age_.restart();
    // Reset the stale-error latch — we're freshly populated again, so
    // any future aging-out should be reported as a new event.
    stale_error_emitted_ = false;

    if (!first_ready_emitted_) {
        first_ready_emitted_ = true;
        LOG_DEBUG("LiveHlsProxy",
                  "ready: upstream=" + QString::number(raw.size()) +
                  "B trimmed=" + QString::number(trimmed.size()) + "B");
        emit ready();
    }
}

// Parse an HLS variant playlist and return a new playlist containing only
// the last `keep` segments. Header lines (#EXTM3U, #EXT-X-VERSION,
// #EXT-X-TARGETDURATION, etc.) are preserved. #EXT-X-MEDIA-SEQUENCE is
// rewritten so the trimmed playlist's first segment carries the original
// sequence number — libavformat uses that for live-edge tracking.
//
// Per-segment lines we keep with each segment: #EXTINF (required) and any
// other #EXT-X- tags that appear between the previous segment URI and this
// segment's #EXTINF (e.g., #EXT-X-DISCONTINUITY, #EXT-X-PROGRAM-DATE-TIME,
// #EXT-X-DATERANGE). Dropping them on the trim boundary is safe — the
// trimmed playlist still parses as valid HLS, just without the historical
// markers we're discarding anyway.
//
// If the input doesn't look like an HLS playlist (no #EXTM3U on first
// non-empty line) we return it unchanged so the caller sees the upstream
// response verbatim and can decide what to do.
QByteArray LiveHlsProxy::trim_playlist(const QByteArray& upstream, int keep_segments) {
    // Quick reject: not HLS at all.
    if (!upstream.startsWith("#EXTM3U")) {
        return upstream;
    }

    const QList<QByteArray> lines = upstream.split('\n');
    QByteArrayList header;
    // Each segment is [tag-lines..., #EXTINF:..., URI]. We accumulate
    // tag lines into `pending_tags` until we see #EXTINF, then take the
    // next non-comment non-empty line as the URI.
    struct Segment {
        QByteArrayList tags;     ///< zero or more #EXT-X-* lines before #EXTINF
        QByteArray extinf;       ///< the #EXTINF:... line itself
        QByteArray uri;          ///< the URL for this segment
    };
    QList<Segment> segments;
    QByteArrayList pending_tags;
    QByteArray pending_extinf;
    bool seen_first_extinf = false;

    for (const QByteArray& raw_line : lines) {
        QByteArray line = raw_line;
        // Strip trailing CR (Windows line endings).
        if (line.endsWith('\r')) line.chop(1);
        if (line.isEmpty()) continue;

        if (line.startsWith("#EXTINF:")) {
            seen_first_extinf = true;
            pending_extinf = line;
            continue;
        }
        if (line.startsWith('#')) {
            if (!seen_first_extinf) {
                header.append(line);
            } else {
                // Tag between previous segment URI and the next #EXTINF —
                // belongs to the NEXT segment.
                pending_tags.append(line);
            }
            continue;
        }
        // Non-comment, non-empty line. If we have a pending #EXTINF, this
        // is the segment URI. Otherwise it's a stray line; ignore.
        if (!pending_extinf.isEmpty()) {
            segments.append({pending_tags, pending_extinf, line});
            pending_tags.clear();
            pending_extinf.clear();
        }
    }

    if (segments.isEmpty()) {
        // Master playlist (no #EXTINF), or empty — return as-is. The
        // caller is expected to give us a VARIANT playlist; master
        // playlists would need variant selection upstream of us.
        return upstream;
    }

    const int total = segments.size();
    const int keep  = qMin(keep_segments, total);
    const int drop  = total - keep;

    // Rewrite #EXT-X-MEDIA-SEQUENCE so the first kept segment carries the
    // sequence number it actually had in the upstream. libavformat tracks
    // segment continuity by this value during refresh.
    bool seq_rewritten = false;
    for (int i = 0; i < header.size(); ++i) {
        if (header[i].startsWith("#EXT-X-MEDIA-SEQUENCE:")) {
            const int colon = header[i].indexOf(':');
            const qlonglong orig = header[i].mid(colon + 1).toLongLong();
            header[i] = "#EXT-X-MEDIA-SEQUENCE:" + QByteArray::number(orig + drop);
            seq_rewritten = true;
            break;
        }
    }
    if (!seq_rewritten) {
        // Defensive: if upstream lacked the header, add one so libavformat
        // doesn't assume sequence 0 (which would look like a brand-new
        // playlist on every refresh and cause a re-buffer).
        header.append("#EXT-X-MEDIA-SEQUENCE:" + QByteArray::number(drop));
    }

    QByteArray out;
    out.reserve(upstream.size() / total * keep + 256);
    for (const QByteArray& h : header) {
        out += h; out += '\n';
    }
    for (int i = drop; i < total; ++i) {
        const Segment& s = segments[i];
        for (const QByteArray& t : s.tags) {
            out += t; out += '\n';
        }
        out += s.extinf; out += '\n';
        out += s.uri;    out += '\n';
    }
    return out;
}

} // namespace fincept::services::video
