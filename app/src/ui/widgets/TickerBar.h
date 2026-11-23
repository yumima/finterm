#pragma once
#include <QElapsedTimer>
#include <QHideEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QShowEvent>
#include <QTimer>
#include <QVector>
#include <QWidget>

namespace finterm::ui {

/// The scrolling market tape. It lives in the banner row at the top of every
/// main window (PushpinBar), which feeds it quotes.
///
/// - Click an entry: its chart. Drag one: send the symbol to any panel.
/// - Right-click an entry: chart, pin / unpin, copy, broadcast to a group;
///   anywhere: "Edit default symbols…".
/// The default list is persisted via SettingsRepository under
/// "ticker_bar_symbols"; pinned symbols (PushpinService) are drawn in the
/// accent colour.
class TickerBar : public QWidget {
    Q_OBJECT
  public:
    explicit TickerBar(QWidget* parent = nullptr);
    ~TickerBar() override;

    struct Entry {
        QString symbol;
        double price = 0;  ///< NaN when unknown → "—"
        double change = 0; ///< percent change; NaN when unknown → "—"
        bool pinned = false;
    };

    void set_data(const QVector<Entry>& entries);
    void pause() { scroll_timer_.stop(); }
    void resume() {
        if (total_width_ > 0) {
            // Drop the stored delta so the first tick after resume doesn't
            // teleport the ticker based on however long it was paused for.
            scroll_clock_.invalidate();
            scroll_timer_.start();
        }
    }

    /// The default symbol list (persisted user preference).
    QStringList symbols() const { return symbols_; }
    /// A drag started on this tape is in progress (a drop back on the banner
    /// is not a request to pin).
    bool dragging() const { return dragging_; }

  signals:
    /// Emitted when the user saves a new default list — the feeder re-subscribes.
    void symbols_changed(const QStringList& symbols);

  protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

  private:
    void load_symbols();
    void save_symbols();
    void show_edit_bar();
    void hide_edit_bar();
    void commit_edit();
    /// The entry painted under x (widget coordinates); -1 when none.
    int entry_at(double x) const;

    // ── Scrolling ──
    // Animation is time-based, not tick-based. Each tick advances the offset
    // by `kScrollPixelsPerSec * elapsed_since_last_tick` so a late timer
    // firing (event loop busy with a quote refresh, a heavy paint elsewhere,
    // etc.) advances further on the next tick to compensate — motion stays
    // smooth even when the tick rate is irregular.
    QVector<Entry> entries_;
    QTimer scroll_timer_;
    QElapsedTimer scroll_clock_; ///< last_tick wall-clock for delta
    double offset_ = 0;
    int total_width_ = 0;
    /// Width of each entry as painted (symbol, price, change and the trailing
    /// spacing), in entries_ order — the hit test walks the same layout.
    QVector<int> entry_widths_;
    static constexpr double kScrollPixelsPerSec = 20.0; ///< matches prior visual speed

    // ── Press: a click opens the chart, a drag sends the symbol ──
    // The SYMBOL pressed, not its index: entries_ is rebuilt on every quote
    // batch, and a first quote or a pin shifts the indices under the press.
    QString pressed_symbol_;
    QPoint press_pos_;
    bool dragging_ = false;

    // ── Symbol list ──
    QStringList symbols_;

    // ── Inline edit overlay ──
    QWidget* edit_bar_ = nullptr;
    QLineEdit* edit_input_ = nullptr;
    QPushButton* edit_ok_ = nullptr;
    QPushButton* edit_cancel_ = nullptr;
};

} // namespace finterm::ui
