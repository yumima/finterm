#include "ui/formatting/CsvWriter.h"

namespace fincept::ui::formatting {

namespace {
constexpr const char kUtf8Bom[] = "\xEF\xBB\xBF";
constexpr const char kCrLf[] = "\r\n";
} // namespace

CsvWriter::CsvWriter(const QString& path) : file_(path) {
    if (!file_.open(QIODevice::WriteOnly)) {
        set_error(file_.errorString());
        return;
    }
    if (file_.write(kUtf8Bom, sizeof(kUtf8Bom) - 1) != qint64(sizeof(kUtf8Bom) - 1)) {
        set_error(file_.errorString());
        return;
    }
    ok_ = true;
}

CsvWriter::~CsvWriter() {
    if (file_.isOpen())
        file_.close();
}

void CsvWriter::write_row(const QStringList& cells) {
    if (!ok_)
        return;
    QString line;
    line.reserve(cells.size() * 16);
    bool first = true;
    for (const auto& c : cells) {
        if (!first)
            line.append(QLatin1Char(','));
        line.append(escape_cell(c));
        first = false;
    }
    write_raw(line);
}

void CsvWriter::write_raw(const QString& line) {
    if (!ok_)
        return;
    const QByteArray utf8 = line.toUtf8();
    if (!utf8.isEmpty() && file_.write(utf8) != utf8.size()) {
        set_error(file_.errorString());
        return;
    }
    if (file_.write(kCrLf, sizeof(kCrLf) - 1) != qint64(sizeof(kCrLf) - 1))
        set_error(file_.errorString());
}

bool CsvWriter::finalize() {
    if (!ok_)
        return false;
    if (!file_.flush()) {
        set_error(file_.errorString());
        return false;
    }
    return ok_;
}

QStringList CsvWriter::parse_row(const QString& line_in) {
    // Strip leading UTF-8 BOM (U+FEFF) if present — happens on the first
    // line of files our own CsvWriter creates.
    QStringView line = line_in;
    if (!line.isEmpty() && line.front() == QChar(0xFEFF))
        line = line.mid(1);

    QStringList fields;
    QString cell;
    bool in_quotes = false;
    for (int i = 0; i < line.size(); ++i) {
        const QChar c = line[i];
        if (in_quotes) {
            if (c == QLatin1Char('"')) {
                // Doubled "" inside a quoted field → literal "
                if (i + 1 < line.size() && line[i + 1] == QLatin1Char('"')) {
                    cell.append(QLatin1Char('"'));
                    ++i;
                } else {
                    in_quotes = false;
                }
            } else {
                cell.append(c);
            }
        } else {
            if (c == QLatin1Char(',')) {
                fields.append(cell);
                cell.clear();
            } else if (c == QLatin1Char('"') && cell.isEmpty()) {
                in_quotes = true;
            } else {
                cell.append(c);
            }
        }
    }
    fields.append(cell);
    return fields;
}

QString CsvWriter::escape_cell(const QString& s) {
    const bool needs_quoting = s.contains(QLatin1Char(',')) || s.contains(QLatin1Char('"')) ||
                               s.contains(QLatin1Char('\n')) || s.contains(QLatin1Char('\r'));
    if (!needs_quoting)
        return s;
    QString escaped;
    escaped.reserve(s.size() + 2);
    escaped.append(QLatin1Char('"'));
    for (QChar ch : s) {
        if (ch == QLatin1Char('"'))
            escaped.append(QLatin1Char('"'));
        escaped.append(ch);
    }
    escaped.append(QLatin1Char('"'));
    return escaped;
}

void CsvWriter::set_error(const QString& msg) {
    error_ = msg;
    ok_ = false;
}

} // namespace fincept::ui::formatting
