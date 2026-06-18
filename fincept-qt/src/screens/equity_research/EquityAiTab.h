// src/screens/equity_research/EquityAiTab.h
#pragma once
#include "services/equity/EquityResearchModels.h"
#include "storage/repositories/AiPredictionRepository.h"

#include <QVector>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;
class QTextEdit;

namespace fincept::screens {

/// Pure-paint overlay of the AI's predicted path against the real price line.
/// Each prediction is drawn as a segment from (made-on, price-then) to
/// (resolve-date, predicted-target); green = the directional call was right,
/// red = wrong, amber = not yet resolved. No Q_OBJECT — paint only.
class PredictionChart : public QWidget {
  public:
    explicit PredictionChart(QWidget* parent = nullptr);
    void set_data(const QVector<services::equity::Candle>& candles,
                  const QVector<AiPrediction>& predictions);

  protected:
    void paintEvent(QPaintEvent*) override;

  private:
    QVector<services::equity::Candle> candles_;
    QVector<AiPrediction>             preds_;
};

/// "AI Forecast" tab: on-demand / daily AI analysis of a stock with an
/// immutable, self-scoring prediction track record (see AiPredictionRepository).
class EquityAiTab : public QWidget {
    Q_OBJECT
  public:
    explicit EquityAiTab(QWidget* parent = nullptr);

    void set_symbol(const QString& symbol);
    void set_info(const services::equity::StockInfo& info); // fundamentals (optional enrichment)

  private:
    void build_ui();
    void on_candles(const QVector<services::equity::Candle>& candles);
    void run_forecast(bool automatic);   // gather real data → LLM → record a prediction
    void resolve_due();                  // fill realized outcomes from real closes
    void refresh_track_record();         // reload the table + chart + hit-rate
    void maybe_auto_forecast();          // at most one auto-forecast per ticker per day

    QString build_prompt() const;        // assemble the real-data LLM input
    int     horizon_days() const;        // from the horizon selector

    QString current_symbol_;
    QString current_historical_key_;   // QueryStore key, unsubscribed on symbol switch
    services::equity::StockInfo info_;
    bool    info_ready_  = false;
    QVector<services::equity::Candle> candles_;
    bool    forecasting_ = false;
    quint64 forecast_epoch_ = 0;         // drop stale stream chunks on symbol switch

    // UI
    QComboBox*    horizon_sel_   = nullptr;
    QCheckBox*    auto_chk_      = nullptr;
    QPushButton*  run_btn_       = nullptr;
    QLabel*       status_lbl_    = nullptr;
    QLabel*       accuracy_lbl_  = nullptr;
    QTextEdit*    analysis_view_ = nullptr;
    QTableWidget* table_         = nullptr;
    PredictionChart* chart_      = nullptr;
};

} // namespace fincept::screens
