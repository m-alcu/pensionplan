#include "pension.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace pension {
namespace {

constexpr int kWindowA = 300;        // months in the classic regulatory base
constexpr double kDivisorA = 350.0;  // 300 months spread over 14 payments
constexpr int kNominalMonths = 24;   // the most recent bases are not updated
constexpr long kDaysPerYear = 365;
constexpr double kDaysPerMonth = 30.416666666666668;  // 365 / 12
constexpr int kCarencyDays = 15 * kDaysPerYear;       // 15 years, generic
constexpr int kSpecificCarencyDays = 2 * kDaysPerYear;

// Ley 27/2011 final scale (art. 210.1 LGSS, in force from 2027): 50% at 15
// years, then 0,19% a month for 248 months and 0,18% for the 16 after that,
// which lands exactly on 100% at 37 years.
constexpr int kBasePercentMonths = 180;
constexpr double kBasePercent = 50.0;
constexpr int kTier1Months = 248;
constexpr double kTier1Rate = 0.19;
constexpr int kTier2Months = 16;
constexpr double kTier2Rate = 0.18;

// art. 208.2 LGSS: the reduction for voluntary early retirement, by months of
// anticipation (row) and length of career (column: under 38y6m, under 41y6m,
// under 44y6m, 44y6m or more). Rows run from 1 to 24 months.
const double kVoluntaryReduction[24][4] = {
    {3.26, 3.11, 2.96, 2.81},  {3.38, 3.23, 3.08, 2.92},
    {3.52, 3.36, 3.20, 3.04},  {3.67, 3.50, 3.33, 3.17},
    {3.83, 3.65, 3.48, 3.30},  {4.00, 3.82, 3.64, 3.45},
    {4.19, 4.00, 3.81, 3.62},  {4.40, 4.20, 4.00, 3.80},
    {4.63, 4.42, 4.21, 4.00},  {4.89, 4.67, 4.44, 4.22},
    {5.18, 4.94, 4.71, 4.47},  {5.50, 5.25, 5.00, 4.75},
    {5.87, 5.60, 5.33, 5.07},  {6.29, 6.00, 5.71, 5.43},
    {6.77, 6.46, 6.15, 5.85},  {7.33, 7.00, 6.67, 6.33},
    {8.00, 7.64, 7.27, 6.91},  {8.80, 8.40, 8.00, 7.60},
    {9.78, 9.33, 8.89, 8.40},  {11.00, 10.50, 10.00, 9.20},
    {12.57, 12.00, 11.43, 10.00}, {14.67, 14.00, 13.33, 11.00},
    {17.60, 16.50, 15.00, 12.00}, {21.00, 19.00, 17.00, 13.00},
};

// The maximum contribution base of the general regime as it really was, EUR a
// month. Every year that overlaps the report's own grid matches the bases it
// prints, which is what makes this a sound basis for filling in the months it
// leaves out.
struct MaximumBaseRow {
    int year;
    double base;
};
const MaximumBaseRow kHistoricalMaximumBase[] = {
    {2005, 2813.40}, {2006, 2897.70}, {2007, 2996.10}, {2008, 3074.10},
    {2009, 3166.20}, {2010, 3198.00}, {2011, 3230.10}, {2012, 3262.50},
    {2013, 3425.70}, {2014, 3597.00}, {2015, 3606.00}, {2016, 3642.00},
    {2017, 3751.20}, {2018, 3803.70}, {2019, 4070.10}, {2020, 4070.10},
    {2021, 4070.10}, {2022, 4139.40}, {2023, 4495.50}, {2024, 4720.50},
    {2025, 4909.50}, {2026, 5101.20},
};

int careerBracket(long days) {
    double years = (double)days / kDaysPerYear;
    if (years < 38.5) return 0;
    if (years < 41.5) return 1;
    if (years < 44.5) return 2;
    return 3;
}

// The active dataset, plus the lookup maps derived from it. Rebuilt only when
// the dataset is swapped: compute() runs hundreds of times over whenever the
// convenio planner bisects, so neither of these may be rebuilt per call.
struct ActiveData {
    Dataset set;
    std::map<int, double> bases;
    std::map<int, double> cpi;
    int cpiFirst = 0, cpiLast = 0;
    double cpiFirstValue = 1.0, cpiLastValue = 1.0;

