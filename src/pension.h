// The pension arithmetic, with no UI dependencies.
//
// Everything the Spanish system needs to turn a contribution history into a
// monthly pension: the regulatory base over its 25-year (or, from 2026, dual)
// window, the percentage earned by years contributed, the early/late retirement
// adjustment, and the maximum-pension cap in force the year you retire.
//
// The engine is calibrated against the INSS report in assets/: feeding it the
// report's own parameters reproduces its base reguladora (4.817,15 EUR) and its
// pension (4.055,96 EUR) to the cent. See tests/pension_tests.cpp.
//
// Legal references, all as amended by Real Decreto-ley 2/2023:
//   art. 209 LGSS + DT 40a  base reguladora and the 2026-2037 dual phase-in
//   art. 210.1 LGSS + DT 9a percentage by years contributed (Ley 27/2011 scale)
//   art. 208 LGSS           voluntary early retirement, reduction coefficients
//   art. 210.2 LGSS         delayed retirement, 4% per complete year
//   art. 57 LGSS            maximum pension and its 0,115 pp/year extra rise
#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace pension {

// ---------------------------------------------------------------------------
// Calendar helpers. Months are 1..12; a "month ordinal" is year*12 + (month-1)
// so that arithmetic on months is plain integer arithmetic.

struct Date {
    int year = 0, month = 0, day = 0;
};

struct Month {
    int year = 0, month = 0;
};

int monthOrdinal(Month m);
Month monthFromOrdinal(int ordinal);
Month monthOf(Date d);
Month addMonths(Month m, int count);
int daysInMonth(int year, int month);
// Calendar days from `a` to `b` inclusive, the convention the INSS uses for
// working-life periods: 04/03/1996-31/10/2001 is 2.068 days.
long daysInclusive(Date a, Date b);
Date addMonthsToDate(Date d, int count);
int compare(Date a, Date b);
std::string formatDate(Date d);
const char* monthAbbrev(int month);  // "ENE".."DIC"

// ---------------------------------------------------------------------------
// The recorded facts the engine computes against: a working life, the
// contribution bases on record, and the published CPI. Kept behind an
// swappable instance so the program can run on data loaded from disk instead
// of the tables compiled into report_data.cpp.

struct WorkPeriodRec {
    std::string employer;
    std::string detail;
    Date from{}, to{};
    int days = 0;       // as the INSS prints them: net of part-time and overlaps
    bool estimated;     // a stretch the report projected rather than recorded
};

struct Dataset {
    std::vector<WorkPeriodRec> workPeriods;
    std::vector<std::pair<Month, double>> historicBases;  // recorded BI, oldest first
    std::vector<std::pair<Month, double>> priceIndex;     // CPI, normalised to its last entry
    int overlappingDays = 0;
    Month handover{};  // first month the scenario owns rather than the record
};

const Dataset& activeDataset();
void setActiveDataset(Dataset d);

// ---------------------------------------------------------------------------
// Scenario: what the holder does between today and the retirement date.

enum class PeriodKind {
    Contributing,  // alta: contributes at `base`, days count towards the career
    Unemployment,  // contributory unemployment: days still count, base is given
    Gap,           // no obligation to contribute: days do not count, and months
                   // inside the regulatory-base window get filled in with the
                   // minimum base (integracion de lagunas, art. 209.1.b LGSS)
    Convenio,      // convenio especial: you stop working and pay the contribution
                   // yourself. A situacion asimilada al alta, so the days count
                   // for carency, for the percentage and for the 38-years-and-6-
                   // months test exactly as employment would.
};

struct ScenarioPeriod {
    Date from{}, to{};
    PeriodKind kind = PeriodKind::Contributing;
    double baseAtStart = 0.0;        // EUR/month during the starting year
    double annualGrowthPct = 2.0;    // stepped every January
    bool followMaximumBase = false;  // instead, track the base maxima each year
    std::string label;
};

struct Params {
    Date birth{};
    Date retirement{};

    // Projections. The report used 2,00% for both the CPI and future bases.
    double inflationPct = 2.0;

    // The maximum pension rises with the CPI plus an extra 0,115 pp a year
    // until 2050 (RDL 2/2023); the maximum contribution base rises with the CPI
    // plus an extra 1,2 pp a year over the same stretch. The gap between those
    // two is why contributing more does not always buy more pension.
    double maxPensionExtraPct = 0.115;
    double maximumBaseExtraPct = 1.2;

