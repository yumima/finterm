#include "screens/knowledge/ContentLoader.h"

#include "core/logging/Logger.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

#include <algorithm>

namespace fincept::knowledge {

ContentLoader& ContentLoader::instance() {
    static ContentLoader inst;
    return inst;
}

static QString read_file(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readAll());
}

void ContentLoader::load(const QString& root_dir) {
    root_ = root_dir;
    categories_.clear();
    category_order_.clear();
    entry_index_.clear();
    alias_index_.clear();
    abbreviations_.clear();
    body_cache_.clear();

    QDir root(root_dir);
    if (!root.exists()) {
        LOG_WARN("Knowledge", "Content root does not exist: " + root_dir);
        loaded_ = true;
        emit loaded();
        return;
    }

    // Categories live in subdirectories with an _index.json.
    const auto subdirs = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const auto& sub : subdirs) {
        load_category(root.absoluteFilePath(sub));
    }

    // Sort categories by declared order, then label.
    QVector<QString> order_keys = categories_.keys().toVector();
    std::sort(order_keys.begin(), order_keys.end(), [this](const QString& a, const QString& b) {
        const auto& ca = categories_[a];
        const auto& cb = categories_[b];
        if (ca.order != cb.order)
            return ca.order < cb.order;
        return ca.label < cb.label;
    });
    category_order_ = QStringList(order_keys.begin(), order_keys.end());

    // Rebuild entry pointer index after the categories container is final
    // (QHash entries may move otherwise). We store pointers into the QHash buckets.
    entry_index_.clear();
    alias_index_.clear();
    for (auto& cat_pair : categories_) {
        for (auto& e : cat_pair.entries) {
            entry_index_.insert(e.id, &e);
            alias_index_.insert(e.id.toLower(), e.id);
            for (const auto& alias : e.aliases)
                alias_index_.insert(alias.toLower(), e.id);
            if (!e.abbreviation.isEmpty())
                alias_index_.insert(e.abbreviation.toLower(), e.id);
        }
    }

    // Abbreviations dictionary (separate, flat).
    QFile abbr(root.absoluteFilePath("abbreviations.json"));
    if (abbr.open(QIODevice::ReadOnly)) {
        const auto doc = QJsonDocument::fromJson(abbr.readAll());
        if (doc.isObject()) {
            const auto items = doc.object().value("items").toArray();
            for (const auto& v : items) {
                const auto o = v.toObject();
                const auto k = o.value("abbr").toString();
                const auto v2 = o.value("expansion").toString();
                if (!k.isEmpty())
                    abbreviations_.insert(k, v2);
            }
        }
    }

    loaded_ = true;
    LOG_INFO("Knowledge", QString("Loaded %1 categories, %2 entries, %3 abbreviations from %4")
                             .arg(categories_.size())
                             .arg(entry_index_.size())
                             .arg(abbreviations_.size())
                             .arg(root_dir));
    emit loaded();
}

bool ContentLoader::load_category(const QString& category_dir) {
    QDir dir(category_dir);
    QFile idx(dir.absoluteFilePath("_index.json"));
    if (!idx.exists())
        return false;
    if (!idx.open(QIODevice::ReadOnly)) {
        LOG_WARN("Knowledge", "Cannot open " + idx.fileName());
        return false;
    }
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(idx.readAll(), &err);
    if (err.error != QJsonParseError::NoError) {
        LOG_WARN("Knowledge", QString("Parse error in %1: %2").arg(idx.fileName(), err.errorString()));
        return false;
    }
    if (!doc.isObject())
        return false;
    const auto root = doc.object();
    const auto cat_obj = root.value("category").toObject();

    KnowledgeCategory cat;
    cat.id = cat_obj.value("id").toString();
    cat.label = cat_obj.value("label").toString(cat.id.toUpper());
    cat.description = cat_obj.value("description").toString();
    cat.order = cat_obj.value("order").toInt(100);

    if (cat.id.isEmpty()) {
        cat.id = QFileInfo(category_dir).fileName();
    }

    const auto entries = root.value("entries").toArray();
    for (const auto& v : entries) {
        if (!v.isObject())
            continue;
        cat.entries.push_back(parse_entry(v.toObject(), cat.id));
    }
    categories_.insert(cat.id, cat);
    return true;
}

