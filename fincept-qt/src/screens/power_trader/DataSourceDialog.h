// src/screens/power_trader/DataSourceDialog.h
#pragma once

#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

namespace fincept::power_trader {

/// Dialog shown when Power Trader opens without a Finnhub API key configured.
/// Explains the free data sources (Senate eFTS, House FDS) and offers to save
/// a Finnhub key for the fallback path.
///
/// Key is stored in QSettings("Fincept","Terminal")/"finnhub_api_key" —
/// NOT in an environment variable, so it persists across sessions.
class DataSourceDialog : public QDialog {
    Q_OBJECT
  public:
    explicit DataSourceDialog(QWidget* parent = nullptr);

    /// Read the stored key (empty string if not set).
    static QString stored_key();

    /// Save a key to persistent QSettings.
    static void save_key(const QString& key);

    /// True if a key is stored AND non-empty.
    static bool has_key();

  private slots:
    void on_save();
    void on_open_link();

  private:
    void build_ui();

    QLineEdit*   key_input_     = nullptr;
    QPushButton* save_btn_      = nullptr;
    QPushButton* skip_btn_      = nullptr;
    QLabel*      status_lbl_    = nullptr;
};

} // namespace fincept::power_trader