    int anchorYear = 2026;
    double maxPensionAnchor = 3359.60;   // EUR/month, 14 payments, 2026
    double maximumBaseAnchor = 5101.20;  // base maxima de cotizacion, 2026
    // Minimum contribution base of the general regime: what a month with no
    // contributions is filled in with, and the same figure the convenio floor
    // is built on.
    double minimumBaseAnchor = 1424.40;

    // The reports between them record every base from Feb-2007 on. Only a
    // retirement early enough to drag the window back before that needs an
    // estimate, and those months are taken as this share of the maximum
    // contribution base really in force that year. 100% is the right default:
    // where the second report let us check it, for 2007, 2008 and 2009, that
    // guess matched the recorded base to the cent.
    double unknownEarlyBasePct = 100.0;

    // Convenio especial (Orden TAS/2865/2003). The monthly cost is the chosen
    // base times the common-contingency rate times the 0,94 coefficient, plus
    // the MEI surcharge on the same base.
    double convenioRatePct = 28.30;
    double convenioCoefficient = 0.94;
    // The floor you may pick. Art. 6.2.1 of the order lets you choose anything
    // between letter c), a minimum, and the higher of letter a) -- the maximum
    // base of your contribution group, open to anyone who paid it for 24 of the
    // last 60 months -- and letter b), the average of your last 12 bases. This
    // career clears the condition on a), so the whole range is open to it.
    //
    // The figure below is the Regimen General minimum base for 2026. Letter c)
    // as written now points at the RETA general table instead (950,98 EUR in
    // 2026), but the INSS simulator offers 1.481,95 EUR for a convenio starting
    // in 2028, which is exactly 1.424,40 projected at 2% a year. The
    // administration's own arithmetic is what the engine follows.
    double convenioFloorBase = 1424.40;  // EUR/month, 2026, Regimen General

    bool voluntaryEarlyRetirement = true;  // involuntary uses a milder scale
    bool applyDualBaseReguladora = true;   // the art. 209 + DT 40a dual rule
    bool integrateGaps = true;
    // Cancel the convenio as soon as the career no longer needs it.
    bool trimConvenioWhenPossible = true;
    bool clampToMaximumBase = true;
};

// ---------------------------------------------------------------------------
// Results.

enum class MonthSource {
    None,       // outside the window, or no contribution and no integration
    Report,     // a base recorded in the INSS report
    Scenario,   // produced by one of the scenario periods
    Integrated, // a gap filled in with the minimum base
    Estimated,  // before the report's earliest base: a guess
};

struct MonthDetail {
    Month ym{};
    double bi = 0.0;  // base introducida (nominal)
    double ia = 1.0;  // indice de actualizacion
    double ba = 0.0;  // base actualizada
    MonthSource source = MonthSource::None;
    bool inWindowA = false;   // inside the 300-month window
    bool inWindowB = false;   // inside the longer dual-rule window
    bool chosenInB = false;   // among the highest bases the dual rule keeps
    bool nominal = false;     // inside the 24 months that are not updated
    bool clamped = false;     // base trimmed to that year's maximum
};

struct BaseReguladora {
    bool applicable = false;
    int windowMonths = 0;  // months looked at
    int basesTaken = 0;    // how many of them are summed
    double divisor = 0.0;
    double sum = 0.0;
    double value = 0.0;
    int monthsWithoutData = 0;
};

struct Result {
    // Career
    long daysContributed = 0;
    long daysLast15Years = 0;
    double yearsComputable = 0.0;
    int percentMonths = 0;  // months counted by the percentage scale

    // Regulatory base: the two competing computations and the winner
    BaseReguladora optionA{};  // last 300 bases / 350
    BaseReguladora optionB{};  // highest N bases of a longer window / divisor
    bool optionBWins = false;
    double baseReguladora = 0.0;

    // Percentages
    double percentByYears = 0.0;
    Date ordinaryRetirement{};
    int monthsAnticipated = 0;  // > 0 early, < 0 delayed
    double reductionPct = 0.0;  // the art. 208 coefficient, as a percentage
    int yearsDelayed = 0;
    double delayPct = 0.0;
    double percentTotal = 0.0;

    // Amounts, all EUR/month over 14 payments
    double pensionInitial = 0.0;  // base reguladora x percentage by years
    double calcInitial = 0.0;     // base reguladora x total percentage
    double maxPension = 0.0;      // the cap for the retirement year
    double effectiveCap = 0.0;    // the cap actually applied
    double excess = 0.0;          // trimmed by the cap
    double delaySupplement = 0.0; // the part of a delay bonus the cap displaces
    double pension = 0.0;         // importe pension total
    double pensionAnnual = 0.0;   // x14
    double pensionInTodayEuros = 0.0;

