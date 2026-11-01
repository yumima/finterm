#pragma once
// Merging one feed's batch into the running article list.
//
// Publishers syndicate one story into several of their own feeds: a BBC China
// story is also in BBC World, and often in BBC Business. Each copy has its
// own article id, so without this the wire list shows the story two or three
// times. Copies are matched on their exact link. The sampled BBC feeds repeat
// the same URL, tracking query included, so no normalisation is needed.
//
// When copies disagree on region, a pill region (US / CHINA / EU, see
// is_pill_feed_region) wins over any other tag. Copies arrive in network
// order, so if the BBC World (GLOBAL) or SCMP News (ASIA) copy came first and
// simply stayed, the story would lose the CHINA tag its China-desk copy
// carries, and with it its place under the CHN pill.

#include "services/news/NewsRegions.h"
#include "services/news/NewsService.h"

#include <QHash>
#include <QString>
#include <QVector>

namespace fincept::news {

inline void merge_feed_batch(QVector<services::NewsArticle>& all, QHash<QString, qsizetype>& index_by_link,
                             const QVector<services::NewsArticle>& batch) {
    for (const auto& a : batch) {
        const QString link = a.link.trimmed();
        if (link.isEmpty()) {
            all.append(a);
            continue;
        }
        const auto it = index_by_link.constFind(link);
        if (it == index_by_link.constEnd()) {
            index_by_link.insert(link, all.size());
            all.append(a);
            continue;
        }
        auto& kept = all[it.value()];
        if (!is_pill_feed_region(kept.region) && is_pill_feed_region(a.region))
            kept.region = a.region;
    }
}

} // namespace fincept::news
