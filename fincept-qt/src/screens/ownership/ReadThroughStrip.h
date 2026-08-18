#pragma once
#include <QVector>
#include <QWidget>

#include "screens/ownership/OwnershipSignals.h"

namespace fincept::screens {

/// The register's read-through as scannable tokens rather than paragraphs.
///
/// The reads themselves are the right content — each one carries the number
/// that produced it and the rule that was applied — but rendered as a stack of
/// prose cards they cost more attention than they save. A reader scanning a
/// register wants to know IN ONE GLANCE which findings are present and which of
/// them are serious; the sentence explaining a finding is what they want second,
/// about one finding, once they have decided it matters.
///
/// So each read becomes a chip: a severity bar, and the headline. The bar
/// encodes Weight — the only ordering the signals layer actually asserts — so
/// severity reads as height before any word is processed. Chips are ordered
/// strongest first within each lens, which puts the thing worth knowing at the
/// top left where reading starts.
///
/// The full sentence and the threshold behind it live in the tooltip. Nothing
/// is removed, only deferred: hovering a chip gives exactly what the card used
/// to show, for the one finding the reader chose.
class ReadThroughStrip : public QWidget {
    Q_OBJECT
  public:
    explicit ReadThroughStrip(QWidget* parent = nullptr);

    void set_reads(const QVector<ownership::Read>& reads);
    /// Shown in place of chips when there are none — loading, or a register
    /// that crosses no threshold, which is itself a finding.
    void set_empty_text(const QString& text);

    QSize minimumSizeHint() const override;
    int heightForWidth(int w) const override;
    bool hasHeightForWidth() const override { return true; }

  protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    bool event(QEvent* e) override;

  private:
    struct Chip {
        ownership::Read read;
        QRect box;
        bool  is_heading = false;
        QString heading;
    };
    /// Lays chips out for a given width and returns the height used. Also the
    /// hit-test source, so what is drawn and what responds to the mouse cannot
    /// drift apart.
    int layout_chips(int width, QVector<Chip>* out) const;

    QVector<ownership::Read> reads_;
    QVector<Chip>            chips_;
    QString                  empty_text_;
};

} // namespace fincept::screens