KnowledgeEntry ContentLoader::parse_entry(const QJsonObject& obj, const QString& category_id) {
    KnowledgeEntry e;
    e.id = obj.value("id").toString();
    e.title = obj.value("title").toString(e.id);
    e.subtitle = obj.value("subtitle").toString();
    e.body_path = obj.value("body").toString();
    e.category = category_id;
    e.difficulty = obj.value("difficulty").toString(Difficulty::BEGINNER);
    e.abbreviation = obj.value("abbreviation").toString();
    e.updated = obj.value("updated").toString();

    for (const auto& v : obj.value("tags").toArray())
        e.tags << v.toString();
    for (const auto& v : obj.value("aliases").toArray())
        e.aliases << v.toString();
    for (const auto& v : obj.value("related").toArray())
        e.related << v.toString();

    for (const auto& v : obj.value("seen_in").toArray()) {
        const auto o = v.toObject();
        ScreenRef r;
        r.screen = o.value("screen").toString();
        r.label = o.value("label").toString();
        if (!r.screen.isEmpty())
            e.seen_in.push_back(r);
    }
    for (const auto& v : obj.value("actions").toArray()) {
        const auto o = v.toObject();
        ActionRef a;
        a.label = o.value("label").toString();
        a.screen = o.value("screen").toString();
        a.ticker = o.value("ticker").toString();
        a.hint = o.value("hint").toString();
        if (!a.label.isEmpty() && !a.screen.isEmpty())
            e.actions.push_back(a);
    }
    for (const auto& v : obj.value("live_examples").toArray()) {
        const auto o = v.toObject();
        LiveExample x;
        x.label = o.value("label").toString();
        x.ticker = o.value("ticker").toString();
        x.metric = o.value("metric").toString();
        if (!x.ticker.isEmpty() && !x.metric.isEmpty())
            e.live_examples.push_back(x);
    }
    for (const auto& v : obj.value("rules_of_thumb").toArray()) {
        const auto o = v.toObject();
        RuleOfThumb r;
        r.label = o.value("label").toString();
        r.value = o.value("value").toString();
        r.note = o.value("note").toString();
        if (!r.label.isEmpty() || !r.value.isEmpty())
            e.rules_of_thumb.push_back(r);
    }
    for (const auto& v : obj.value("pitfalls").toArray()) {
        Pitfall p;
        if (v.isString()) {
            p.text = v.toString();
        } else if (v.isObject()) {
            p.text = v.toObject().value("text").toString();
        }
        if (!p.text.isEmpty())
            e.pitfalls.push_back(p);
    }
    for (const auto& v : obj.value("peer_tickers").toArray())
        e.peer_tickers.push_back(v.toString());
    e.primary_ticker = obj.value("primary_ticker").toString();
    e.primary_metric = obj.value("primary_metric").toString();
    {
        const auto co = obj.value("calculator").toObject();
        if (!co.isEmpty()) {
            e.calculator.kind = co.value("kind").toString();
            e.calculator.numerator_label = co.value("numerator_label").toString();
            e.calculator.denominator_label = co.value("denominator_label").toString();
            e.calculator.result_label = co.value("result_label").toString();
            e.calculator.result_format = co.value("result_format").toString("ratio");
            e.calculator.numerator_default = co.value("numerator_default").toDouble();
            e.calculator.denominator_default = co.value("denominator_default").toDouble();
        }
    }
    for (const auto& v : obj.value("exposure_tickers").toArray())
        e.exposure_tickers.push_back(v.toString());
    e.exposure_criterion = obj.value("exposure_criterion").toString();
    for (const auto& v : obj.value("external_links").toArray()) {
        const auto o = v.toObject();
        ExternalLink l;
        l.type = o.value("type").toString("primer");
        l.source = o.value("source").toString();
        l.title = o.value("title").toString();
        l.url = o.value("url").toString();
        if (!l.url.isEmpty())
            e.external_links.push_back(l);
    }
    for (const auto& v : obj.value("warnings").toArray()) {
        const auto o = v.toObject();
        Warning w;
        w.severity = o.value("severity").toString("medium");
        w.text = o.value("text").toString();
        if (!w.text.isEmpty())
            e.warnings.push_back(w);
    }
    return e;
}

