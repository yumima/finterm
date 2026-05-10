// src/screens/power_trader/DataSourceDialog.h
#pragma once

#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

namespace fincept::power_trader {

/// First-run dialog that prompts the user for a (free) Congress.gov API key.
///
/// Why: Congress.gov's JSON API is the canonical source of the full live
/// member roster, district / bioguide IDs, photos, sponsored/cosponsored
/// legislation, voting records, hearings, and nominations. Without a key,
/// Power Trader falls back to senate.gov's public JSON (senators only) plus
/// a small KNOWN_MEMBERS lookup table — usable, but missing the richness.
///
/// Storage: QSettings("Fincept","Terminal") / "congress_gov_api_key" — per
/// user, never bundled as a default, never committed to the repo.
/// PowerTraderService reads the value on every refresh and injects it into
/// the Python payload sent to senate_disclosures_data.py.
class DataSourceDialog : public QDialog {
    Q_OBJECT
  public:
    explicit DataSourceDialog(QWidget* parent = nullptr);

    /// Read the stored Congress.gov key from QSettings. "" if not set.
    static QString stored_key();
    /// Persist the key to QSettings.
    static void    save_key(const QString& key);
    /// True iff a non-empty key has been persisted.
    static bool    has_key();

  private slots:
    void on_save();
    void on_open_signup();

  private:
    void build_ui();

    QLineEdit*   key_input_  = nullptr;
    QPushButton* save_btn_   = nullptr;
    QPushButton* skip_btn_   = nullptr;
    QLabel*      status_lbl_ = nullptr;
};

} // namespace fincept::power_trader