    void rebuild() {
        bases.clear();
        cpi.clear();
        for (const auto& [m, v] : set.historicBases) bases[monthOrdinal(m)] = v;
        for (const auto& [m, v] : set.priceIndex) cpi[monthOrdinal(m)] = v;
        if (!cpi.empty()) {
            cpiFirst = cpi.begin()->first;
            cpiFirstValue = cpi.begin()->second;
            cpiLast = cpi.rbegin()->first;
            cpiLastValue = cpi.rbegin()->second;
        }
    }
};

ActiveData& active() {
    // Empty until setActiveDataset() is called with data read from disk.
    static ActiveData a;
    return a;
}

// Price index: real published CPI through the last month on record, then
// projected forward (and backward) at the scenario rate.
struct PriceIndex {
    double monthlyFactor;
    explicit PriceIndex(double inflationPct)
        : monthlyFactor(std::pow(1.0 + inflationPct / 100.0, 1.0 / 12.0)) {}

    double at(Month m) const {
        const ActiveData& a = active();
        int o = monthOrdinal(m);
        auto it = a.cpi.find(o);
        if (it != a.cpi.end()) return it->second;
        if (o > a.cpiLast) return a.cpiLastValue * std::pow(monthlyFactor, o - a.cpiLast);
        return a.cpiFirstValue * std::pow(monthlyFactor, o - a.cpiFirst);
    }
};

// A contribution base actually on record, or -1 for months the data does not
// reach back to.
double historicBase(Month m) {
    const auto& bases = active().bases;
    auto it = bases.find(monthOrdinal(m));
    return it == bases.end() ? -1.0 : it->second;
}

// Where the recorded history stops and the scenario takes over.
Month handoverMonth() { return active().set.handover; }

bool periodCoversMonth(const ScenarioPeriod& p, Month m) {
    Date first{m.year, m.month, 1};
    Date last{m.year, m.month, daysInMonth(m.year, m.month)};
    return compare(p.from, last) <= 0 && compare(p.to, first) >= 0;
}

// How many days of `m` the period actually covers.
int periodDaysInMonth(const ScenarioPeriod& p, Month m) {
    Date first{m.year, m.month, 1};
    Date last{m.year, m.month, daysInMonth(m.year, m.month)};
    Date a = compare(p.from, first) > 0 ? p.from : first;
    Date b = compare(p.to, last) < 0 ? p.to : last;
    return compare(a, b) <= 0 ? (int)daysInclusive(a, b) : 0;
}

double periodBaseFor(const ScenarioPeriod& p, Month m, const Params& params) {
    if (p.followMaximumBase) return maximumBaseFor(params, m.year);
    int steps = m.year - p.from.year;
    return p.baseAtStart * std::pow(1.0 + p.annualGrowthPct / 100.0, steps);
}

void addNote(Result& r, const std::string& s) {
    if (std::find(r.notes.begin(), r.notes.end(), s) == r.notes.end()) r.notes.push_back(s);
}

// Spanish number formatting, so the notes read like the report they came from:
// thousands separated by '.', decimals by ','.
std::string fmtEur(double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.2f", std::fabs(v));
    std::string digits(buf);
    size_t dot = digits.find('.');
    std::string whole = digits.substr(0, dot), frac = digits.substr(dot + 1);
    std::string out;
    for (size_t i = 0; i < whole.size(); ++i) {
        if (i && (whole.size() - i) % 3 == 0) out += '.';
        out += whole[i];
    }
    return (v < 0 ? "-" : "") + out + "," + frac;
}

}  // namespace

// ---------------------------------------------------------------------------
// Calendar

int monthOrdinal(Month m) { return m.year * 12 + (m.month - 1); }

Month monthFromOrdinal(int ordinal) {
    int y = ordinal >= 0 ? ordinal / 12 : -((-ordinal + 11) / 12);
    return {y, ordinal - y * 12 + 1};
}

Month monthOf(Date d) { return {d.year, d.month}; }

Month addMonths(Month m, int count) { return monthFromOrdinal(monthOrdinal(m) + count); }

int daysInMonth(int year, int month) {
    static const int len[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2) {
        bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
        return leap ? 29 : 28;
    }
    return len[month - 1];
}

namespace {
// Days since an arbitrary epoch, for differencing only (Howard Hinnant's
// days_from_civil).
long civilDays(int y, int m, int d) {
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153u * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097L + (long)doe;
}
}  // namespace

long daysInclusive(Date a, Date b) {
    long n = civilDays(b.year, b.month, b.day) - civilDays(a.year, a.month, a.day) + 1;
    return n > 0 ? n : 0;
}

Date addMonthsToDate(Date d, int count) {
    Month m = addMonths({d.year, d.month}, count);
    int day = std::min(d.day, daysInMonth(m.year, m.month));
    return {m.year, m.month, day};
}

