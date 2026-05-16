#pragma once
#include <QFile>
#include <QString>
#include <QStringList>

namespace fincept::ui::formatting {

/// RFC 4180 CSV writer with UTF-8 BOM, CRLF terminators, and error tracking.
///
/// Why this exists: finterm has several screens that export CSV (GovData,
/// Excel, Portfolio, Watchlist, …) and each rolled its own. Without a BOM,
/// Excel on Windows treats UTF-8 as Windows-1252 and mangles non-ASCII
/// symbols — a real problem for the asia_markets screen. Without explicit
/// CRLF + binary mode, QIODevice::Text double-translates newlines inside
/// quoted fields.
///
/// Usage (typical):
///     CsvWriter w(path);
///     if (!w.ok()) { /* show error */ return; }
///     w.write_row({"SYM", "NAME", "PRICE"});
///     w.write_row({"AAPL", "Apple Inc.", "175.20"});
///     if (!w.finalize()) { /* show w.error() */ }
///
/// Errors stick: once a write fails, subsequent writes are no-ops and
/// `ok()` stays false. Callers check `finalize()` (or `ok()`) at the end
/// and surface `error()` to the user.
class CsvWriter {
  public:
    /// Open `path` for writing in binary mode and emit the UTF-8 BOM.
    /// Check `ok()` afterwards.
    explicit CsvWriter(const QString& path);

    /// Flushes + closes on destruction (best-effort, no throw). If you need
    /// to know whether the final flush succeeded, call `finalize()` first.
    ~CsvWriter();

    CsvWriter(const CsvWriter&) = delete;
    CsvWriter& operator=(const CsvWriter&) = delete;

    /// True iff every write so far has succeeded and the file is still open.
    bool ok() const { return ok_; }

    /// Human-readable error from the underlying QFile. Empty iff `ok()`.
    QString error() const { return error_; }

    /// Append a row with RFC 4180 quoting (",", "\"", "\n", "\r" in cell
    /// values are escaped). CRLF terminator. No-op when not `ok()`.
    void write_row(const QStringList& cells);

    /// Append a raw line — caller owns escaping. Used for non-CSV framing
    /// like Portfolio's "## HOLDINGS" section markers or "# Owner: Alice"
    /// comments. CRLF terminator. No-op when not `ok()`.
    void write_raw(const QString& line);

    /// Flush pending writes and confirm the file is in a good state. Returns
    /// `ok()` after the flush. Safe to call multiple times.
    bool finalize();

    /// RFC 4180 cell escape, exposed for callers that already manage their
    /// own QFile (rare — prefer the class).
    static QString escape_cell(const QString& s);

  private:
    void set_error(const QString& msg);

    QFile file_;
    bool ok_ = false;
    QString error_;
};

} // namespace fincept::ui::formatting
