#pragma once
#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;

namespace fincept::screens {

/// "Which tracked managers hold this, and what did they do."
///
/// The stock-perspective view of the 13F index, embedded in two places: the
/// OWNERSHIP screen, where it sits under the security's own register, and
/// Equity Research, where it answers the ownership question inside a per-ticker
/// workflow. One widget rather than two renderings of the same data.
///
/// The number this exists to show is the position as a share of the MANAGER'S
/// book. A holder table gives "BlackRock owns 8% of AAPL", which mostly says
/// AAPL is in the index; this gives "AAPL is 22% of Berkshire's book", which
/// says somebody with discretion decided something. The second cannot be read
/// off a holder table at all — it needs the manager's whole 13F as a
/// denominator.
///
/// It is expensive: every tracked manager is several EDGAR round-trips, so the
/// fetch is opt-in behind a button rather than firing on every symbol change.
class SmartMoneyPanel : public QWidget {
    Q_OBJECT
  public:
    explicit SmartMoneyPanel(QWidget* parent = nullptr);

    /// Point the panel at a ticker. Renders whatever is already cached and
    /// leaves the fetch to the user unless @p auto_fetch is set.
    void set_symbol(const QString& symbol, bool auto_fetch = false);

  private:
    /// Two legitimate rankings of the same rows. Bloomberg's HDS defaults to
    /// position size; a weight answers a different question. Exposed as a
    /// control rather than baked in, because the reader needs to know which one
    /// they are looking at.
    enum class Sort { Weight, Value };

    void render();

    QString          symbol_;
    QComboBox*       sort_ = nullptr;
    QLabel*          status_ = nullptr;
    QLabel*          caveat_ = nullptr;
    QLabel*          flow_ = nullptr;
    QPushButton*     build_btn_ = nullptr;
    class RankedBarChart* chart_ = nullptr;
};

} // namespace fincept::screens