int compare(Date a, Date b) {
    if (a.year != b.year) return a.year < b.year ? -1 : 1;
    if (a.month != b.month) return a.month < b.month ? -1 : 1;
    if (a.day != b.day) return a.day < b.day ? -1 : 1;
    return 0;
}

std::string formatDate(Date d) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%02d/%02d/%04d", d.day, d.month, d.year);
    return buf;
}

const char* monthAbbrev(int month) {
    static const char* names[] = {"ENE", "FEB", "MAR", "ABR", "MAY", "JUN",
                                  "JUL", "AGO", "SEP", "OCT", "NOV", "DIC"};
    return (month >= 1 && month <= 12) ? names[month - 1] : "???";
}

const MonthDetail* Result::find(Month ym) const {
    for (const auto& m : months)
        if (m.ym.year == ym.year && m.ym.month == ym.month) return &m;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Projections and legal tables

DualRule dualRuleFor(int retirementYear) {
    DualRule r;
    if (retirementYear < 2026) return r;  // single 300/350 computation
    r.applicable = true;
    int step = std::min(retirementYear, 2037) - 2026;
    r.windowMonths = 304 + 4 * step;
    r.basesTaken = 302 + 2 * step;
    r.divisor = r.basesTaken * 14.0 / 12.0;
    return r;
}

double maxPensionFor(const Params& p, int year) {
    // CPI plus an extra 0,115 pp a year, the rise RDL 2/2023 set out to 2050.
    double rate = 1.0 + (p.inflationPct + p.maxPensionExtraPct) / 100.0;
    int capped = std::min(year, 2050);
    double v = p.maxPensionAnchor * std::pow(rate, capped - p.anchorYear);
    if (year > 2050) v *= std::pow(1.0 + p.inflationPct / 100.0, year - 2050);
    return v;
}

double maximumBaseFor(const Params& p, int year) {
    double rate = 1.0 + (p.inflationPct + p.maximumBaseExtraPct) / 100.0;
    int capped = std::min(year, 2050);
    double v = p.maximumBaseAnchor * std::pow(rate, capped - p.anchorYear);
    if (year > 2050) v *= std::pow(1.0 + p.inflationPct / 100.0, year - 2050);
    return v;
}

double historicalMaximumBase(int year) {
    const auto* first = kHistoricalMaximumBase;
    const auto* last = kHistoricalMaximumBase +
                       sizeof kHistoricalMaximumBase / sizeof *kHistoricalMaximumBase;
    double best = 0.0;
    for (const auto* it = first; it != last; ++it)
        if (it->year == year) best = it->base;
    if (best == 0.0 && year < first->year) best = first->base;
    if (best == 0.0 && year > (last - 1)->year) best = (last - 1)->base;
    return best;
}

double minimumBaseFor(const Params& p, int year) {
    return p.minimumBaseAnchor * std::pow(1.0 + p.inflationPct / 100.0, year - p.anchorYear);
}

Date ordinaryRetirementDate(Date birth, long daysContributed) {
    // Final Ley 27/2011 scale: 65 with a career of 38 years and 6 months or
    // more, otherwise 67. Anyone reaching 65 from 2027 on is on this scale.
    bool longCareer = daysContributed >= (long)std::llround(38.5 * kDaysPerYear);
    return addMonthsToDate(birth, (longCareer ? 65 : 67) * 12);
}

double reductionCoefficient(int months, long days, bool voluntary) {
    if (months <= 0) return 0.0;
    int row = std::min(months, 24) - 1;
    double pct = kVoluntaryReduction[row][careerBracket(days)];
    // Involuntary early retirement (art. 207) is milder; the engine models it
    // as the voluntary scale less a quarter, which is the usual rule of thumb
    // rather than a statutory table.
    return voluntary ? pct : pct * 0.75;
}

// ---------------------------------------------------------------------------
// The computation

Result compute(const Params& p, const std::vector<ScenarioPeriod>& periods) {
    Result r;
    const PriceIndex cpi(p.inflationPct);
    const Month handover = handoverMonth();
    const Month hechoCausante = monthOf(p.retirement);

    // --- career length -----------------------------------------------------
    // Recorded periods up to the handover come straight from the report, whose
    // printed day counts already net out part-time coefficients. The report's
    // own estimated stretch is dropped: the scenario stands in for it.
    const Dataset& data = active().set;
    long recordedDays = 0;
    for (const auto& w : data.workPeriods)
        if (!w.estimated) recordedDays += w.days;
    recordedDays -= data.overlappingDays;

    long scenarioDays = 0;
    for (const auto& sp : periods) {
        if (sp.kind == PeriodKind::Gap) continue;  // convenio days do count
        Date to = compare(sp.to, p.retirement) > 0 ? p.retirement : sp.to;
        scenarioDays += daysInclusive(sp.from, to);
    }
    r.daysContributed = recordedDays + scenarioDays;
    r.yearsComputable = (double)r.daysContributed / kDaysPerYear;

    // Specific carency: days inside the 15 years before the hecho causante.
    Date windowStart = addMonthsToDate(p.retirement, -15 * 12);
    Date windowEnd = p.retirement;
    {
        long days = 0;
        for (const auto& w : data.workPeriods) {
            if (w.estimated) continue;
            Date a = w.from, b = w.to;
            if (compare(a, windowStart) < 0) a = windowStart;
            if (compare(b, windowEnd) > 0) b = windowEnd;
            if (compare(a, b) <= 0) days += daysInclusive(a, b);
        }
        for (const auto& sp : periods) {
            if (sp.kind == PeriodKind::Gap) continue;
            Date a = sp.from, b = sp.to;
            if (compare(a, windowStart) < 0) a = windowStart;
            if (compare(b, windowEnd) > 0) b = windowEnd;
            if (compare(a, b) <= 0) days += daysInclusive(a, b);
        }
        // The report counts the 15 years as ending the day before the hecho
        // causante, which is one day short of the inclusive span.
        r.daysLast15Years = std::min(days - 1, (long)(15 * 365.25));
    }

    if (r.daysContributed < kCarencyDays) {
        r.eligible = false;
        addNote(r, "No llega a la carencia genérica de 15 años (5.475 días): sin derecho a "
                   "pensión contributiva de jubilación.");
    }
    if (r.daysLast15Years < kSpecificCarencyDays) {
        r.eligible = false;
        addNote(r, "No llega a la carencia específica de 2 años cotizados dentro de los "
                   "últimos 15.");
    }

    // --- the month grid ----------------------------------------------------
    // Wide enough to cover both regulatory-base windows plus the surrounding
    // context the UI shows.
    DualRule dual = dualRuleFor(p.retirement.year);
    int lastOrdinal = monthOrdinal(hechoCausante) - 2;  // the month before the mes previo
    int spanA = kWindowA;
    int spanB = (p.applyDualBaseReguladora && dual.applicable) ? dual.windowMonths : 0;
    int firstOrdinal = lastOrdinal - std::max(spanA, spanB) + 1;
    int referenceOrdinal = lastOrdinal - kNominalMonths;  // last updatable month

    const Month reference = monthFromOrdinal(referenceOrdinal);
    const double referenceIndex = cpi.at(reference);

    // Pass one: nominal base per month and where it came from.
    for (int o = firstOrdinal; o <= lastOrdinal; ++o) {
        Month ym = monthFromOrdinal(o);
        MonthDetail d;
        d.ym = ym;
        d.inWindowA = o > lastOrdinal - spanA;
        d.inWindowB = spanB > 0 && o > lastOrdinal - spanB;
        d.nominal = o > referenceOrdinal;

        if (o < monthOrdinal(handover)) {
            if (double recorded = historicBase(ym); recorded >= 0) {
                d.bi = recorded;
                d.source = MonthSource::Report;
            } else {
                d.bi = historicalMaximumBase(ym.year) * p.unknownEarlyBasePct / 100.0;
                d.source = MonthSource::Estimated;
            }
        } else {
            std::vector<const ScenarioPeriod*> covering;
            bool anyConvenio = false;
            for (const auto& sp : periods) {
                if (sp.kind == PeriodKind::Gap) continue;
                if (!periodCoversMonth(sp, ym)) continue;
                covering.push_back(&sp);
                if (sp.kind == PeriodKind::Convenio) anyConvenio = true;
            }
            if (!covering.empty()) {
                // A whole month of employment contributes its monthly base as
                // it stands. Only the month where a convenio especial starts or
                // ends is split, and there each leg is prorated over 30 days --
                // art. 7.2.c of the order for the convenio side, and the INSS
                // simulator uses the same divisor for the employment side.
                if (covering.size() == 1 || !anyConvenio) {
                    d.bi = periodBaseFor(*covering.front(), ym, p);
                } else {
                    d.bi = 0.0;
                    for (const auto* sp : covering)
                        d.bi += periodBaseFor(*sp, ym, p) * periodDaysInMonth(*sp, ym) / 30.0;
                }
                d.source = MonthSource::Scenario;
                if (p.clampToMaximumBase) {
                    double cap = maximumBaseFor(p, ym.year);
                    if (d.bi > cap) {
                        d.bi = cap;
                        d.clamped = true;
                    }
                }
            }
        }
        r.months.push_back(d);
    }

    // Pass two: integracion de lagunas. Months inside the regulatory-base
    // window with no contribution are filled in with the minimum base - the
    // 48 most recent at 100%, the rest at 50% (art. 209.1.b LGSS).
    if (p.integrateGaps) {
        int filled = 0;
        for (auto it = r.months.rbegin(); it != r.months.rend(); ++it) {
            if (it->source != MonthSource::None) continue;
            if (!it->inWindowA && !it->inWindowB) continue;
            double minimum = minimumBaseFor(p, it->ym.year);
            it->bi = (filled < 48) ? minimum : minimum * 0.5;
            it->source = MonthSource::Integrated;
            ++filled;
        }
        if (filled > 0)
            addNote(r, "Integración de lagunas: " + std::to_string(filled) +
                           " meses sin cotización dentro del periodo de cómputo, rellenados "
                           "con la base mínima (los 48 más recientes al 100 %, el resto al "
                           "50 %).");
    }

    // Pass three: actualisation. The 24 bases immediately before the mes previo
    // count at face value; the rest are carried forward by the CPI to the month
    // just before that nominal stretch starts (art. 209.1 LGSS).
    for (auto& d : r.months) {
        d.ia = d.nominal ? 1.0 : referenceIndex / cpi.at(d.ym);
        // The report rounds every actualised base to the cent before adding them
        // up, so round here too rather than drifting a few euros over 300 months.
        d.ba = std::round(d.bi * d.ia * 100.0) / 100.0;
    }

    // --- what the convenio especial costs ----------------------------------
    // Counted over its whole run, not just the months the regulatory base
    // happens to look at: it is money actually paid out.
    for (const auto& sp : periods) {
        if (sp.kind != PeriodKind::Convenio) continue;
        Date to = compare(sp.to, p.retirement) > 0 ? p.retirement : sp.to;
        int first = monthOrdinal(monthOf(sp.from)), last = monthOrdinal(monthOf(to));
        for (int o = first; o <= last; ++o) {
            Month ym = monthFromOrdinal(o);
            double base = periodBaseFor(sp, ym, p);
            if (p.clampToMaximumBase) base = std::min(base, maximumBaseFor(p, ym.year));
            double full = convenioMonthlyCost(p, base, ym.year);
            // Art. 7.2.c: a part month is the monthly quota over 30, times the
            // days the convenio was in force.
            int days = periodDaysInMonth(sp, ym);
            int whole = daysInMonth(ym.year, ym.month);
            double cost = days >= whole ? full : full / 30.0 * days;
            if (r.convenioMonths == 0) r.convenioFirstMonthlyCost = full;
            ++r.convenioMonths;
            r.convenioTotalCost += cost;
        }
    }

    // --- regulatory base ---------------------------------------------------
    r.optionA.applicable = true;
    r.optionA.windowMonths = kWindowA;
    r.optionA.basesTaken = kWindowA;
    r.optionA.divisor = kDivisorA;
    for (const auto& d : r.months) {
        if (!d.inWindowA) continue;
        r.optionA.sum += d.ba;
        if (d.source == MonthSource::None) ++r.optionA.monthsWithoutData;
        if (d.source == MonthSource::Estimated) ++r.optionA.monthsWithoutData;
    }
    r.optionA.value = r.optionA.sum / r.optionA.divisor;

    if (p.applyDualBaseReguladora && dual.applicable) {
        r.optionB.applicable = true;
        r.optionB.windowMonths = dual.windowMonths;
        r.optionB.basesTaken = dual.basesTaken;
        r.optionB.divisor = dual.divisor;

        std::vector<MonthDetail*> inB;
        for (auto& d : r.months)
            if (d.inWindowB) inB.push_back(&d);
        std::sort(inB.begin(), inB.end(),
                  [](const MonthDetail* a, const MonthDetail* b) { return a->ba > b->ba; });
        int take = std::min((int)inB.size(), dual.basesTaken);
        for (int i = 0; i < take; ++i) {
            inB[i]->chosenInB = true;
            r.optionB.sum += inB[i]->ba;
            if (inB[i]->source == MonthSource::None ||
                inB[i]->source == MonthSource::Estimated)
                ++r.optionB.monthsWithoutData;
        }
        r.optionB.value = r.optionB.sum / r.optionB.divisor;
    }

    r.optionBWins = r.optionB.applicable && r.optionB.value > r.optionA.value;
    r.baseReguladora = r.optionBWins ? r.optionB.value : r.optionA.value;
    if (!r.optionBWins)
        for (auto& d : r.months) d.chosenInB = false;

    {
        int inA = 0, inB = 0;
        for (const auto& d : r.months) {
            if (d.source != MonthSource::Estimated) continue;
            if (d.inWindowA) ++inA;
            if (d.inWindowB) ++inB;
        }
        const std::string how =
            " Se han estimado al " + fmtEur(p.unknownEarlyBasePct) +
            " % de la base máxima de cotización de cada año, que es lo que el titular cotizó "
            "en todos los años que el informe sí detalla.";
        if (inA > 0) {
            addNote(r, "El periodo de cómputo alcanza " + std::to_string(inA) +
                           " meses anteriores a febrero de 2007, que los informes no detallan, así "
                           "que esta base reguladora es aproximada." + how);
        } else if (inB > 0 && r.optionBWins) {
            addNote(r, "La opción B llega a " + std::to_string(inB) +
                           " meses anteriores a febrero de 2007, que los informes no detallan, así "
                           "que esta base reguladora es aproximada." + how);
        } else if (inB > 0) {
            addNote(r, "La ventana de la opción B llega a " + std::to_string(inB) +
                           " meses anteriores a febrero de 2007 que los informes no detallan. No "
                           "afectan al resultado, porque gana la opción A, pero sí a la "
                           "comparación entre ambas." + how);
        }
    }

    // --- percentage --------------------------------------------------------
    r.percentMonths = (int)std::floor((double)r.daysContributed / kDaysPerMonth);
    int extra = r.percentMonths - kBasePercentMonths;
    if (extra <= 0) {
        r.percentByYears = r.daysContributed >= kCarencyDays ? kBasePercent : 0.0;
    } else {
        double pct = kBasePercent;
        pct += std::min(extra, kTier1Months) * kTier1Rate;
        if (extra > kTier1Months)
            pct += std::min(extra - kTier1Months, kTier2Months) * kTier2Rate;
        r.percentByYears = std::min(pct, 100.0);
    }

    r.ordinaryRetirement = ordinaryRetirementDate(p.birth, r.daysContributed);
    int monthsOff = monthOrdinal(monthOf(r.ordinaryRetirement)) - monthOrdinal(hechoCausante);
    // Retiring on a later day of the same month is neither early nor late.
    if (monthsOff == 0) monthsOff = 0;
    r.monthsAnticipated = monthsOff;

    if (r.monthsAnticipated > 0) {
        if (r.monthsAnticipated > 24) {
            r.anticipationTooLong = true;
            addNote(r, "Con " + std::to_string(r.monthsAnticipated) +
                           " meses de adelanto sobre la edad ordinaria (" +
                           formatDate(r.ordinaryRetirement) +
                           ") esta jubilación NO es legalmente posible: el máximo son 24 meses. "
                           "La cifra que se muestra aplica el coeficiente de 24 meses, sólo "
                           "como referencia.");
        }
        if (p.voluntaryEarlyRetirement && r.daysContributed < 35 * kDaysPerYear) {
            r.earlyRetirementUnqualified = true;
            addNote(r, "La jubilación anticipada voluntaria exige 35 años cotizados y aquí sólo "
                       "hay " + fmtEur((double)r.daysContributed / kDaysPerYear) +
                       ": esta jubilación no se puede causar en esta fecha.");
        }
        r.reductionPct = reductionCoefficient(r.monthsAnticipated, r.daysContributed,
                                              p.voluntaryEarlyRetirement);
        r.percentTotal = r.percentByYears * (1.0 - r.reductionPct / 100.0);
    } else if (r.monthsAnticipated < 0) {
        int delayMonths = -r.monthsAnticipated;
        r.yearsDelayed = delayMonths / 12;
        // 4% a complete year; from the second year on, a leftover stretch of
        // more than six months adds a further 2% (art. 210.2 LGSS).
        r.delayPct = 4.0 * r.yearsDelayed;
        if (r.yearsDelayed >= 2 && delayMonths % 12 > 6) r.delayPct += 2.0;
        r.percentTotal = r.percentByYears + r.delayPct;
    } else {
        r.percentTotal = r.percentByYears;
    }

    // --- amounts -----------------------------------------------------------
    r.pensionInitial = r.baseReguladora * r.percentByYears / 100.0;
    r.calcInitial = r.baseReguladora * r.percentTotal / 100.0;
    r.maxPension = maxPensionFor(p, p.retirement.year);
    r.maximumBase = maximumBaseFor(p, p.retirement.year);

    r.effectiveCap = r.maxPension;
    if (r.monthsAnticipated > 0 && p.voluntaryEarlyRetirement) {
        // art. 210.3 LGSS: for voluntary early retirement the reduction bites
        // on the cap itself. The phase-in ended in 2033, so from then on the
        // full coefficient applies to the maximum pension.
        double share = p.retirement.year >= 2033 ? 1.0 : 0.0;
        r.effectiveCap = r.maxPension * (1.0 - share * r.reductionPct / 100.0);
    }

    if (r.calcInitial > r.effectiveCap) {
        r.excess = r.calcInitial - r.effectiveCap;
        r.pension = r.effectiveCap;
        r.capBinding = true;
    } else {
        r.pension = r.calcInitial;
    }

    // A delay bonus the cap would swallow is paid separately instead, so that
    // delaying still pays for a career already at the ceiling. The engine
    // models it as the displaced amount, itself capped at the maximum
    // contribution base - a simplified reading of art. 210.2 LGSS.
    if (r.yearsDelayed > 0 && r.capBinding) {
        double uncappedByDelay = r.baseReguladora * r.delayPct / 100.0;
        double roomUsed = std::max(0.0, r.effectiveCap - r.pensionInitial);
        r.delaySupplement = std::min(std::max(0.0, uncappedByDelay - roomUsed), r.maximumBase);
        addNote(r, "La pensión ya toca el tope, así que el " + fmtEur(r.delayPct) +
                       " % por demora no cabe en la mensualidad. Se refleja como complemento "
                       "por demora (" + fmtEur(r.delaySupplement) +
                       " €/mes). Es una estimación: el art. 210.2 LGSS fija la fórmula exacta "
                       "y admite además el cobro a tanto alzado.");
    }

    if (!r.eligible) {
        r.pension = 0.0;
        r.delaySupplement = 0.0;
    }

    r.pension += r.delaySupplement;
    r.pensionAnnual = r.pension * 14.0;
    r.pensionInTodayEuros =
        r.pension / std::pow(1.0 + p.inflationPct / 100.0, p.retirement.year - p.anchorYear);

    if (r.capBinding)
        addNote(r, "El cálculo inicial (" + fmtEur(r.calcInitial) +
                       " €) supera el tope de " + fmtEur(r.effectiveCap) + " € del año " +
                       std::to_string(p.retirement.year) + ": se recortan " +
                       fmtEur(r.excess) +
                       " €/mes. Mientras el tope siga mordiendo, subir las bases futuras no "
                       "sube la pensión.");

    {
        int clamped = 0;
        for (const auto& d : r.months)
            if (d.clamped && (d.inWindowA || d.inWindowB)) ++clamped;
        if (clamped > 0)
            addNote(r, std::to_string(clamped) +
                           " meses del escenario superaban la base máxima de cotización de su "
                           "año y se han recortado a ella.");
    }

    return r;
}

// ---------------------------------------------------------------------------

double meiRatePct(int year) {
    if (year < 2023) return 0.0;
    return std::min(0.6 + 0.1 * (year - 2023), 1.2);
}

double convenioMonthlyCost(const Params& p, double base, int year) {
    double rate = p.convenioRatePct / 100.0 * p.convenioCoefficient + meiRatePct(year) / 100.0;
    return base * rate;
}

std::vector<ScenarioPeriod> scenarioStoppingAt(const std::vector<ScenarioPeriod>& scenario,
                                               Date stopWork, Date retirement,
                                               bool useConvenio, double convenioBase,
                                               double convenioGrowthPct, Date convenioUntil) {
    std::vector<ScenarioPeriod> out;
    for (const auto& sp : scenario) {
        if (compare(sp.from, stopWork) > 0) continue;  // starts after the handover
        ScenarioPeriod t = sp;
        if (compare(t.to, stopWork) > 0) t.to = stopWork;
        out.push_back(t);
    }
    Date next = stopWork;
    next.day += 1;
    if (next.day > daysInMonth(next.year, next.month)) {
        next.day = 1;
        Month m = addMonths({next.year, next.month}, 1);
        next.year = m.year;
        next.month = m.month;
    }
    if (compare(next, retirement) <= 0) {
        ScenarioPeriod tail;
        tail.from = next;
        tail.to = (useConvenio && convenioUntil.year != 0 &&
                   compare(convenioUntil, retirement) < 0)
                      ? convenioUntil
                      : retirement;
        if (compare(tail.to, tail.from) < 0) return out;
        tail.kind = useConvenio ? PeriodKind::Convenio : PeriodKind::Gap;
        tail.baseAtStart = convenioBase;
        tail.annualGrowthPct = convenioGrowthPct;
        tail.label = useConvenio ? "Convenio especial" : "Sin cotizar";
        out.push_back(tail);
    }
    return out;
}

ConvenioPlan planConvenio(const Params& p, const std::vector<ScenarioPeriod>& scenario,
                          Date stopWork, double targetPension, double convenioGrowthPct) {
    ConvenioPlan plan;
    plan.stopWork = stopWork;

    Date until = p.retirement;
    auto runUntil = [&](double base, Date end) {
        return compute(p, scenarioStoppingAt(scenario, stopWork, p.retirement, true, base,
                                             convenioGrowthPct, end));
    };
    auto runAt = [&](double base) { return runUntil(base, until); };

    // Stopping outright, with nothing in its place.
    Result bare =
        compute(p, scenarioStoppingAt(scenario, stopWork, p.retirement, false, 0.0, 0.0));
    plan.pensionWithoutConvenio = bare.pension;
    plan.daysWithoutConvenio = bare.daysContributed;
    plan.ordinaryWithoutConvenio = bare.ordinaryRetirement;
    plan.retiresAt65WithoutConvenio =
        !bare.anticipationTooLong && !bare.earlyRetirementUnqualified && bare.eligible &&
        bare.monthsAnticipated <= 0;

    const double eps = 0.005;  // half a cent: the pension is quoted to the cent
    if (bare.pension >= targetPension - eps && plan.retiresAt65WithoutConvenio) {
        plan.convenioUnnecessary = true;
        plan.feasible = true;
    }

    double floor = p.convenioFloorBase * std::pow(1.0 + convenioGrowthPct / 100.0,
                                                  stopWork.year - p.anchorYear);
    double ceiling = maximumBaseFor(p, stopWork.year);

    // How long the convenio has to run. Paying past the point where the career
    // clears 38 years and 6 months buys nothing: the pension is already pinned
    // to the cap, and the remaining months are filled in with the minimum base
    // free of charge. Walk the end date back month by month while the target
    // still holds at the dearest base available.
    if (p.trimConvenioWhenPossible) {
        for (int back = 1; back <= 240; ++back) {
            Date earlier = addMonthsToDate(p.retirement, -back);
            if (compare(earlier, stopWork) <= 0) break;
            if (runUntil(ceiling, earlier).pension < targetPension - 0.005) break;
            until = earlier;
        }
    }

    Result atFloor = runAt(floor);
    Result atCeiling = runAt(ceiling);

    if (atCeiling.pension >= targetPension - eps) {
        plan.feasible = true;
        if (atFloor.pension >= targetPension - eps) {
            plan.floorIsEnough = true;
            plan.minimumBase = floor;
        } else {
            // The pension rises with the base and then flattens against the
            // cap, so it is monotone: bisect for the smallest base that reaches
            // the target.
            double lo = floor, hi = ceiling;
            for (int i = 0; i < 28; ++i) {
                double mid = 0.5 * (lo + hi);
                if (runAt(mid).pension >= targetPension - eps)
                    hi = mid;
                else
                    lo = mid;
            }
            // Round up to the cent, then make sure the rounded figure really
            // clears the target rather than landing a cent under it.
            plan.minimumBase = std::ceil(hi * 100.0) / 100.0;
            for (int i = 0; i < 8 && runAt(plan.minimumBase).pension < targetPension - eps; ++i)
                plan.minimumBase += 0.01;
        }
    } else {
        plan.minimumBase = ceiling;  // the best available, still short
    }

    Result best = runAt(plan.minimumBase);
    plan.convenioUntil = until;
    plan.costIfRunToRetirement =
        runUntil(plan.minimumBase, p.retirement).convenioTotalCost;
    plan.pensionWithConvenio = best.pension;
    plan.daysWithConvenio = best.daysContributed;
    plan.ordinaryWithConvenio = best.ordinaryRetirement;
    plan.retiresAt65WithConvenio =
        !best.anticipationTooLong && !best.earlyRetirementUnqualified && best.eligible &&
        best.monthsAnticipated <= 0;
    plan.months = best.convenioMonths;
    // Starting and ending mid-month means the bill count runs one over the real
    // duration: two half months that together make one.
    plan.durationMonths = monthOrdinal(monthOf(until)) - monthOrdinal(monthOf(stopWork));
    plan.totalCost = best.convenioTotalCost;
    plan.firstMonthlyCost = best.convenioFirstMonthlyCost;
    plan.saving = std::max(0.0, plan.costIfRunToRetirement - plan.totalCost);
    return plan;
}

const Dataset& activeDataset() { return active().set; }

void setActiveDataset(Dataset d) {
    active().set = std::move(d);
    active().rebuild();
}



}  // namespace pension