    // Context
    double maximumBase = 0.0;  // base maxima the retirement year
    bool capBinding = false;

    // Convenio especial, when the scenario contains one
    int convenioMonths = 0;
    double convenioTotalCost = 0.0;
    double convenioFirstMonthlyCost = 0.0;
    bool eligible = true;
    bool anticipationTooLong = false;   // beyond the 24 months art. 208 allows
    bool earlyRetirementUnqualified = false;  // short of the 35 years art. 208 asks
    std::vector<std::string> notes;
    std::vector<MonthDetail> months;  // the whole grid, newest last

    const MonthDetail* find(Month ym) const;
};

// The dual regulatory-base rule of DT 40a LGSS, by year of retirement.
struct DualRule {
    bool applicable = false;
    int windowMonths = 0;
    int basesTaken = 0;
    double divisor = 0.0;
};
DualRule dualRuleFor(int retirementYear);

// Projections for a given year, from the anchors in `Params`.
double maxPensionFor(const Params& p, int year);
double maximumBaseFor(const Params& p, int year);
double minimumBaseFor(const Params& p, int year);

// The maximum contribution base really in force in a past year, or 0 if that
// year predates the table. Used for the months the report does not list.
double historicalMaximumBase(int year);

// The ordinary retirement age under the Ley 27/2011 final scale: 67, or 65 for
// a career of at least 38 years and 6 months.
Date ordinaryRetirementDate(Date birth, long daysContributed);

// The art. 208 reduction for `months` of anticipation (1..24) and a career of
// `days`, as a percentage.
double reductionCoefficient(int months, long days, bool voluntary);

// The MEI surcharge (Mecanismo de Equidad Intergeneracional): 0,6% in 2023,
// a tenth of a point more each year, settling at 1,2% from 2029.
double meiRatePct(int year);

// What a convenio especial at `base` costs per month in a given year.
double convenioMonthlyCost(const Params& p, double base, int year);

Result compute(const Params& p, const std::vector<ScenarioPeriod>& periods);

// ---------------------------------------------------------------------------
// Stopping work early, and what it would take not to lose anything.
//
// The question these answer: the pension here is pinned to the maximum, so the
// contributions of the last years are partly wasted. How soon could you stop,
// and how small a convenio especial would still leave the pension untouched?

// `scenario` truncated at `stopWork`, followed by either a convenio especial at
// `convenioBase` running until `convenioUntil`, or, when `useConvenio` is false,
// nothing at all. Months after the convenio ends are left empty, so they fall to
// the gap integration like any other stretch without contributions.
std::vector<ScenarioPeriod> scenarioStoppingAt(const std::vector<ScenarioPeriod>& scenario,
                                               Date stopWork, Date retirement,
                                               bool useConvenio, double convenioBase,
                                               double convenioGrowthPct,
                                               Date convenioUntil = Date{});

struct ConvenioPlan {
    Date stopWork{};
    bool feasible = false;       // the target is reachable at all
    bool floorIsEnough = false;  // the cheapest allowed base already does it
    bool convenioUnnecessary = false;  // stopping outright loses nothing either

    double minimumBase = 0.0;  // smallest base that holds the pension at target
    double firstMonthlyCost = 0.0;
    double totalCost = 0.0;
    int months = 0;          // monthly bills issued, the first and last partial
    int durationMonths = 0;  // how long it actually runs

    // A convenio need not run to the retirement date. Once the career clears
    // the 38-years-and-6-months mark, extra days buy nothing, and the months
    // left over are integrated with the minimum base anyway -- the very figure
    // the cheapest convenio pays for. So it can be cancelled early, free.
    Date convenioUntil{};
    double costIfRunToRetirement = 0.0;
    double saving = 0.0;  // what cancelling early saves

    double pensionWithConvenio = 0.0;
    double pensionWithoutConvenio = 0.0;
    long daysWithConvenio = 0;
    long daysWithoutConvenio = 0;
    Date ordinaryWithConvenio{}, ordinaryWithoutConvenio{};
    bool retiresAt65WithConvenio = false;
    bool retiresAt65WithoutConvenio = false;
};

// Searches for the smallest convenio base that keeps the pension at or above
// `targetPension`, for someone who stops working on `stopWork`.
ConvenioPlan planConvenio(const Params& p, const std::vector<ScenarioPeriod>& scenario,
                          Date stopWork, double targetPension, double convenioGrowthPct);

}  // namespace pension
