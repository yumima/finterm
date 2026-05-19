// tests/services/video/test_live_hls_proxy.cpp
//
// Unit tests for LiveHlsProxy::trim_playlist — the pure function that
// turns a YouTube DVR HLS playlist (~3.5 MB / ~2800 segments) into a
// live-edge slice (~7 KB / 6 segments). Correctness here is load-bearing
// for the whole live-stream fix in commit 59a0e513, so we lock in:
//
//   - the basic shape transformation (header preserved, segment count
//     trimmed, MEDIA-SEQUENCE adjusted),
//   - per-segment tag carry-through (#EXT-X-DISCONTINUITY,
//     #EXT-X-PROGRAM-DATE-TIME) so libavformat sees correct context on
//     the segments we keep,
//   - defensive behaviour on malformed inputs (no #EXTM3U, no
//     #EXTINF, missing MEDIA-SEQUENCE).

#include "services/video/LiveHlsProxy.h"

#include <QObject>
#include <QTest>

using fincept::services::video::LiveHlsProxy;

class TestLiveHlsProxy : public QObject {
    Q_OBJECT

  private slots:

    // Baseline: 10-segment playlist trimmed to 4. Expected: last 4
    // segments survive, MEDIA-SEQUENCE bumped by 6 (the number we
    // dropped), header tags preserved.
    void trim_basic_case() {
        QByteArray input =
            "#EXTM3U\n"
            "#EXT-X-VERSION:3\n"
            "#EXT-X-TARGETDURATION:5\n"
            "#EXT-X-MEDIA-SEQUENCE:1000\n";
        for (int i = 0; i < 10; ++i) {
            input += "#EXTINF:5.0,\n";
            input += "https://cdn.example/seg-" + QByteArray::number(1000 + i) + ".ts\n";
        }
        const QByteArray out = LiveHlsProxy::trim_playlist(input, 4);

        // Header lines preserved (note MEDIA-SEQUENCE is rewritten).
        QVERIFY(out.contains("#EXTM3U"));
        QVERIFY(out.contains("#EXT-X-VERSION:3"));
        QVERIFY(out.contains("#EXT-X-TARGETDURATION:5"));
        // 6 segments dropped (1000..1005), so new seq = 1006.
        QVERIFY(out.contains("#EXT-X-MEDIA-SEQUENCE:1006"));
        // Only the last 4 segment URIs (1006..1009) should remain.
        QVERIFY(!out.contains("seg-1005.ts"));
        QVERIFY( out.contains("seg-1006.ts"));
        QVERIFY( out.contains("seg-1007.ts"));
        QVERIFY( out.contains("seg-1008.ts"));
        QVERIFY( out.contains("seg-1009.ts"));
        // Exactly 4 #EXTINF lines — no dangling history.
        QCOMPARE(out.count("#EXTINF:"), 4);
    }

    // keep > total: should keep all segments and rewrite MEDIA-SEQUENCE
    // to the original value (drop count = 0).
    void trim_keep_exceeds_total() {
        QByteArray input =
            "#EXTM3U\n"
            "#EXT-X-MEDIA-SEQUENCE:42\n"
            "#EXTINF:5.0,\n"
            "https://cdn.example/seg-42.ts\n"
            "#EXTINF:5.0,\n"
            "https://cdn.example/seg-43.ts\n";
        const QByteArray out = LiveHlsProxy::trim_playlist(input, 100);

        QVERIFY(out.contains("#EXT-X-MEDIA-SEQUENCE:42"));
        QVERIFY(out.contains("seg-42.ts"));
        QVERIFY(out.contains("seg-43.ts"));
        QCOMPARE(out.count("#EXTINF:"), 2);
    }

    // Per-segment tags (#EXT-X-DISCONTINUITY, #EXT-X-PROGRAM-DATE-TIME)
    // that appear immediately before an #EXTINF must travel with that
    // segment so libavformat sees correct continuity / timing context.
    void trim_preserves_per_segment_tags_on_kept_segments() {
        QByteArray input =
            "#EXTM3U\n"
            "#EXT-X-MEDIA-SEQUENCE:1\n"
            "#EXTINF:5.0,\n"
            "https://cdn.example/seg-1.ts\n"
            "#EXTINF:5.0,\n"
            "https://cdn.example/seg-2.ts\n"
            "#EXT-X-DISCONTINUITY\n"
            "#EXT-X-PROGRAM-DATE-TIME:2026-01-01T00:00:00Z\n"
            "#EXTINF:5.0,\n"
            "https://cdn.example/seg-3.ts\n";
        // Keep last 1 segment (seg-3). Its DISCONTINUITY + PDT tags
        // come along.
        const QByteArray out = LiveHlsProxy::trim_playlist(input, 1);
        QVERIFY(out.contains("seg-3.ts"));
        QVERIFY(out.contains("#EXT-X-DISCONTINUITY"));
        QVERIFY(out.contains("#EXT-X-PROGRAM-DATE-TIME:2026-01-01T00:00:00Z"));
        QVERIFY(!out.contains("seg-1.ts"));
        QVERIFY(!out.contains("seg-2.ts"));
    }

