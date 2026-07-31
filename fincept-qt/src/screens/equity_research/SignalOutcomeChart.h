// src/screens/equity_research/SignalOutcomeChart.h
#pragma once
#include "services/equity/EarningsSignal.h"
#include "storage/repositories/EarningsSignalRepository.h"

#include <QVector>
#include <QWidget>

namespace fincept::screens {

/// What the signal said before each print, against what the print did.
///
/// Sits directly above EarningsReactionChart and shares its column geometry —
/// same quarter slots, same order, same margins — so a quarter can be read
/// straight down through both charts. Changing the layout in one without the
/// other breaks the only reason they are stacked.
///
/// Two lines and the gap between them:
///   actual     the realised close-to-close move on the print
///   predicted  the estimate as it stood beforehand — DOTTED where it was
///              reconstructed after the fact, SOLID where it was genuinely
///              recorded before the print
///
/// The dotted stretch is the honest part. Consensus and revisions are a live
/// snapshot with no history, so a past quarter's estimate can only be rebuilt
/// from the backward-looking legs — about a third of the model's weight. Those
/// points run through the same formula with the missing legs counted absent,
/// which makes them visibly smaller than the solid ones will be. That is the
/// reconstruction knowing less, not a scaling artefact.
///
/// QPainter, like every other chart here: QOpenGLWidget spawns duplicate
/// xdg_toplevels under Mutter.
class SignalOutcomeChart : public QWidget {
    Q_OBJECT
  public:
    explicit SignalOutcomeChart(QWidget* parent = nullptr);

    /// `history` is newest-first, exactly as the reaction chart takes it, so
    /// the two agree on which quarters exist and in what order. `recorded` are
    /// the readings actually written down before a print; they replace the
    /// reconstruction wherever they match a quarter.
    void set_data(const services::equity::EarningsAnalysis& analysis,
                  const QVector<EarningsSignalRecord>& recorded,
                  const services::equity::EarningsVerdict& live);

    /// Hit rate and mean absolute error over the points that have both halves,
    /// for the panel's subtitle. Empty when nothing is comparable yet.
    QString summary() const { return summary_; }

  protected:
    void paintEvent(QPaintEvent* e) override;

  private:
    struct Column {
        qint64 timestamp = 0;
        std::optional<double> predicted;
        std::optional<double> actual;
        bool reconstructed = true;
        bool projected = false;   // the coming print: a prediction, no outcome
    };

    QVector<Column> cols_;
    QString summary_;
};

} // namespace fincept::screens
