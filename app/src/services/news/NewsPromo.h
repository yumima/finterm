#pragma once
// NewsPromo.h — coupon / promo-code / shopping-deal spam in news feeds.
//
// General feeds syndicate affiliate posts ("Chatbooks Promo Code: 40% Off")
// that ranked into the INTEL drawer's top stories. They are adverts, not news;
// is_promo_spam() lets a ranked list skip them. Headline-only on purpose: a
// real story can mention a discount in its body.

#include <QRegularExpression>
#include <QString>

namespace finterm::news {

inline bool is_promo_spam(const QString& headline) {
    // Unambiguous affiliate vocabulary.
    static const QRegularExpression kPromo(
        QStringLiteral("\\b(promo|coupon|discount|voucher|referral)\\s*codes?\\b|\\bcoupons?\\b|"
                       "\\bpromo\\b|\\bbest\\b.*\\bdeals\\b|\\b(black friday|cyber monday|prime day)\\b.*\\bdeals?\\b|"
                       "\\bdeals? of the (day|week)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    // "40% off" only alongside shopping words — "shares 40% off their highs"
    // is market news.
    static const QRegularExpression kPctOff(QStringLiteral("\\d+\\s*%\\s*off\\b"),
                                            QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression kShopping(
        QStringLiteral("\\b(sale|deal|deals|code|codes|save|shop|order|subscription|discount)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    if (kPromo.match(headline).hasMatch())
        return true;
    return kPctOff.match(headline).hasMatch() && kShopping.match(headline).hasMatch();
}

} // namespace finterm::news
