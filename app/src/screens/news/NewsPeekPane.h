#pragma once
// NewsPeekPane.h — the slot right of the News drawer where whatever a WHAT'S
// ON row opens (Equity Research for a company, IPO Watch for a deal, the
// weekly brief) appears beside the list it came from, instead of in a dock
// panel elsewhere or in place of the News screen.
//
// Each hosted view is built once on first use and kept (hidden views stop
// their own timers in hideEvent), so re-opening is instant. The header names
// what is shown, offers the full screen (↗) and closes the pane (×).

#include <QHash>
#include <QWidget>

#include <functional>

class QLabel;
class QStackedWidget;

namespace finterm::screens {

class NewsPeekPane : public QWidget {
    Q_OBJECT
  public:
    explicit NewsPeekPane(QWidget* parent = nullptr);

    /// Show the view kept under `key` — built by `make` the first time —
    /// titled `title`. `pop_out` opens the same thing as a full screen.
    /// Returns the view so the caller can point it at the row's subject.
    QWidget* show_view(const QString& key, const QString& title, const std::function<QWidget*()>& make,
                       std::function<void()> pop_out);

    /// Replace what FULL SCREEN does (when it depends on the view's state).
    void set_pop_out(std::function<void()> pop_out) { pop_out_ = std::move(pop_out); }

  signals:
    void close_requested();

  private:
    void apply_styles();

    QLabel* title_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QHash<QString, QWidget*> views_;
    QHash<QString, QWidget*> holders_; ///< the scroll area around each view
    std::function<void()> pop_out_;
};

} // namespace finterm::screens