QVector<KnowledgeCategory> ContentLoader::categories() const {
    QVector<KnowledgeCategory> out;
    out.reserve(category_order_.size());
    for (const auto& id : category_order_) {
        auto it = categories_.find(id);
        if (it != categories_.end())
            out.push_back(it.value());
    }
    return out;
}

const KnowledgeCategory* ContentLoader::category(const QString& id) const {
    auto it = categories_.find(id);
    return it == categories_.end() ? nullptr : &it.value();
}

const KnowledgeEntry* ContentLoader::entry(const QString& id) const {
    auto it = entry_index_.find(id);
    return it == entry_index_.end() ? nullptr : it.value();
}

QString ContentLoader::resolve_alias(const QString& alias_or_id) const {
    if (alias_or_id.isEmpty())
        return {};
    auto it = alias_index_.find(alias_or_id.toLower());
    return it == alias_index_.end() ? QString{} : it.value();
}

QStringList ContentLoader::search(const QString& query, int limit) const {
    if (query.trimmed().isEmpty())
        return {};
    const auto q = query.trimmed().toLower();

    // Tiered scoring: exact alias = 100, prefix on title = 60, substring on
    // title/abbr = 40, substring on aliases/tags = 25.
    QHash<QString, int> scored;
    auto bump = [&](const QString& id, int score) {
        auto it = scored.find(id);
        if (it == scored.end() || it.value() < score)
            scored.insert(id, score);
    };

    if (auto exact = alias_index_.value(q); !exact.isEmpty())
        bump(exact, 100);

    for (const auto& cat : categories_) {
        for (const auto& e : cat.entries) {
            const auto title_l = e.title.toLower();
            const auto abbr_l = e.abbreviation.toLower();
            if (title_l.startsWith(q))
                bump(e.id, 60);
            else if (title_l.contains(q))
                bump(e.id, 40);
            if (!abbr_l.isEmpty() && abbr_l.contains(q))
                bump(e.id, 50);
            for (const auto& a : e.aliases) {
                if (a.toLower().contains(q)) {
                    bump(e.id, 25);
                    break;
                }
            }
            for (const auto& t : e.tags) {
                if (t.toLower().contains(q)) {
                    bump(e.id, 15);
                    break;
                }
            }
        }
    }

    QVector<QPair<int, QString>> ranked;
    ranked.reserve(scored.size());
    for (auto it = scored.begin(); it != scored.end(); ++it)
        ranked.push_back({it.value(), it.key()});
    std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first)
            return a.first > b.first;
        return a.second < b.second;
    });
    QStringList out;
    out.reserve(qMin(limit, int(ranked.size())));
    for (int i = 0; i < ranked.size() && i < limit; ++i)
        out.push_back(ranked[i].second);
    return out;
}

QString ContentLoader::load_body(const QString& entry_id) const {
    if (auto it = body_cache_.find(entry_id); it != body_cache_.end())
        return it.value();
    const auto* e = entry(entry_id);
    if (!e || e->body_path.isEmpty())
        return {};
    const QString full = QDir(root_).absoluteFilePath(e->body_path);
    QString body = read_file(full);
    body_cache_.insert(entry_id, body);
    return body;
}

} // namespace fincept::knowledge
