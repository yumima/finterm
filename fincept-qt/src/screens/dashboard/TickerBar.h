#pragma once
#include <QElapsedTimer>
#include <QHideEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QShowEvent>
#include <QTimer>
#include <QVector>
#include <QWidget>

namespace fincept::screens {

/// Scrolling ticker bar with live market data.
/// Right-click → "Edit Symbols" to customise the symbol list.
/// Symbol list is persisted via SettingsRepository under "ticker_bar_symbols".
class TickerBar : public QWidget {
    Q_OBJECT
  public:
    explicit TickerBar(QWidget* parent = nullptr);

    struct Entry {
        QString symbol;
        double  price  = 0;
        double  change = 0;
    };

    void set_data(const QVector<Entry>& entries);
    void pause()  { scroll_timer_.stop(); }
    void resume() {
        if (total_width_ > 0) {
            // Drop the stored delta so the first tick after resume doesn't
            // teleport the ticker based on however long it was paused for.
            scroll_clock_.invalidate();
            scroll_timer_.start();
        }
    }

    /// Returns the current symbol list (persisted user preference).
    QStringList symbols() const { return symbols_; }

  signals:
    /// Emitted when the user saves a new symbol list — caller should re-fetch.
    void symbols_changed(const QStringList& symbols);

  protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

  private:
    void load_symbols();
    void save_symbols();
    void show_edit_bar();
    void hide_edit_bar();
    void commit_edit();

    // ── Scrolling ──
    // Animation is time-based, not tick-based. Each tick advances the offset
    // by `kScrollPixelsPerSec * elapsed_since_last_tick` so a late timer
    // firing (event loop busy with a quote refresh, a heavy paint elsewhere,
    // etc.) advances further on the next tick to compensate — motion stays
    // smooth even when the tick rate is irregular. The previous frame-rate-
    // coupled `offset_ += 1.0` produced visible stutter under load.
    QVector<Entry> entries_;
    QTimer         scroll_timer_;
    QElapsedTimer  scroll_clock_;            ///< last_tick wall-clock for delta
    double         offset_      = 0;
    int            total_width_ = 0;
    static constexpr double kScrollPixelsPerSec = 20.0;  ///< matches prior visual speed

    // ── Symbol list ──
    QStringList symbols_;

    // ── Inline edit overlay ──
    QWidget*     edit_bar_   = nullptr;
    QLineEdit*   edit_input_ = nullptr;
    QPushButton* edit_ok_    = nullptr;
    QPushButton* edit_cancel_ = nullptr;
};

} // namespace fincept::screens
