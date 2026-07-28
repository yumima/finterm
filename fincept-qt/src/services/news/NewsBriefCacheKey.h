#pragma once
// Cache key for AI news briefs.
//
// Extracted from NewsService::summarize_headlines so it can be tested without
// standing up the service, which pulls in LlmService, HttpClient,
// PythonRunner, CacheManager and PortfolioRepository.
//
// Header-only and dependency-free beyond QtCore.

#include <QCryptographicHash>
#include <QString>
#include <QStringList>

#include <algorithm>

namespace fincept::services::brief_cache {

/// Builds the cache key for a brief.
///
/// Every input that can change the generated text is folded in:
///
///   - the headline set, hashed IN FULL. The previous implementation keyed on
///     the joined headlines truncated to 180 characters — roughly the
///     alphabetically-first two or three of up to 35 — so two different sets
///     sharing those few headlines collided and the second caller was served
///     the first one's brief. In a feed where the lead story persists across
///     refreshes while the rest turns over, that is the normal case rather
///     than an edge one.
///   - @p portfolio_id, because the brief carries a holdings section.
///   - @p sample_size, because briefing 20 headlines and 35 of the same feed
///     are different requests.
///   - @p prompt_version, so changing the prompt invalidates old entries
///     instead of leaving them to age out on the TTL.
///
/// Headlines are sorted internally, so feed ordering does not fragment the
/// cache: the same set in a different order is the same key.
inline QString key(QStringList headlines, const QString& portfolio_id, int sample_size,
                   int prompt_version) {
    std::sort(headlines.begin(), headlines.end());
    const QString digest = QString::fromLatin1(
        QCryptographicHash::hash(headlines.join(QLatin1Char('|')).toUtf8(),
                                 QCryptographicHash::Sha1)
            .toHex());
    return QStringLiteral("news:summary:v%1:n%2:%3:%4")
        .arg(prompt_version)
        .arg(sample_size)
        .arg(portfolio_id, digest);
}

} // namespace fincept::services::brief_cache
