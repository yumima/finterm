// src/screens/pre_ipo/PreIpoService.h
#pragma once
#include "screens/pre_ipo/PreIpoTypes.h"

#include <QObject>

namespace fincept::pre_ipo {

/// Singleton service owning the PRE-IPO company universe, Form D feed, and IPO pipeline.
///
/// Phase 1: loads hardcoded curated data for 20+ well-known private companies.
/// To wire real SEC EDGAR data: replace generate_static_data() with PythonWorker
/// calls to "form_d_filings" and "ipo_pipeline" daemon actions.
class PreIpoService : public QObject {
    Q_OBJECT
  public:
    static PreIpoService& instance();

    void load_data();

    QVector<PrivateCompany> companies()       const;
    PrivateCompany          company(const QString& id) const;
    QVector<FormDFiling>    recent_form_d()   const;
    QVector<S1Filing>       ipo_pipeline()    const;

    void toggle_watch(const QString& id);

    bool      is_loaded()     const { return summary_.loaded; }
    QDateTime last_updated()  const { return summary_.last_updated; }

  signals:
    void data_loaded(fincept::pre_ipo::PreIpoSummary summary);
    void error_occurred(QString error);

  private:
    explicit PreIpoService(QObject* parent = nullptr);
    Q_DISABLE_COPY(PreIpoService)

    void generate_static_data();

    PreIpoSummary summary_;
};

} // namespace fincept::pre_ipo
