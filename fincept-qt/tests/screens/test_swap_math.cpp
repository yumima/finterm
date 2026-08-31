// tests/screens/test_swap_math.cpp
//
// A swap has a CONFIGURED slippage tolerance and an EFFECTIVE one. They are not
// the same number: the settings stored basis points, and PumpPortal's trade API
// takes an integer percent, so the configured value is rounded up on its way
// into the transaction.
//
// The UI used the two interchangeably. The swap panel and — worse — the confirm
// dialog both displayed the CONFIGURED value while the transaction carried the
// EFFECTIVE one, and the dialog's warning named the figure it showed as the
// tolerance the trade would be rejected past. A 0.25% setting was shown as
// 0.25% and sent as 1%: on the last screen before an irreversible on-chain
// swap, the stated bound on how much value could be lost was four times too
// tight.
//
// These cases pin the mapping, and pin the property that actually matters:
// what is displayed is what is sent.

#include "screens/crypto_center/SwapMath.h"
#include "services/wallet/PumpFunSwapService.h"

#include <QtTest/QtTest>

#include <cmath>

using namespace fincept::screens::swapmath;

class TestSwapMath : public QObject {
    Q_OBJECT

  private slots:

    // ── The property the whole header exists for ─────────────────────────────
    void what_is_displayed_is_what_is_sent() {
        // Every value the settings slider can hold, and every legacy value the
        // old fine-grained slider could have stored.
        for (int bps = 10; bps <= 500; bps += 5) {
            const int sent = effective_pct(bps);
            const QString shown = format_pct(bps);
            QCOMPARE(shown, QString::number(sent) + QStringLiteral(".00%"));
            // …and the worst-case factor agrees with both.
            QVERIFY(std::abs(factor(bps) - (1.0 - sent / 100.0)) < 1e-12);
            QCOMPARE(effective_bps(bps), sent * 100);
        }
    }

    // ── The rounding rule ────────────────────────────────────────────────────
    void a_configured_tolerance_rounds_up_never_down() {
        // Up, so a swap is never tolerance-starved into a failure the user did
        // not ask for. The cost is that it is never TIGHTER than configured —
        // which is exactly why the effective value has to be the one displayed.
        QCOMPARE(effective_pct(10), 1);    // 0.10% -> 1%   (10x looser)
        QCOMPARE(effective_pct(25), 1);    // 0.25% -> 1%
        QCOMPARE(effective_pct(99), 1);
        QCOMPARE(effective_pct(100), 1);   // exact percent stays put
        QCOMPARE(effective_pct(101), 2);
        QCOMPARE(effective_pct(150), 2);
        QCOMPARE(effective_pct(200), 2);
        QCOMPARE(effective_pct(201), 3);
        QCOMPARE(effective_pct(500), 5);
    }

    void the_effective_value_is_never_tighter_than_configured() {
        for (int bps = 10; bps <= 500; ++bps)
            QVERIFY2(effective_bps(bps) >= bps,
                     qPrintable(QString("bps %1 -> effective %2 is TIGHTER than asked")
                                    .arg(bps).arg(effective_bps(bps))));
    }

    // ── The service's range is mirrored, not guessed ─────────────────────────
    void the_range_matches_what_the_service_accepts() {
        // Compared against the SERVICE's own constants, not against literals.
        // Restating 1 and 5 here would pass happily if the service later
        // widened to 1..10 while the UI went on capping every user at 5% — the
        // exact drift this header claims to prevent, certified by its own test.
        using Svc = fincept::wallet::PumpFunSwapService;
        QCOMPARE(kMinPct, Svc::kSlippagePctMin);
        QCOMPARE(kMaxPct, Svc::kSlippagePctMax);
        QVERIFY(kMinPct >= 1 && kMaxPct > kMinPct);
        // Clamping is asserted against the range too, not against literals —
        // a hard-coded 5 here would fail for the wrong reason the moment the
        // service widened, which is noise rather than protection.
        QCOMPARE(effective_pct(0), kMinPct);       // below range
        QCOMPARE(effective_pct(-50), kMinPct);     // nonsense in, clamped
        QCOMPARE(effective_pct(9999), kMaxPct);    // above range
    }

    // ── The factor a quoted output is reduced by ─────────────────────────────
    void the_worst_case_factor_is_the_one_the_panel_shows() {
        QVERIFY(std::abs(factor(100) - 0.99) < 1e-12);
        QVERIFY(std::abs(factor(500) - 0.95) < 1e-12);
        // A 25 bps setting must NOT produce a 0.9975 factor — that would be the
        // configured tolerance leaking back into a user-facing number.
        QVERIFY2(std::abs(factor(25) - 0.99) < 1e-12,
                 "factor must follow the EFFECTIVE tolerance, not the setting");
    }

    // The settings slider persists whatever it is snapped to, so every value it
    // can store must already be a fixed point of the mapping — otherwise the
    // handle moves by itself on the next visit.
    void a_snapped_value_is_stable_under_re_snapping() {
        for (int pct = kMinPct; pct <= kMaxPct; ++pct) {
            const int bps = pct * 100;
            QCOMPARE(effective_bps(bps), bps);
            QCOMPARE(effective_bps(effective_bps(bps)), bps);
        }
        // …and any legacy value snaps to one of those in a single step.
        for (int bps = 10; bps <= 500; ++bps) {
            const int once = effective_bps(bps);
            QCOMPARE(effective_bps(once), once);
        }
    }

    void format_is_always_whole_percent() {
        // Two decimals of a value that can only be a whole percent invited the
        // reader to believe a precision the transaction cannot express.
        QCOMPARE(format_pct(10), QStringLiteral("1.00%"));
        QCOMPARE(format_pct(25), QStringLiteral("1.00%"));
        QCOMPARE(format_pct(250), QStringLiteral("3.00%"));
        QCOMPARE(format_pct(500), QStringLiteral("5.00%"));
    }
};

QTEST_MAIN(TestSwapMath)
#include "test_swap_math.moc"
