#pragma once
// PriceSourceSection.h — Settings › Data Sources › "Price source".
//
// Which vendor answers daily/weekly/monthly bars and stock quotes before
// Yahoo (scripts/price_sources.py). The choice is stored in the main settings
// store (market_data/price_source) and handed to every Python child as
// FINTERM_PRICE_SOURCE; the vendor's key comes from the keyring like any other
// credential. Yahoo stays the fallback for everything a vendor can't answer.

#include <QSet>
#include <QWidget>

class QComboBox;
class QLabel;

namespace finterm::screens {

class PriceSourceSection : public QWidget {
    Q_OBJECT
  public:
    explicit PriceSourceSection(QWidget* parent = nullptr);

    /// Re-read which vendor keys are set (call when the page is shown).
    void reload();

  protected:
    void showEvent(QShowEvent* e) override;

  private:
    void update_detail();

    QSet<QString> missing_keys_; // vendors whose key isn't set, as of reload()

    QComboBox* combo_ = nullptr;
    QLabel* detail_ = nullptr;
};

} // namespace finterm::screens
