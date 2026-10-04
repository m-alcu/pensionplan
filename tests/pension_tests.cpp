// Engine tests. Nothing personal is compiled in: they run against the worked
// example in examples/, and the legal tables are checked on their own.
//
// Point PENSION_FIXTURE at a real .dat exported from an INSS simulation and the
// last block also checks the engine against the figures that simulation printed
// -- the [informe] section of the file. That is the strongest check there is,
// but it needs data that does not belong in a public repository.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "pension.h"
#include "pensioner.h"

using namespace pension;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("%s  %s\n", ok ? "[ ok ]" : "[FAIL]", what.c_str());
    if (!ok) ++failures;
}

void near(double got, double want, double tol, const std::string& what) {
    bool ok = std::fabs(got - want) <= tol;
    char buf[256];
    std::snprintf(buf, sizeof buf, "%s: got %.4f, want %.4f (+-%.4g)", what.c_str(), got, want,
                  tol);
    check(ok, buf);
}

std::string plain(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.0f", v);
    return buf;
}

// The example, loaded once and made active.
pensioner::Pensioner loadExample() {
    pensioner::Pensioner p;
    std::string error;
    for (const auto& path : pensioner::exampleSearchPaths())
        if (pensioner::load(path, p, error)) return p;
    std::printf("[FAIL]  no se encuentra examples/pensionista-ejemplo.dat (%s)\n", error.c_str());
    std::exit(1);
}

}  // namespace

