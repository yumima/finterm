// src/services/pre_ipo/PreIpoService.cpp
#include "services/pre_ipo/PreIpoService.h"

#include <QDateTime>

namespace fincept::services {

using namespace fincept::pre_ipo;

// ── Singleton ────────────────────────────────────────────────────────────────

PreIpoService& PreIpoService::instance() {
    static PreIpoService inst;
    return inst;
}

PreIpoService::PreIpoService(QObject* parent) : QObject(parent) {}

// ── Public API ────────────────────────────────────────────────────────────────

void PreIpoService::load_data() {
    if (loaded_)
        return;
    build_mock_data();
    loaded_ = true;

    PreIpoSummary summary;
    summary.companies     = companies_;
    summary.recent_form_d = form_d_;
    summary.ipo_pipeline  = pipeline_;
    summary.last_updated  = QDateTime::currentDateTime();
    summary.loaded        = true;

    emit data_loaded(summary);
}

QVector<PrivateCompany> PreIpoService::companies() const {
    return companies_;
}

PrivateCompany PreIpoService::company(const QString& id) const {
    for (const auto& c : companies_) {
        if (c.id == id)
            return c;
    }
    return {};
}

void PreIpoService::toggle_watch(const QString& id) {
    for (auto& c : companies_) {
        if (c.id == id) {
            c.watched = !c.watched;
            emit company_updated(id);
            return;
        }
    }
}

QVector<FormDFiling> PreIpoService::recent_form_d() const {
    return form_d_;
}

QVector<S1Filing> PreIpoService::ipo_pipeline() const {
    return pipeline_;
}

// ── Mock data ─────────────────────────────────────────────────────────────────

void PreIpoService::build_mock_data() {

    // ── Helper lambdas ────────────────────────────────────────────────────────

    auto make_round = [](const QString& name, int year, int month,
                         double amount_m, double valuation_b,
                         const QStringList& investors) -> FundingRound {
        FundingRound r;
        r.round_name         = name;
        r.date               = QDate(year, month, 1);
        r.amount_usd         = amount_m;
        r.implied_valuation  = valuation_b;
        r.lead_investors     = investors;
        return r;
    };

    // ── Stripe ────────────────────────────────────────────────────────────────
    {
        PrivateCompany c;
        c.id                  = "stripe";
        c.name                = "Stripe";
        c.sector              = "Fintech";
        c.sub_sector          = "Payments";
        c.hq_city             = "San Francisco";
        c.hq_country          = "USA";
        c.founded             = QDate(2010, 1, 1);
        c.ipo_status          = IpoStatus::Rumored;
        c.last_valuation_usd  = 70.0;
        c.last_round_date     = QDate(2023, 3, 1);
        c.last_round_name     = "Series I";
        c.revenue_est_usd     = 14000;
        c.employee_count      = 8000;
        c.key_investors       = {"Sequoia", "Andreessen Horowitz", "General Catalyst", "Thrive Capital"};
        c.ipo_expected_window = "2025–2026";
        c.public_comps        = {"PYPL", "SQ", "ADYEY"};
        c.tags                = {"fintech", "payments", "infrastructure"};
        c.description         = "Stripe is a technology company that builds economic infrastructure "
                                "for the internet, offering APIs and software tools to help businesses "
                                "accept payments and manage their finances online.";
        c.rounds = {
            make_round("Series A",  2011, 3,    2.0,   0.1,  {"Sequoia"}),
            make_round("Series B",  2012, 7,   18.0,   0.5,  {"Sequoia", "General Catalyst"}),
            make_round("Series C",  2014, 1,   80.0,   1.75, {"Founders Fund", "Khosla"}),
            make_round("Series D",  2016, 11, 150.0,   9.2,  {"CapitalG", "General Catalyst"}),
            make_round("Series G",  2019, 9,  250.0,  35.0,  {"Sequoia", "Andreessen Horowitz"}),
            make_round("Series H",  2021, 3,  600.0,  95.0,  {"Allianz", "Fidelity", "Sequoia"}),
            make_round("Series I",  2023, 3,  694.0,  50.0,  {"Andreessen Horowitz", "Baillie Gifford"}),
        };
        companies_.append(c);
    }

    // ── SpaceX ────────────────────────────────────────────────────────────────
    {
        PrivateCompany c;
        c.id                  = "spacex";
        c.name                = "SpaceX";
        c.sector              = "Aerospace";
        c.sub_sector          = "Launch & Satellite";
        c.hq_city             = "Hawthorne";
        c.hq_country          = "USA";
        c.founded             = QDate(2002, 3, 14);
        c.ipo_status          = IpoStatus::Rumored;
        c.last_valuation_usd  = 350.0;
        c.last_round_date     = QDate(2024, 6, 1);
        c.last_round_name     = "Series (Tender)";
        c.revenue_est_usd     = 9000;
        c.employee_count      = 13000;
        c.key_investors       = {"Elon Musk", "Founders Fund", "Google", "Fidelity", "Baillie Gifford"};
        c.ipo_expected_window = "Starlink IPO possible 2026";
        c.public_comps        = {"BA", "RTX", "MAXR"};
        c.tags                = {"aerospace", "defense", "launch", "satellite"};
        c.description         = "SpaceX designs, manufactures, and launches advanced rockets and spacecraft "
                                "with the mission to reduce space transportation costs and enable colonization "
                                "of Mars. Operates the Starlink broadband constellation.";
        c.rounds = {
            make_round("Series A",  2002, 3,   100.0,   0.3,  {"Elon Musk"}),
            make_round("Series F",  2015, 1,  1000.0,  12.0,  {"Fidelity", "Google"}),
            make_round("Tender",    2021, 4,   750.0,  74.0,  {"Founders Fund"}),
            make_round("Tender",    2023, 6,   750.0, 150.0,  {"Various Institutions"}),
            make_round("Tender",    2024, 6,   500.0, 210.0,  {"Andreessen Horowitz", "Valor Equity"}),
        };
        companies_.append(c);
    }

    // ── Databricks ───────────────────────────────────────────────────────────
    {
        PrivateCompany c;
        c.id                  = "databricks";
        c.name                = "Databricks";
        c.sector              = "AI/Data";
        c.sub_sector          = "Data & MLOps";
        c.hq_city             = "San Francisco";
        c.hq_country          = "USA";
        c.founded             = QDate(2013, 1, 1);
        c.ipo_status          = IpoStatus::Rumored;
        c.last_valuation_usd  = 62.0;
        c.last_round_date     = QDate(2024, 3, 1);
        c.last_round_name     = "Series J";
        c.revenue_est_usd     = 1600;
        c.employee_count      = 6000;
        c.key_investors       = {"Andreessen Horowitz", "T. Rowe Price", "Franklin Templeton", "NVIDIA"};
        c.ipo_expected_window = "2025";
        c.public_comps        = {"SNOW", "MDB", "DDOG"};
        c.tags                = {"ai", "data", "cloud", "enterprise"};
        c.description         = "Databricks provides a unified data analytics platform combining data "
                                "engineering, machine learning, and collaborative notebooks — built on "
                                "Apache Spark and Delta Lake.";
        c.rounds = {
            make_round("Series A",  2013, 6,   14.0,  0.07, {"Andreessen Horowitz"}),
            make_round("Series D",  2017, 8,  140.0,  1.4,  {"Andreessen Horowitz", "NEA"}),
            make_round("Series G",  2021, 2, 1000.0, 28.0,  {"Franklin Templeton", "T. Rowe"}),
            make_round("Series H",  2022, 9, 1580.0, 38.0,  {"T. Rowe Price"}),
            make_round("Series J",  2024, 3,  500.0, 62.0,  {"NVIDIA", "Andreessen Horowitz"}),
        };
        companies_.append(c);
    }

    // ── OpenAI ────────────────────────────────────────────────────────────────
    {
        PrivateCompany c;
        c.id                  = "openai";
        c.name                = "OpenAI";
        c.sector              = "AI";
        c.sub_sector          = "Foundation Models";
        c.hq_city             = "San Francisco";
        c.hq_country          = "USA";
        c.founded             = QDate(2015, 12, 11);
        c.ipo_status          = IpoStatus::Unknown;
        c.last_valuation_usd  = 300.0;
        c.last_round_date     = QDate(2024, 10, 1);
        c.last_round_name     = "Series (Tender)";
        c.revenue_est_usd     = 3400;
        c.employee_count      = 3000;
        c.key_investors       = {"Microsoft", "Thrive Capital", "Tiger Global", "Fidelity"};
        c.ipo_expected_window = "No announced plans";
        c.public_comps        = {"MSFT", "GOOGL", "AMZN"};
        c.tags                = {"ai", "llm", "foundation-model", "enterprise"};
        c.description         = "OpenAI is an AI research and deployment company best known for the GPT "
                                "series and ChatGPT. Operates a capped-profit structure with Microsoft "
                                "as the primary strategic investor.";
        c.rounds = {
            make_round("Seed",          2015, 12, 1000.0,  1.0,  {"Y Combinator", "Elon Musk"}),
            make_round("Series A",      2019, 3,  100.0,   1.0,  {"Microsoft"}),
            make_round("Microsoft",     2021, 1, 1000.0,  14.0,  {"Microsoft"}),
            make_round("Microsoft",     2023, 1,10000.0,  29.0,  {"Microsoft"}),
            make_round("Series (Tender)", 2024,10, 6600.0, 157.0, {"Thrive Capital", "Microsoft", "Nvidia"}),
        };
        companies_.append(c);
    }

    // ── Anthropic ────────────────────────────────────────────────────────────
    {
        PrivateCompany c;
        c.id                  = "anthropic";
        c.name                = "Anthropic";
        c.sector              = "AI";
        c.sub_sector          = "Foundation Models";
        c.hq_city             = "San Francisco";
        c.hq_country          = "USA";
        c.founded             = QDate(2021, 4, 1);
        c.ipo_status          = IpoStatus::Unknown;
        c.last_valuation_usd  = 61.5;
        c.last_round_date     = QDate(2025, 3, 1);
        c.last_round_name     = "Series E";
        c.revenue_est_usd     = 1000;
        c.employee_count      = 2000;
        c.key_investors       = {"Amazon", "Google", "Spark Capital", "Salesforce Ventures"};
        c.ipo_expected_window = "No announced plans";
        c.public_comps        = {"AMZN", "GOOGL", "MSFT"};
        c.tags                = {"ai", "llm", "safety", "foundation-model"};
        c.description         = "Anthropic is an AI safety company building reliable, interpretable, and "
                                "steerable AI systems. Known for the Claude family of models and pioneering "
                                "Constitutional AI alignment research.";
        c.rounds = {
            make_round("Series A",  2021, 5,  124.0,   1.0,  {"Spark Capital", "Google"}),
            make_round("Series B",  2022, 4,  580.0,   4.1,  {"Spark Capital", "Google"}),
            make_round("Series C",  2023, 5, 1250.0,   5.0,  {"Google", "Spark Capital"}),
            make_round("Amazon",    2023, 9, 4000.0,  20.0,  {"Amazon"}),
            make_round("Series E",  2025, 3, 2500.0,  61.5,  {"Google", "Amazon", "Spark Capital"}),
        };
        companies_.append(c);
    }

    // ── Klarna ────────────────────────────────────────────────────────────────
    {
        PrivateCompany c;
        c.id                  = "klarna";
        c.name                = "Klarna";
        c.sector              = "Fintech";
        c.sub_sector          = "Buy Now Pay Later";
        c.hq_city             = "Stockholm";
        c.hq_country          = "Sweden";
        c.founded             = QDate(2005, 1, 1);
        c.ipo_status          = IpoStatus::Filed;
        c.last_valuation_usd  = 14.6;
        c.last_round_date     = QDate(2022, 7, 1);
        c.last_round_name     = "Series (Down Round)";
        c.revenue_est_usd     = 2500;
        c.employee_count      = 5000;
        c.key_investors       = {"Sequoia", "SoftBank Vision Fund", "Silver Lake", "Atomico"};
        c.s1_filed_date       = QDate(2025, 3, 1);
        c.ipo_expected_window = "H1 2025";
        c.public_comps        = {"AFRM", "PYPL", "SQ"};
        c.tags                = {"fintech", "bnpl", "payments", "europe"};
        c.description         = "Klarna is a Swedish fintech and global payments network providing "
                                "buy now, pay later services. Filed its S-1 for a US IPO in 2025 "
                                "after raising at a peak valuation of $45.6B in 2021.";
        c.rounds = {
            make_round("Series A",      2007, 6,   2.0,   0.1,  {"Sequoia"}),
            make_round("Series D",      2019, 8,  460.0,  5.5,  {"Dragoneer", "Commonwealth Bank"}),
            make_round("Series E",      2021, 6, 1000.0, 45.6,  {"SoftBank Vision Fund"}),
            make_round("Down Round",    2022, 7,  800.0,  6.7,  {"SoftBank", "Silver Lake"}),
        };
        companies_.append(c);
    }

    // ── eToro ─────────────────────────────────────────────────────────────────
    {
        PrivateCompany c;
        c.id                  = "etoro";
        c.name                = "eToro";
        c.sector              = "Fintech";
        c.sub_sector          = "Social Trading";
        c.hq_city             = "Tel Aviv";
        c.hq_country          = "Israel";
        c.founded             = QDate(2007, 1, 1);
        c.ipo_status          = IpoStatus::Filed;
        c.last_valuation_usd  = 3.5;
        c.last_round_date     = QDate(2021, 3, 1);
        c.last_round_name     = "Series F";
        c.revenue_est_usd     = 640;
        c.employee_count      = 1800;
        c.key_investors       = {"SoftBank Vision Fund", "ION", "Spark Capital"};
        c.s1_filed_date       = QDate(2024, 12, 1);
        c.ipo_expected_window = "H1 2025";
        c.public_comps        = {"HOOD", "IBKR", "SCHW"};
        c.tags                = {"fintech", "trading", "social", "crypto"};
        c.description         = "eToro is a social trading and investment platform enabling users to "
                                "trade stocks, cryptocurrencies, and ETFs. Known for its CopyTrader "
                                "feature and community-driven investment tools.";
        c.rounds = {
            make_round("Series D",  2014, 3,   27.0,  0.2,  {"BRM Group"}),
            make_round("Series E",  2018, 3,  100.0,  0.8,  {"China Minsheng"}),
            make_round("Series F",  2021, 3,  250.0,  3.5,  {"SoftBank Vision Fund"}),
        };
        companies_.append(c);
    }

    // ── Chime ─────────────────────────────────────────────────────────────────
    {
        PrivateCompany c;
        c.id                  = "chime";
        c.name                = "Chime";
        c.sector              = "Fintech";
        c.sub_sector          = "Neobank";
        c.hq_city             = "San Francisco";
        c.hq_country          = "USA";
        c.founded             = QDate(2013, 1, 1);
        c.ipo_status          = IpoStatus::Rumored;
        c.last_valuation_usd  = 25.0;
        c.last_round_date     = QDate(2021, 8, 1);
        c.last_round_name     = "Series G";
        c.revenue_est_usd     = 1500;
        c.employee_count      = 1500;
        c.key_investors       = {"Sequoia", "SoftBank Vision Fund", "Coatue", "ICONIQ"};
        c.ipo_expected_window = "2025–2026";
        c.public_comps        = {"SOFI", "NU", "AFRM"};
        c.tags                = {"fintech", "neobank", "banking"};
        c.description         = "Chime is a financial technology company offering fee-free mobile "
                                "banking services including spending accounts, savings, and early "
                                "direct deposit. One of the largest US neobanks by account holders.";
        c.rounds = {
            make_round("Series D",  2019, 3,  200.0,  1.5,  {"DST Global"}),
            make_round("Series F",  2020, 9,  485.0, 14.5,  {"Coatue", "Tiger Global"}),
            make_round("Series G",  2021, 8,  750.0, 25.0,  {"Sequoia", "SoftBank Vision Fund"}),
        };
        companies_.append(c);
    }

    // ── Discord ───────────────────────────────────────────────────────────────
    {
        PrivateCompany c;
        c.id                  = "discord";
        c.name                = "Discord";
        c.sector              = "Social";
        c.sub_sector          = "Communication";
        c.hq_city             = "San Francisco";
        c.hq_country          = "USA";
        c.founded             = QDate(2015, 1, 1);
        c.ipo_status          = IpoStatus::Rumored;
        c.last_valuation_usd  = 15.0;
        c.last_round_date     = QDate(2021, 9, 1);
        c.last_round_name     = "Series H";
        c.revenue_est_usd     = 600;
        c.employee_count      = 600;
        c.key_investors       = {"Dragoneer", "Greenoaks", "Index Ventures", "IVP"};
        c.ipo_expected_window = "2026";
        c.public_comps        = {"RBLX", "MTCH", "ZM"};
        c.tags                = {"social", "gaming", "communication", "community"};
        c.description         = "Discord is a communication platform popular with gamers, developers, "
                                "and online communities. Offers voice, video, and text communication "
                                "through topic-based servers.";
        c.rounds = {
            make_round("Series A",  2015, 12,  20.0,  0.1,  {"Benchmark"}),
            make_round("Series D",  2019, 12, 150.0,  2.0,  {"Greenoaks"}),
            make_round("Series G",  2020, 12, 100.0,  7.0,  {"Index Ventures", "Greenoaks"}),
            make_round("Series H",  2021, 9,  500.0, 15.0,  {"Dragoneer", "Greenoaks"}),
        };
        companies_.append(c);
    }

    // ── Plaid ─────────────────────────────────────────────────────────────────
    {
        PrivateCompany c;
        c.id                  = "plaid";
        c.name                = "Plaid";
        c.sector              = "Fintech";
        c.sub_sector          = "Banking Infrastructure";
        c.hq_city             = "San Francisco";
        c.hq_country          = "USA";
        c.founded             = QDate(2013, 1, 1);
        c.ipo_status          = IpoStatus::Rumored;
        c.last_valuation_usd  = 13.4;
        c.last_round_date     = QDate(2021, 4, 1);
        c.last_round_name     = "Series D";
        c.revenue_est_usd     = 250;
        c.employee_count      = 900;
        c.key_investors       = {"Andreessen Horowitz", "New Enterprise Associates", "Spark Capital"};
        c.ipo_expected_window = "2025–2026";
        c.public_comps        = {"PYPL", "FIS", "FISV"};
        c.tags                = {"fintech", "banking", "infrastructure", "api"};
        c.description         = "Plaid is a financial technology company providing data transfer "
                                "infrastructure that enables applications to connect with users' "
                                "bank accounts. Powers thousands of fintech apps.";
        c.rounds = {
            make_round("Series A",  2013, 9,  2.8,  0.07, {"New Enterprise Associates"}),
            make_round("Series B",  2016, 6,  44.0, 0.25, {"Goldman Sachs"}),
            make_round("Series C",  2018, 12, 250.0, 2.65, {"Mary Meeker (KPCB)"}),
            make_round("Series D",  2021, 4,  425.0, 13.4, {"Altimeter", "Silver Lake"}),
        };
        companies_.append(c);
    }

    // ── Revolut ───────────────────────────────────────────────────────────────
    {
        PrivateCompany c;
        c.id                  = "revolut";
        c.name                = "Revolut";
        c.sector              = "Fintech";
        c.sub_sector          = "Neobank";
        c.hq_city             = "London";
        c.hq_country          = "UK";
        c.founded             = QDate(2015, 7, 1);
        c.ipo_status          = IpoStatus::Rumored;
        c.last_valuation_usd  = 45.0;
        c.last_round_date     = QDate(2024, 8, 1);
        c.last_round_name     = "Secondary (Employee)";
        c.revenue_est_usd     = 2200;
        c.employee_count      = 8000;
        c.key_investors       = {"SoftBank Vision Fund", "Tiger Global", "TCV"};
        c.ipo_expected_window = "2026";
        c.public_comps        = {"NU", "SOFI", "WBD"};
        c.tags                = {"fintech", "neobank", "europe", "banking"};
        c.description         = "Revolut is a British neobank offering banking, currency exchange, "
                                "cryptocurrency, stock trading, and insurance services. "
                                "One of the most valuable European fintechs.";
        c.rounds = {
            make_round("Series A",  2016, 7,  10.0,  0.05, {"Balderton Capital"}),
            make_round("Series C",  2018, 4,  250.0,  1.7,  {"DST Global"}),
            make_round("Series D",  2020, 2,  500.0,  5.5,  {"TCV"}),
            make_round("Series E",  2021, 7, 800.0, 33.0,  {"SoftBank Vision Fund", "Tiger Global"}),
        };
        companies_.append(c);
    }

    // ── Anduril ───────────────────────────────────────────────────────────────
    {
        PrivateCompany c;
        c.id                  = "anduril";
        c.name                = "Anduril Industries";
        c.sector              = "Defense Tech";
        c.sub_sector          = "Autonomous Weapons & Sensors";
        c.hq_city             = "Costa Mesa";
        c.hq_country          = "USA";
        c.founded             = QDate(2017, 1, 1);
        c.ipo_status          = IpoStatus::Unknown;
        c.last_valuation_usd  = 28.0;
        c.last_round_date     = QDate(2024, 8, 1);
        c.last_round_name     = "Series F";
        c.revenue_est_usd     = 500;
        c.employee_count      = 3000;
        c.key_investors       = {"Andreessen Horowitz", "8VC", "Founders Fund", "Lux Capital"};
        c.ipo_expected_window = "No announced plans";
        c.public_comps        = {"LMT", "RTX", "NOC"};
        c.tags                = {"defense", "autonomous", "ai", "national-security"};
        c.description         = "Anduril Industries builds advanced defense technology including "
                                "autonomous surveillance systems, drones, and defense software. "
                                "Founded by Palmer Luckey (creator of Oculus).";
        c.rounds = {
            make_round("Series A",  2018, 8,  14.0,  0.15, {"8VC", "Founders Fund"}),
            make_round("Series C",  2020, 9,  200.0, 1.9,  {"Andreessen Horowitz"}),
            make_round("Series D",  2021, 12, 450.0, 4.6,  {"Andreessen Horowitz", "8VC"}),
            make_round("Series E",  2022, 12, 1500.0, 8.5, {"Andreessen Horowitz", "General Atlantic"}),
            make_round("Series F",  2024, 8, 1500.0, 14.0, {"Andreessen Horowitz", "Founders Fund"}),
        };
        companies_.append(c);
    }

    // ── Scale AI ──────────────────────────────────────────────────────────────
    {
        PrivateCompany c;
        c.id                  = "scale_ai";
        c.name                = "Scale AI";
        c.sector              = "AI";
        c.sub_sector          = "Data Labeling & AI Infrastructure";
        c.hq_city             = "San Francisco";
        c.hq_country          = "USA";
        c.founded             = QDate(2016, 1, 1);
        c.ipo_status          = IpoStatus::Rumored;
        c.last_valuation_usd  = 13.8;
        c.last_round_date     = QDate(2024, 5, 1);
        c.last_round_name     = "Series F";
        c.revenue_est_usd     = 700;
        c.employee_count      = 1200;
        c.key_investors       = {"Accel", "Tiger Global", "Coatue", "Meta", "Amazon", "Google"};
        c.ipo_expected_window = "2025–2026";
        c.public_comps        = {"GOOGL", "AMZN", "MSFT"};
        c.tags                = {"ai", "data", "labeling", "defense"};
        c.description         = "Scale AI provides high-quality training data for AI applications, "
                                "including computer vision, NLP, and autonomous vehicles. "
                                "Also develops AI solutions for US government and defense.";
        c.rounds = {
            make_round("Series A",  2018, 8,   18.0,  0.1,  {"Accel"}),
            make_round("Series B",  2019, 8,  100.0,  1.0,  {"Tiger Global"}),
            make_round("Series C",  2020, 8,  155.0,  3.5,  {"Tiger Global", "Coatue"}),
            make_round("Series E",  2021, 4, 325.0,   7.3,  {"Tiger Global", "Coatue"}),
            make_round("Series F",  2024, 5,1000.0,  13.8,  {"Amazon", "Google", "Meta"}),
        };
        companies_.append(c);
    }

    // ── xAI ───────────────────────────────────────────────────────────────────
    {
        PrivateCompany c;
        c.id                  = "xai";
        c.name                = "xAI";
        c.sector              = "AI";
        c.sub_sector          = "Foundation Models";
        c.hq_city             = "Austin";
        c.hq_country          = "USA";
        c.founded             = QDate(2023, 7, 12);
        c.ipo_status          = IpoStatus::Unknown;
        c.last_valuation_usd  = 50.0;
        c.last_round_date     = QDate(2025, 3, 1);
        c.last_round_name     = "Series C";
        c.revenue_est_usd     = 200;
        c.employee_count      = 600;
        c.key_investors       = {"Elon Musk", "Andreessen Horowitz", "Sequoia", "Fidelity"};
        c.ipo_expected_window = "No announced plans";
        c.public_comps        = {"GOOGL", "MSFT", "META"};
        c.tags                = {"ai", "llm", "foundation-model"};
        c.description         = "xAI is an artificial intelligence company founded by Elon Musk "
                                "with the mission to understand the true nature of the universe. "
                                "Builds the Grok LLM integrated with the X (Twitter) platform.";
        c.rounds = {
            make_round("Series B",  2024, 5, 6000.0, 18.0, {"Andreessen Horowitz", "Sequoia"}),
            make_round("Series C",  2025, 3, 6000.0, 50.0, {"Various Institutions"}),
        };
        companies_.append(c);
    }

    // ── Waymo ─────────────────────────────────────────────────────────────────
    {
        PrivateCompany c;
        c.id                  = "waymo";
        c.name                = "Waymo";
        c.sector              = "Autonomous Vehicles";
        c.sub_sector          = "Robotaxi";
        c.hq_city             = "Mountain View";
        c.hq_country          = "USA";
        c.founded             = QDate(2009, 1, 1);
        c.ipo_status          = IpoStatus::Unknown;
        c.last_valuation_usd  = 45.0;
        c.last_round_date     = QDate(2024, 10, 1);
        c.last_round_name     = "Series B";
        c.revenue_est_usd     = 300;
        c.employee_count      = 2500;
        c.key_investors       = {"Alphabet", "Andreessen Horowitz", "Temasek", "Silver Lake"};
        c.ipo_expected_window = "No announced plans";
        c.public_comps        = {"GOOGL", "TSLA", "GM", "UBER"};
        c.tags                = {"autonomous", "robotics", "transportation", "ai"};
        c.description         = "Waymo is an autonomous driving technology company and subsidiary "
                                "of Alphabet (Google). Operates a commercial robotaxi service "
                                "(Waymo One) in San Francisco, Phoenix, and Los Angeles.";
        c.rounds = {
            make_round("Series A",  2020, 3, 2250.0, 30.0, {"Andreessen Horowitz", "Silver Lake"}),
            make_round("Series B",  2021, 6, 2500.0, 30.0, {"Temasek", "Alphabet"}),
            make_round("Series C",  2024,10, 5600.0, 45.0, {"Alphabet", "Andreessen Horowitz"}),
        };
        companies_.append(c);
    }

    // ── Canva ─────────────────────────────────────────────────────────────────
    {
        PrivateCompany c;
        c.id                  = "canva";
        c.name                = "Canva";
        c.sector              = "Design Tech";
        c.sub_sector          = "Visual Communication";
        c.hq_city             = "Sydney";
        c.hq_country          = "Australia";
        c.founded             = QDate(2013, 1, 1);
        c.ipo_status          = IpoStatus::Rumored;
        c.last_valuation_usd  = 26.0;
        c.last_round_date     = QDate(2021, 9, 1);
        c.last_round_name     = "Series E";
        c.revenue_est_usd     = 1700;
        c.employee_count      = 4000;
        c.key_investors       = {"Sequoia", "Blackbird", "T. Rowe Price", "Franklin Templeton"};
        c.ipo_expected_window = "2026";
        c.public_comps        = {"ADBE", "CRM", "FIGM"};
        c.tags                = {"design", "saas", "productivity", "enterprise"};
        c.description         = "Canva is an Australian online design and publishing tool used by "
                                "over 150 million people worldwide. Offers templates for social "
                                "media, presentations, documents, and more.";
        c.rounds = {
            make_round("Series A",  2014, 7,   3.0,  0.04, {"Blackbird", "Matrix"}),
            make_round("Series C",  2019, 10, 85.0,  3.2,  {"Sequoia"}),
            make_round("Series D",  2020, 6,  60.0,  6.0,  {"T. Rowe Price"}),
            make_round("Series E",  2021, 9,  200.0, 40.0, {"T. Rowe Price", "Franklin Templeton"}),
        };
        companies_.append(c);
    }

    // ── Figure AI ────────────────────────────────────────────────────────────
    {
        PrivateCompany c;
        c.id                  = "figure_ai";
        c.name                = "Figure AI";
        c.sector              = "Robotics";
        c.sub_sector          = "Humanoid Robots";
        c.hq_city             = "Sunnyvale";
        c.hq_country          = "USA";
        c.founded             = QDate(2022, 1, 1);
        c.ipo_status          = IpoStatus::Unknown;
        c.last_valuation_usd  = 2.6;
        c.last_round_date     = QDate(2024, 2, 1);
        c.last_round_name     = "Series A";
        c.revenue_est_usd     = 0;
        c.employee_count      = 300;
        c.key_investors       = {"Microsoft", "OpenAI", "Jeff Bezos", "NVIDIA", "Intel Capital"};
        c.ipo_expected_window = "No announced plans";
        c.public_comps        = {"TSLA", "GOOGL", "MSFT"};
        c.tags                = {"robotics", "humanoid", "ai", "manufacturing"};
        c.description         = "Figure AI is building general-purpose humanoid robots designed "
                                "to perform dangerous, dirty, and dull jobs. Partners with BMW "
                                "for manufacturing deployment.";
        c.rounds = {
            make_round("Series A", 2024, 2, 675.0, 2.6, {"Microsoft", "OpenAI", "NVIDIA", "Jeff Bezos"}),
        };
        companies_.append(c);
    }

    // ── Groq ─────────────────────────────────────────────────────────────────
    {
        PrivateCompany c;
        c.id                  = "groq";
        c.name                = "Groq";
        c.sector              = "AI Hardware";
        c.sub_sector          = "AI Inference";
        c.hq_city             = "Mountain View";
        c.hq_country          = "USA";
        c.founded             = QDate(2016, 1, 1);
        c.ipo_status          = IpoStatus::Unknown;
        c.last_valuation_usd  = 2.8;
        c.last_round_date     = QDate(2024, 8, 1);
        c.last_round_name     = "Series D";
        c.revenue_est_usd     = 100;
        c.employee_count      = 250;
        c.key_investors       = {"Tiger Global", "New Enterprise Associates", "D1 Capital"};
        c.ipo_expected_window = "No announced plans";
        c.public_comps        = {"NVDA", "AMD", "INTC"};
        c.tags                = {"ai", "hardware", "chips", "inference"};
        c.description         = "Groq builds the Language Processing Unit (LPU), a purpose-built "
                                "AI inference chip delivering record-breaking token generation speeds. "
                                "Offers GroqCloud for fast LLM API access.";
        c.rounds = {
            make_round("Series C",  2021, 4,  300.0, 1.0,  {"Tiger Global", "D1 Capital"}),
            make_round("Series D",  2024, 8,  640.0, 2.8,  {"Cisco Investments", "KDDI"}),
        };
        companies_.append(c);
    }

    // ── Perplexity ───────────────────────────────────────────────────────────
    {
        PrivateCompany c;
        c.id                  = "perplexity";
        c.name                = "Perplexity AI";
        c.sector              = "AI";
        c.sub_sector          = "AI Search";
        c.hq_city             = "San Francisco";
        c.hq_country          = "USA";
        c.founded             = QDate(2022, 8, 1);
        c.ipo_status          = IpoStatus::Unknown;
        c.last_valuation_usd  = 9.0;
        c.last_round_date     = QDate(2024, 11, 1);
        c.last_round_name     = "Series C";
        c.revenue_est_usd     = 100;
        c.employee_count      = 100;
        c.key_investors       = {"Andreessen Horowitz", "IVP", "NVIDIA", "Jeff Bezos", "SoftBank"};
        c.ipo_expected_window = "No announced plans";
        c.public_comps        = {"GOOGL", "MSFT", "META"};
        c.tags                = {"ai", "search", "llm"};
        c.description         = "Perplexity AI is building the world's best answer engine, "
                                "combining LLMs with real-time web search to deliver cited, "
                                "conversational answers to complex questions.";
        c.rounds = {
            make_round("Seed",     2023, 1,   26.0,  0.1,  {"Andreessen Horowitz"}),
            make_round("Series A", 2023, 3,   25.6,  0.5,  {"New Enterprise Associates"}),
            make_round("Series B", 2024, 1,   73.6,  1.0,  {"IVP", "NVIDIA", "Jeff Bezos"}),
            make_round("Series C", 2024, 11, 500.0,  9.0,  {"SoftBank", "Andreessen Horowitz"}),
        };
        companies_.append(c);
    }

    // ── Shein ─────────────────────────────────────────────────────────────────
    {
        PrivateCompany c;
        c.id                  = "shein";
        c.name                = "Shein";
        c.sector              = "E-Commerce";
        c.sub_sector          = "Fast Fashion";
        c.hq_city             = "Singapore";
        c.hq_country          = "Singapore";
        c.founded             = QDate(2012, 1, 1);
        c.ipo_status          = IpoStatus::Rumored;
        c.last_valuation_usd  = 66.0;
        c.last_round_date     = QDate(2023, 5, 1);
        c.last_round_name     = "Series G";
        c.revenue_est_usd     = 32000;
        c.employee_count      = 10000;
        c.key_investors       = {"General Atlantic", "Tiger Global", "IDG Capital"};
        c.ipo_expected_window = "2025–2026 (London or HK)";
        c.public_comps        = {"AMZN", "PDD", "BABA"};
        c.tags                = {"e-commerce", "fashion", "consumer", "china"};
        c.description         = "Shein is a Chinese-founded fast fashion e-commerce platform "
                                "known for ultra-low prices and rapid design-to-market cycles. "
                                "Exploring a London or Hong Kong IPO amid US scrutiny.";
        c.rounds = {
            make_round("Series B",  2019, 1,  100.0,  3.0,  {"Tiger Global"}),
            make_round("Series D",  2020, 8,  100.0,  5.0,  {"IDG Capital"}),
            make_round("Series E",  2021, 7,  1000.0, 15.0, {"Tiger Global", "General Atlantic"}),
            make_round("Series F",  2022, 4,  1000.0, 100.0,{"General Atlantic", "Sequoia China"}),
            make_round("Series G",  2023, 5,  2000.0, 66.0, {"General Atlantic"}),
        };
        companies_.append(c);
    }

    // ── Epic Games ───────────────────────────────────────────────────────────
    {
        PrivateCompany c;
        c.id                  = "epic_games";
        c.name                = "Epic Games";
        c.sector              = "Gaming";
        c.sub_sector          = "Game Engine & Metaverse";
        c.hq_city             = "Cary";
        c.hq_country          = "USA";
        c.founded             = QDate(1991, 1, 1);
        c.ipo_status          = IpoStatus::Unknown;
        c.last_valuation_usd  = 32.0;
        c.last_round_date     = QDate(2022, 4, 1);
        c.last_round_name     = "Strategic Investment";
        c.revenue_est_usd     = 5600;
        c.employee_count      = 4000;
        c.key_investors       = {"Sony", "Kirkbi (LEGO)", "T. Rowe Price", "Andreessen Horowitz"};
        c.ipo_expected_window = "No announced plans";
        c.public_comps        = {"RBLX", "TTWO", "EA", "ATVI"};
        c.tags                = {"gaming", "metaverse", "unreal-engine", "consumer"};
        c.description         = "Epic Games is the creator of Fortnite and the Unreal Engine, "
                                "one of the most widely used game development platforms. "
                                "Also operates the Epic Games Store as a competitor to Steam.";
        c.rounds = {
            make_round("Series I",         2020, 8, 1780.0, 17.3, {"Sony", "Baillie Gifford"}),
            make_round("Series J",         2021, 4, 1000.0, 28.7, {"Appaloosa"}),
            make_round("Strategic",        2022, 4, 2000.0, 32.0, {"Sony", "Kirkbi"}),
        };
        companies_.append(c);
    }

    // ── Fanatics ─────────────────────────────────────────────────────────────
    {
        PrivateCompany c;
        c.id                  = "fanatics";
        c.name                = "Fanatics";
        c.sector              = "Sports Commerce";
        c.sub_sector          = "Sports Merchandise & Betting";
        c.hq_city             = "Jacksonville";
        c.hq_country          = "USA";
        c.founded             = QDate(1995, 1, 1);
        c.ipo_status          = IpoStatus::Rumored;
        c.last_valuation_usd  = 31.0;
        c.last_round_date     = QDate(2022, 12, 1);
        c.last_round_name     = "Series D";
        c.revenue_est_usd     = 6000;
        c.employee_count      = 9000;
        c.key_investors       = {"Silver Lake", "SoftBank Vision Fund", "Thrive Capital", "NFL", "MLB"};
        c.ipo_expected_window = "2026";
        c.public_comps        = {"DKNG", "FLUT", "EB"};
        c.tags                = {"sports", "betting", "e-commerce", "consumer"};
        c.description         = "Fanatics is a global sports platform company with businesses spanning "
                                "licensed merchandise, sports betting (Fanatics Sportsbook), and NFT "
                                "collectibles. Official merchandise partner of major US sports leagues.";
        c.rounds = {
            make_round("Series C",  2020, 8, 320.0, 6.2,  {"Silver Lake", "SoftBank"}),
            make_round("Series D",  2022, 12, 700.0, 31.0, {"Thrive Capital", "Griffin Gaming"}),
        };
        companies_.append(c);
    }

    // ── Cerebras Systems ────────────────────────────────────────────────────
    {
        PrivateCompany c;
        c.id                  = "cerebras";
        c.name                = "Cerebras Systems";
        c.sector              = "AI Hardware";
        c.sub_sector          = "AI Training Chips";
        c.hq_city             = "Sunnyvale";
        c.hq_country          = "USA";
        c.founded             = QDate(2016, 1, 1);
        c.ipo_status          = IpoStatus::Filed;
        c.last_valuation_usd  = 4.0;
        c.last_round_date     = QDate(2024, 9, 1);
        c.last_round_name     = "Series F";
        c.revenue_est_usd     = 200;
        c.employee_count      = 500;
        c.key_investors       = {"Foundation Capital", "Benchmark", "Altimeter", "Open AI Fund"};
        c.s1_filed_date       = QDate(2024, 9, 30);
        c.ipo_expected_window = "H1 2025";
        c.public_comps        = {"NVDA", "AMD", "INTC"};
        c.tags                = {"ai", "hardware", "chips", "training"};
        c.description         = "Cerebras Systems builds the world's largest AI processor chips — the "
                                "Wafer-Scale Engine (WSE). Designed for ultra-fast AI model training "
                                "at datacenter scale.";
        c.rounds = {
            make_round("Series A",  2016, 12,  27.0,  0.1,  {"Foundation Capital", "Benchmark"}),
            make_round("Series C",  2019, 11, 112.0,  1.7,  {"Benchmark", "Foundation Capital"}),
            make_round("Series F",  2024, 9,  250.0,  4.0,  {"Altimeter", "Open AI Fund"}),
        };
        companies_.append(c);
    }

    // ── Zipline ───────────────────────────────────────────────────────────────
    {
        PrivateCompany c;
        c.id                  = "zipline";
        c.name                = "Zipline";
        c.sector              = "Logistics";
        c.sub_sector          = "Autonomous Delivery Drones";
        c.hq_city             = "San Francisco";
        c.hq_country          = "USA";
        c.founded             = QDate(2014, 1, 1);
        c.ipo_status          = IpoStatus::Unknown;
        c.last_valuation_usd  = 4.2;
        c.last_round_date     = QDate(2021, 11, 1);
        c.last_round_name     = "Series E";
        c.revenue_est_usd     = 100;
        c.employee_count      = 1200;
        c.key_investors       = {"Sequoia", "GV", "Andreessen Horowitz", "Temasek", "Pfizer"};
        c.ipo_expected_window = "No announced plans";
        c.public_comps        = {"UPS", "FDX", "AMZN"};
        c.tags                = {"drones", "logistics", "autonomous", "healthcare"};
        c.description         = "Zipline operates autonomous drone delivery for medical supplies "
                                "and commercial goods. Largest drone delivery service by deliveries, "
                                "operating in Africa, US, and expanding globally.";
        c.rounds = {
            make_round("Series B",  2017, 12,  25.0,  0.2,  {"Sequoia", "GV"}),
            make_round("Series D",  2020, 5,  190.0,  1.2,  {"Andreessen Horowitz"}),
            make_round("Series E",  2021, 11, 250.0,  4.2,  {"Temasek", "Pfizer"}),
        };
        companies_.append(c);
    }

    // ── IPO Pipeline S-1 filings ─────────────────────────────────────────────

    pipeline_ = {
        {
            "Klarna",
            QDate(2025, 3, 1),
            1200.0,
            {"Goldman Sachs", "JP Morgan", "Morgan Stanley"},
            "https://www.sec.gov/cgi-bin/browse-edgar?action=getcompany&company=klarna",
            false
        },
        {
            "eToro",
            QDate(2024, 12, 1),
            500.0,
            {"Goldman Sachs", "Jefferies"},
            "https://www.sec.gov/cgi-bin/browse-edgar?action=getcompany&company=etoro",
            false
        },
        {
            "Cerebras Systems",
            QDate(2024, 9, 30),
            1000.0,
            {"Citigroup", "Barclays", "Credit Suisse"},
            "https://www.sec.gov/cgi-bin/browse-edgar?action=getcompany&company=cerebras",
            false
        },
        {
            "Fanatics",
            QDate(2025, 1, 15),
            2000.0,
            {"JP Morgan", "Goldman Sachs"},
            "https://www.sec.gov/cgi-bin/browse-edgar?action=getcompany&company=fanatics",
            false
        },
    };

    // ── Recent Form D filings ─────────────────────────────────────────────────

    form_d_ = {
        { "Stealth AI Startup", QDate(2025, 4, 2),  25.0,  "506(c)", "Equity", "CA",
          "https://efts.sec.gov/LATEST/search-index?q=%22506c%22&dateRange=custom" },
        { "NovaBio Therapeutics", QDate(2025, 4, 1), 45.0,  "506(b)", "Debt",   "MA",
          "https://efts.sec.gov/LATEST/search-index?q=novabio" },
        { "QuantumEdge Capital", QDate(2025, 3, 28), 120.0, "506(c)", "Equity", "NY",
          "https://efts.sec.gov/LATEST/search-index?q=quantumedge" },
        { "Apex Defense Systems", QDate(2025, 3, 25), 80.0, "506(b)", "Equity", "VA",
          "https://efts.sec.gov/LATEST/search-index?q=apex+defense" },
        { "GreenGrid Energy",     QDate(2025, 3, 22), 60.0, "506(c)", "Equity", "TX",
          "https://efts.sec.gov/LATEST/search-index?q=greengrid" },
        { "DataVault AI",         QDate(2025, 3, 20), 30.0, "506(b)", "Equity", "CA",
          "https://efts.sec.gov/LATEST/search-index?q=datavault" },
        { "StellarMed Devices",   QDate(2025, 3, 18), 15.0, "506(b)", "Debt",   "MN",
          "https://efts.sec.gov/LATEST/search-index?q=stellarmed" },
        { "ClearPath Robotics",   QDate(2025, 3, 15), 95.0, "506(c)", "Equity", "PA",
          "https://efts.sec.gov/LATEST/search-index?q=clearpath" },
    };
}

} // namespace fincept::services
