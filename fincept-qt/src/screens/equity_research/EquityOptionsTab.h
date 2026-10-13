// src/screens/equity_research/EquityOptionsTab.h
#pragma once
#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QWidget>

#include <vector>

class QLabel;
class QPushButton;
class QStackedWidget;
class QTableWidget;

namespace fincept::surface {
class Surface3DWidget;
class SurfaceLineWidget;
} // namespace fincept::surface

namespace fincept::screens {

/// Per-stock implied-volatility view: the IV surface (3D), the smile per
/// expiry, the ATM term structure, and the quotes behind them. Data is
/// Yahoo's delayed chain via the yfinance daemon's options_surface action —
/// out-of-the-money, two-sided quotes only, nothing interpolated.
///
/// A full chain is one request per expiry, so the tab loads when it is shown
/// (EquityResearchScreen forwards set_symbol only while it is the visible
/// tab) rather than for every ticker the reader flips past.
class EquityOptionsTab : public QWidget {
    Q_OBJECT
  public:
    explicit EquityOptionsTab(QWidget* parent = nullptr);

    /// Idempotent per ticker: re-activating the tab does not re-fetch,
    /// unless `force` (ER's "reload this symbol" path).
    void set_symbol(const QString& symbol, bool force = false);

    /// When Yahoo was actually queried for the chain on screen — not when it
    /// reached the UI, which a daemon cache hit satisfies instantly.
    QDateTime data_as_of() const { return data_as_of_; }

  signals:
    /// A chain landed (including an honest "no listed options"); drives
    /// ER's freshness chip.
    void loaded();

  private:
    struct Point {
        QString expiry;
        int dte = 0;
        double strike = 0;
        bool is_call = false;
        double iv = 0; // fraction, e.g. 0.23
        double bid = 0, ask = 0;
        int oi = -1;   // -1 = not reported
    };
    struct Atm {
        QString expiry;
        int dte = 0;
        double strike = 0;
        double iv = 0;
    };

    void build_ui();
    void fetch();
    void on_result(const QString& symbol, bool ok, const QJsonObject& obj, const QString& err);
    void show_message(const QString& text);
    void render();
    void render_surface();
    void render_smile();
    void render_term();
    void render_table();
    void set_view(int index);

    QString symbol_;
    QString loaded_symbol_;
    quint64 generation_ = 0;
    bool in_flight_ = false;

    double spot_ = 0;
    QDateTime data_as_of_;
    std::vector<Point> points_;
    std::vector<Atm> atm_;

    QLabel* status_label_ = nullptr;
    QLabel* source_label_ = nullptr;
    QPushButton* refresh_btn_ = nullptr;
    std::vector<QPushButton*> view_buttons_;
    QStackedWidget* stack_ = nullptr;
    QLabel* message_ = nullptr;
    fincept::surface::Surface3DWidget* surface_ = nullptr;
    fincept::surface::SurfaceLineWidget* smile_ = nullptr;
    fincept::surface::SurfaceLineWidget* term_ = nullptr;
    QTableWidget* table_ = nullptr;
    int view_ = 0;
};

} // namespace fincept::screens