int main() {
    std::printf("-- calendar --\n");
    check(daysInclusive({1996, 3, 4}, {2001, 10, 31}) == 2068, "inclusive day counts");
    check(daysInclusive({2026, 10, 5}, {2035, 7, 10}) == 3201, "across many years");
    check(daysInclusive({2025, 3, 1}, {2026, 10, 4}) == 583, "and across a year boundary");
    check(daysInclusive({2024, 1, 1}, {2024, 1, 1}) == 1, "a single day is one day");
    check(monthOrdinal({2035, 7}) - monthOrdinal({2010, 6}) == 301, "month arithmetic");
    check(daysInMonth(2024, 2) == 29 && daysInMonth(2025, 2) == 28, "February leap years");
    check(formatDate(addMonthsToDate({2024, 1, 31}, 1)) == "29/02/2024",
          "adding a month clamps to the shorter month");

    std::printf("\n-- the dual regulatory base, DT 40.a LGSS --\n");
    for (int y : {2026, 2027, 2028, 2035, 2037, 2040}) {
        DualRule d = dualRuleFor(y);
        std::printf("       %d: %d months, %d bases, /%.2f\n", y, d.windowMonths, d.basesTaken,
                    d.divisor);
    }
    {
        DualRule d = dualRuleFor(2026);
        check(d.windowMonths == 304 && d.basesTaken == 302 && std::fabs(d.divisor - 352.33) < 0.01,
              "2026: 302 of 304 over 352,33");
        d = dualRuleFor(2027);
        check(d.windowMonths == 308 && d.basesTaken == 304 && std::fabs(d.divisor - 354.67) < 0.01,
              "2027: 304 of 308 over 354,67");
        d = dualRuleFor(2028);
        check(d.windowMonths == 312 && d.basesTaken == 306 && std::fabs(d.divisor - 357.0) < 0.01,
              "2028: 306 of 312 over 357");
        d = dualRuleFor(2035);
        check(d.windowMonths == 340 && d.basesTaken == 320 && std::fabs(d.divisor - 373.33) < 0.01,
              "2035: 320 of 340 over 373,33");
        d = dualRuleFor(2037);
        check(d.windowMonths == 348 && d.basesTaken == 324 && std::fabs(d.divisor - 378.0) < 0.01,
              "2037: the final 324 of 348 over 378");
        check(dualRuleFor(2040).windowMonths == 348, "and it stays there after 2037");
        check(!dualRuleFor(2025).applicable, "no dual rule before 2026");
    }

    std::printf("\n-- early and late retirement --\n");
    check(std::fabs(reductionCoefficient(24, 14372, true) - 19.0) < 1e-9,
          "24 months early on a 39-year career costs 19%");
    check(std::fabs(reductionCoefficient(24, 13000, true) - 21.0) < 1e-9,
          "and 21% under 38 years 6 months");
    check(std::fabs(reductionCoefficient(1, 13000, true) - 3.26) < 1e-9,
          "1 month early under 38y6m costs 3,26%");
    check(std::fabs(reductionCoefficient(12, 16500, true) - 4.75) < 1e-9,
          "12 months early past 44y6m costs 4,75%");
    check(reductionCoefficient(0, 14372, true) == 0.0, "no anticipation, no reduction");
    check(reductionCoefficient(12, 14372, false) < reductionCoefficient(12, 14372, true),
          "involuntary early retirement is milder");

    std::printf("\n-- projections --\n");
    {
        Params p;
        p.anchorYear = 2026;
        p.maxPensionAnchor = 3359.60;
        p.maximumBaseAnchor = 5101.20;
        p.inflationPct = 2.0;
        p.maxPensionExtraPct = 0.115;
        p.maximumBaseExtraPct = 1.2;
        near(maxPensionFor(p, 2026), 3359.60, 0.01, "the cap anchors on its own year");
        // CPI plus the extra 0,115 pp a year that RDL 2/2023 set out to 2050.
        near(maxPensionFor(p, 2035), 3359.60 * std::pow(1.02115, 9), 0.01,
             "and compounds at CPI + 0,115 pp");
        near(maximumBaseFor(p, 2026), 5101.20, 0.01, "the maximum base anchors too");
        near(maximumBaseFor(p, 2035), 5101.20 * std::pow(1.032, 9), 0.01,
             "and compounds at CPI + 1,2 pp");
        check(maximumBaseFor(p, 2050) / maxPensionFor(p, 2050) >
                  maximumBaseFor(p, 2026) / maxPensionFor(p, 2026),
              "so the maximum base pulls away from the cap over time");
        near(historicalMaximumBase(2010), 3198.00, 0.001, "2010 maximum base");
        near(historicalMaximumBase(2025), 4909.50, 0.001, "2025 maximum base");
        near(historicalMaximumBase(2007), 2996.10, 0.001, "2007 maximum base");
    }

    std::printf("\n-- convenio especial, Orden TAS/2865/2003 --\n");
    {
        near(meiRatePct(2026), 0.9, 1e-9, "MEI is 0,9% in 2026");
        near(meiRatePct(2029), 1.2, 1e-9, "and settles at 1,2% from 2029");
        check(meiRatePct(2040) == meiRatePct(2029), "staying there afterwards");
        Params p;
        p.convenioRatePct = 28.30;
        p.convenioCoefficient = 0.94;
        // The worked example in the Social Security's own guide.
        near(convenioMonthlyCost(p, 2000.0, 2026), 550.04, 0.01,
             "2.000 EUR costs 550,04 EUR/month, as the official guide says");
    }

    std::printf("\n-- the worked example --\n");
    pensioner::Pensioner ex = loadExample();
    setActiveDataset(ex.data);
    const Result base = compute(ex.params, ex.scenario);

    check(ex.data.historicBases.size() == 236,
          "236 recorded bases (" + std::to_string(ex.data.historicBases.size()) + ")");
    check(ex.data.priceIndex.size() == 235,
          "235 CPI months (" + std::to_string(ex.data.priceIndex.size()) + ")");
    check(base.eligible, "the example is entitled to a pension");
    check(base.daysContributed == 15172,
          "15.172 days contributed (" + std::to_string(base.daysContributed) + ")");
    near(base.percentByYears, 100.0, 1e-9, "a full career earns 100%");
    check(formatDate(base.ordinaryRetirement) == "15/03/2036",
          "retiring at 65, the career clearing 38 years 6 months");
    check(base.monthsAnticipated == 0, "exactly at the ordinary age");

    {
        int inA = 0, nominal = 0;
        const MonthDetail *first = nullptr, *last = nullptr;
        for (const auto& m : base.months) {
            if (!m.inWindowA) continue;
            ++inA;
            if (m.nominal) ++nominal;
            if (!first) first = &m;
            last = &m;
        }
        check(inA == 300, "300 months in window A (" + std::to_string(inA) + ")");
        check(nominal == 24, "24 of them at face value (" + std::to_string(nominal) + ")");
        check(first && first->ym.year == 2011 && first->ym.month == 2,
              "the window opens 300 months before the month prior to the hecho causante");
        check(last && last->ym.year == 2036 && last->ym.month == 1, "and closes in Jan-2036");
    }
    // Retiring in 2036 is past the DT 40.a phase-in, so option B is the final one.
    check(base.optionB.applicable && base.optionB.windowMonths == 344,
          "option B looks at 344 months in 2036");
    check(!base.optionBWins, "an unbroken career at the maximum base keeps option A ahead");
    near(base.maxPension, 3359.60 * std::pow(1.02115, 10), 0.02, "the 2036 cap");
    check(base.capBinding, "a career at the maximum base is capped");
    near(base.pension, base.effectiveCap, 0.01, "so the pension sits exactly on the cap");
    near(base.pensionAnnual, base.pension * 14.0, 0.01, "paid over 14 instalments");
    near(base.pensionInTodayEuros,
         base.pension / std::pow(1.02, ex.params.retirement.year - ex.params.anchorYear), 0.01,
         "and deflated at the projected CPI");

    std::printf("\n-- what moves the pension, and what does not --\n");
    {
        // Capped, so a richer future changes nothing.
        auto richer = ex.scenario;
        richer[0].annualGrowthPct = 8.0;
        Result rich = compute(ex.params, richer);
        check(rich.baseReguladora > base.baseReguladora, "a steeper base lifts the regulatory base");
        near(rich.pension, base.pension, 0.01, "but not the pension: the cap decides");

        auto poorer = ex.scenario;
        poorer[0].annualGrowthPct = -2.0;
        check(compute(ex.params, poorer).pension >= base.pension - 0.01,
              "and neither does a falling one, while the cap still bites");
    }
    {
        // Stopping outright: the days stop, and with them the ordinary age.
        auto stop = ex.scenario;
        stop[0].kind = PeriodKind::Gap;
        Result idle = compute(ex.params, stop);
        check(idle.daysContributed < base.daysContributed, "a gap stops the clock");
        check(idle.percentByYears < 100.0, "and costs percentage");
        check(idle.baseReguladora < base.baseReguladora, "and regulatory base");
        check(idle.optionBWins, "with holes in it, the dual rule now wins");
        check(idle.pension < base.pension, "so the pension finally moves");
    }
    {
        // Delaying past the ordinary age, while already on the cap.
        Params p = ex.params;
        p.retirement = {2038, 3, 15};
        auto sc = ex.scenario;
        sc[0].to = p.retirement;
        Result late = compute(p, sc);
        check(late.monthsAnticipated == -24, "24 months late");
        check(late.yearsDelayed == 2, "two complete years of delay");
        near(late.delayPct, 8.0, 1e-9, "an 8% bonus, 4% a year");
        check(late.delaySupplement > 0.0, "paid as a supplement, the cap being full");
        check(late.pension > late.maxPension, "so the total does beat the bare cap");
    }
    {
        // Early, which here needs the career to still clear 38 years 6 months.
        Params p = ex.params;
        p.retirement = {2035, 9, 15};
        auto sc = ex.scenario;
        sc[0].to = p.retirement;
        Result early = compute(p, sc);
        check(early.monthsAnticipated == 6, "six months early (" +
                                                std::to_string(early.monthsAnticipated) + ")");
        check(!early.anticipationTooLong, "which art. 208 allows");
        check(early.reductionPct > 0, "a reducing coefficient applies");
        check(early.effectiveCap < early.maxPension,
              "and it bites on the cap itself, not just the computation");
        check(early.pension < base.pension, "so the pension comes out lower");
    }

    std::printf("\n-- stopping work and paying a convenio especial --\n");
    {
        const Date stop{2031, 3, 15};
        const double target = base.pension;
        ConvenioPlan c = planConvenio(ex.params, ex.scenario, stop, target, 2.0);
        check(c.feasible, "the pension can be held after stopping work");
        near(c.pensionWithConvenio, target, 0.01, "exactly at the target");
        check(c.minimumBase < maximumBaseFor(ex.params, 2031) * 0.4,
              "on a fraction of the maximum base: the cap leaves slack");
        check(c.pensionWithoutConvenio < target - 1.0,
              "while stopping outright costs real money");

        // Convenio days count like employment.
        Result with = compute(ex.params, scenarioStoppingAt(ex.scenario, stop, ex.params.retirement,
                                                            true, c.minimumBase, 2.0,
                                                            ex.params.retirement));
        check(with.daysContributed == base.daysContributed,
              "a convenio keeps the career clock running");
        check(with.convenioMonths > 0 && with.convenioTotalCost > 0, "and costs money");

        // It need not run to the end: once the career has its 38 years and 6
        // months the remaining months are integrated for free.
        check(compare(c.convenioUntil, ex.params.retirement) < 0,
              "it can be cancelled early (" + formatDate(c.convenioUntil) + ")");
        check(c.saving > 0, "which saves money (" + plain(c.saving) + ")");
        check(c.totalCost < c.costIfRunToRetirement, "for a smaller bill");
        Result tooShort = compute(ex.params,
                                  scenarioStoppingAt(ex.scenario, stop, ex.params.retirement, true,
                                                     c.minimumBase, 2.0,
                                                     addMonthsToDate(c.convenioUntil, -1)));
        check(tooShort.pension < target - 0.01, "but cancelling a month earlier does cost pension");

        Params noTrim = ex.params;
        noTrim.trimConvenioWhenPossible = false;
        ConvenioPlan full = planConvenio(noTrim, ex.scenario, stop, target, 2.0);
        check(compare(full.convenioUntil, ex.params.retirement) == 0,
              "unless the trim is switched off");

        // Stopping earlier needs a dearer base, and costs more in total.
        ConvenioPlan earlier = planConvenio(ex.params, ex.scenario, {2028, 3, 15}, target, 2.0);
        check(earlier.minimumBase >= c.minimumBase, "stopping earlier needs at least as much base");
        check(earlier.totalCost > c.totalCost, "and costs more in total");
    }

    std::printf("\n-- persistence --\n");
    {
        const std::string path = "/tmp/pensionplan_roundtrip.dat";
        std::string error;
        check(pensioner::save(path, ex, error), "the data file writes out");
        pensioner::Pensioner back;
        check(pensioner::load(path, back, error),
              "and reads back in" + (error.empty() ? "" : ": " + error));
        check(back.identity.name == ex.identity.name, "the holder's name survives");
        check(formatDate(back.params.birth) == formatDate(ex.params.birth), "the date of birth");
        check(formatDate(back.params.retirement) == formatDate(ex.params.retirement),
              "and the retirement date");
        check(back.data.workPeriods.size() == ex.data.workPeriods.size(),
              "every working-life period survives");
        check(back.data.historicBases.size() == ex.data.historicBases.size(),
              "every recorded base survives");
        check(back.data.priceIndex.size() == ex.data.priceIndex.size(), "and the whole CPI series");
        check(back.scenario.size() == ex.scenario.size(), "the scenario survives");
        near(back.params.convenioFloorBase, ex.params.convenioFloorBase, 1e-9, "and the parameters");

        setActiveDataset(back.data);
        Result again = compute(back.params, back.scenario);
        near(again.baseReguladora, base.baseReguladora, 0.001,
             "the reloaded data gives the same regulatory base");
        near(again.pension, base.pension, 0.001, "and the same pension");
        setActiveDataset(ex.data);
        std::remove(path.c_str());
    }
    {
        const std::string ini = "/tmp/pensionplan_test.ini";
        pensioner::Config c;
        c.root = "/tmp/pensionplan_datos";
        c.dataFile = "titular.dat";
        check(c.path() == "/tmp/pensionplan_datos/titular.dat", "config joins root and filename");
        std::string error;
        check(pensioner::saveConfig(ini, c, error), "config.ini writes out");
        pensioner::Config back = pensioner::loadConfig(ini);
        check(back.root == c.root && back.dataFile == c.dataFile, "and reads back in");
        pensioner::Config missing = pensioner::loadConfig("/tmp/no-existe-esto.ini");
        check(missing.root == "data" && missing.dataFile == "pensionista.dat",
              "a missing config.ini falls back to the defaults");
        std::remove(ini.c_str());
    }

    // The strongest check available, when there is a real simulation to hand:
    // the [informe] section records what the INSS printed, so the engine has to
    // land on it. Skipped when no fixture is configured.
    if (const char* fixture = std::getenv("PENSION_FIXTURE")) {
        std::printf("\n-- against a real INSS simulation (%s) --\n", fixture);
        pensioner::Pensioner real;
        std::string error;
        if (!pensioner::load(fixture, real, error)) {
            check(false, "the fixture loads: " + error);
        } else if (real.report.pension <= 0) {
            std::printf("       el fichero no trae seccion [informe]; nada que comparar\n");
        } else {
            setActiveDataset(real.data);
            Result r = compute(real.params, real.scenario);
            check(r.daysContributed == real.report.daysWholeLife,
                  "days over the whole working life");
            check(r.daysLast15Years == real.report.daysLast15Years, "days in the last 15 years");
            near(r.yearsComputable, real.report.yearsComputable, 0.01, "years computable");
            // The report rounds each actualised base to the cent before adding
            // them up, so allow a couple of cents on the regulatory base.
            near(r.baseReguladora, real.report.baseReguladora, 0.02, "base reguladora");
            near(r.percentByYears, real.report.percentTotal, 1e-9, "percentage by years");
            near(r.calcInitial, real.report.calcInitial, 0.02, "initial calculation");
            near(r.maxPension, real.report.maxPension, 0.01, "the cap for the retirement year");
            near(r.excess, real.report.excess, 0.03, "the excess the cap trims");
            near(r.pension, real.report.pension, 0.02, "and the pension itself");
            near(r.pensionInTodayEuros, real.report.pensionDeflated, 0.02, "deflated to today");
            setActiveDataset(ex.data);
        }
    } else {
        std::printf("\n(PENSION_FIXTURE sin definir: omitida la comparacion con un informe real)\n");
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "all good", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