    // Per-segment tags attached to a DROPPED segment must not bleed into
    // the next kept segment. Validates that we attach tags to the
    // segment they belong to, not to whatever comes next.
    void trim_drops_tags_with_dropped_segments() {
        QByteArray input =
            "#EXTM3U\n"
            "#EXT-X-MEDIA-SEQUENCE:1\n"
            "#EXT-X-PROGRAM-DATE-TIME:2026-01-01T00:00:00Z\n"
            "#EXTINF:5.0,\n"
            "https://cdn.example/seg-1.ts\n"
            "#EXTINF:5.0,\n"
            "https://cdn.example/seg-2.ts\n";
        // Keep last 1. seg-1's PROGRAM-DATE-TIME (which is on the header
        // line before any #EXTINF) is a HEADER tag, so it's preserved.
        // But verify the segment-1 URL is dropped.
        const QByteArray out = LiveHlsProxy::trim_playlist(input, 1);
        QVERIFY(!out.contains("seg-1.ts"));
        QVERIFY( out.contains("seg-2.ts"));
    }

    // Defensive: input that doesn't start with #EXTM3U is returned
    // verbatim. Caller (LiveHlsProxy::on_upstream_finished) treats this
    // as a no-trim passthrough.
    void trim_non_hls_returns_input() {
        const QByteArray input = "not a playlist\nfoo\nbar\n";
        const QByteArray out = LiveHlsProxy::trim_playlist(input, 5);
        QCOMPARE(out, input);
    }

    // Master playlists (no #EXTINF, just #EXT-X-STREAM-INF + variant
    // URLs) shouldn't be trimmed — we'd corrupt the variant list.
    // Returned unchanged.
    void trim_master_playlist_returns_input() {
        const QByteArray input =
            "#EXTM3U\n"
            "#EXT-X-VERSION:3\n"
            "#EXT-X-STREAM-INF:BANDWIDTH=500000,RESOLUTION=640x360\n"
            "low.m3u8\n"
            "#EXT-X-STREAM-INF:BANDWIDTH=2000000,RESOLUTION=1280x720\n"
            "high.m3u8\n";
        const QByteArray out = LiveHlsProxy::trim_playlist(input, 6);
        QCOMPARE(out, input);
    }

    // Defensive: upstream lacking #EXT-X-MEDIA-SEQUENCE shouldn't lose
    // continuity tracking when we trim. We synthesize a MEDIA-SEQUENCE
    // header so libavformat treats the trimmed playlist as a continuing
    // live stream rather than a fresh start on every refresh.
    void trim_synthesizes_media_sequence_if_missing() {
        QByteArray input =
            "#EXTM3U\n"
            "#EXTINF:5.0,\nhttps://cdn.example/seg-1.ts\n"
            "#EXTINF:5.0,\nhttps://cdn.example/seg-2.ts\n"
            "#EXTINF:5.0,\nhttps://cdn.example/seg-3.ts\n";
        const QByteArray out = LiveHlsProxy::trim_playlist(input, 1);
        // We dropped 2, so the synthesized MEDIA-SEQUENCE should be 2.
        QVERIFY(out.contains("#EXT-X-MEDIA-SEQUENCE:2"));
        QVERIFY(out.contains("seg-3.ts"));
    }

    // Windows line endings (\r\n) shouldn't trip up the parser — the
    // upstream CDN may send either form depending on the variant
    // encoder.
    void trim_handles_crlf_line_endings() {
        QByteArray input =
            "#EXTM3U\r\n"
            "#EXT-X-MEDIA-SEQUENCE:1\r\n"
            "#EXTINF:5.0,\r\n"
            "https://cdn.example/seg-1.ts\r\n"
            "#EXTINF:5.0,\r\n"
            "https://cdn.example/seg-2.ts\r\n";
        const QByteArray out = LiveHlsProxy::trim_playlist(input, 1);
        QVERIFY(out.contains("seg-2.ts"));
        QVERIFY(!out.contains("seg-1.ts"));
        // MEDIA-SEQUENCE incremented by drop count.
        QVERIFY(out.contains("#EXT-X-MEDIA-SEQUENCE:2"));
    }
};

QTEST_GUILESS_MAIN(TestLiveHlsProxy)
#include "test_live_hls_proxy.moc"
