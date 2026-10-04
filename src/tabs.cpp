#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "imgui.h"
#include "tabs.h"

using namespace pension;

namespace {

const ImVec4 kAccent(0.42f, 0.74f, 0.95f, 1.0f);
const ImVec4 kGood(0.46f, 0.82f, 0.52f, 1.0f);
const ImVec4 kWarn(0.96f, 0.76f, 0.33f, 1.0f);
const ImVec4 kBad(0.95f, 0.46f, 0.44f, 1.0f);
const ImVec4 kDim(0.62f, 0.65f, 0.72f, 1.0f);

void wrapped(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextV(fmt, args);
    ImGui::PopTextWrapPos();
    va_end(args);
}

void helpMarkerT(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void dimWrapped(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, kDim);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

// One line of a figure table: label, value, and an optional note.
void row(const std::string& label, const std::string& value, const ImVec4* color = nullptr,
         const char* note = nullptr) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(label.c_str());
    ImGui::TableNextColumn();
    if (color)
        ImGui::TextColored(*color, "%s", value.c_str());
    else
        ImGui::TextUnformatted(value.c_str());
    if (ImGui::TableGetColumnCount() > 2) {
        ImGui::TableNextColumn();
        if (note) ImGui::TextColored(kDim, "%s", note);
    }
}

bool beginFigures(const char* id) {
    return ImGui::BeginTable(id, 3,
                             ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg |
                                 ImGuiTableFlags_BordersInnerH);
}

// A line chart drawn by hand: several series over a shared integer x axis, with
// a highlighted x and value labels. ImGui::PlotLines cannot show two series at
// once, and two series is the whole point of most of these charts.
struct Series {
    const char* name;
    std::vector<double> y;
    ImVec4 color;
    bool dashed = false;
};

void lineChart(const char* id, const std::vector<double>& x, const std::vector<Series>& series,
               int highlightIndex, const char* yUnit, float height) {
    if (x.empty() || series.empty()) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    ImVec2 size(width, height);

    const float padL = ImGui::GetFontSize() * 4.6f;
    const float padB = ImGui::GetFontSize() * 1.6f;
    const float padT = ImGui::GetFontSize() * 0.5f;
    const float padR = ImGui::GetFontSize() * 1.6f;
    ImVec2 plotMin(origin.x + padL, origin.y + padT);
    ImVec2 plotMax(origin.x + size.x - padR, origin.y + size.y - padB);

    double yMin = 1e300, yMax = -1e300;
    for (const auto& s : series)
        for (double v : s.y) {
            if (!std::isfinite(v)) continue;
            yMin = std::min(yMin, v);
            yMax = std::max(yMax, v);
        }
    if (yMin > yMax) return;
    double pad = std::max((yMax - yMin) * 0.08, 1.0);
    yMin -= pad;
    yMax += pad;

    dl->AddRectFilled(plotMin, plotMax, IM_COL32(16, 18, 24, 255), 4.0f);

    auto px = [&](double v) {
        double t = x.size() > 1 ? (v - x.front()) / (x.back() - x.front()) : 0.5;
        return (float)(plotMin.x + t * (plotMax.x - plotMin.x));
    };
    auto py = [&](double v) {
        double t = (v - yMin) / (yMax - yMin);
        return (float)(plotMax.y - t * (plotMax.y - plotMin.y));
    };

    // Horizontal gridlines with value labels.
    for (int i = 0; i <= 4; ++i) {
        double v = yMin + (yMax - yMin) * i / 4.0;
        float y = py(v);
        dl->AddLine(ImVec2(plotMin.x, y), ImVec2(plotMax.x, y), IM_COL32(44, 48, 58, 255));
        std::string label = eur(v, 0);
        ImVec2 ts = ImGui::CalcTextSize(label.c_str());
        dl->AddText(ImVec2(plotMin.x - ts.x - 6, y - ts.y * 0.5f), IM_COL32(150, 155, 168, 255),
                    label.c_str());
    }
    // Vertical gridlines, labelled sparsely enough to stay readable.
    int step = std::max(1, (int)x.size() / 8);
    for (size_t i = 0; i < x.size(); i += step) {
        float xp = px(x[i]);
        dl->AddLine(ImVec2(xp, plotMin.y), ImVec2(xp, plotMax.y), IM_COL32(38, 42, 51, 255));
        char buf[16];
        std::snprintf(buf, sizeof buf, "%d", (int)x[i]);
        ImVec2 ts = ImGui::CalcTextSize(buf);
        dl->AddText(ImVec2(xp - ts.x * 0.5f, plotMax.y + 4), IM_COL32(150, 155, 168, 255), buf);
    }

    if (highlightIndex >= 0 && highlightIndex < (int)x.size()) {
        float xp = px(x[highlightIndex]);
        dl->AddLine(ImVec2(xp, plotMin.y), ImVec2(xp, plotMax.y), IM_COL32(255, 255, 255, 70),
                    2.0f);
    }

    for (const auto& s : series) {
        ImU32 col = ImGui::GetColorU32(s.color);
        for (size_t i = 1; i < s.y.size() && i < x.size(); ++i) {
            if (!std::isfinite(s.y[i - 1]) || !std::isfinite(s.y[i])) continue;
            ImVec2 a(px(x[i - 1]), py(s.y[i - 1])), b(px(x[i]), py(s.y[i]));
            if (!s.dashed) {
                dl->AddLine(a, b, col, 2.0f);
                continue;
            }
            // Dash along the segment itself, so a dashed series stays readable
            // even where it runs underneath a solid one.
            float len = std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
            const float dash = 6.0f, gap = 5.0f;
            for (float t = 0; t < len; t += dash + gap) {
                float t2 = std::min(t + dash, len);
                ImVec2 p1(a.x + (b.x - a.x) * t / len, a.y + (b.y - a.y) * t / len);
                ImVec2 p2(a.x + (b.x - a.x) * t2 / len, a.y + (b.y - a.y) * t2 / len);
                dl->AddLine(p1, p2, col, 2.0f);
            }
        }
        if (highlightIndex >= 0 && highlightIndex < (int)s.y.size() &&
            std::isfinite(s.y[highlightIndex]))
            dl->AddCircleFilled(ImVec2(px(x[highlightIndex]), py(s.y[highlightIndex])), 4.0f, col);
    }

    ImGui::Dummy(size);

    // Legend, with the highlighted value spelled out.
    for (const auto& s : series) {
        ImGui::ColorButton(s.name, s.color,
                           ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker,
                           ImVec2(10, 10));
        ImGui::SameLine();
        if (highlightIndex >= 0 && highlightIndex < (int)s.y.size())
            ImGui::Text("%s: %s %s", s.name, eur(s.y[highlightIndex]).c_str(), yUnit);
        else
            ImGui::TextUnformatted(s.name);
        ImGui::SameLine(0, 18);
    }
    ImGui::NewLine();
    (void)id;
}

const char* sourceName(MonthSource s) {
    switch (s) {
        case MonthSource::Report: return "informe";
        case MonthSource::Scenario: return "escenario";
        case MonthSource::Integrated: return "laguna integrada";
        case MonthSource::Estimated: return "estimado";
        case MonthSource::None: break;
    }
    return "sin datos";
}

ImVec4 sourceColor(MonthSource s) {
    switch (s) {
        case MonthSource::Report: return ImVec4(0.82f, 0.84f, 0.88f, 1.0f);
        case MonthSource::Scenario: return kAccent;
        case MonthSource::Integrated: return kWarn;
        case MonthSource::Estimated: return ImVec4(0.80f, 0.60f, 0.90f, 1.0f);
        case MonthSource::None: break;
    }
    return kDim;
}

}  // namespace

// ---------------------------------------------------------------------------

void drawResumenTab(App& app) {
    const Result& r = app.result;
    const Result& b = app.baseline;
    const Params& p = app.params;

    ImGui::SeparatorText("De la base reguladora a la pensión");
    dimWrapped("Cada fila es un paso del cálculo del INSS. La columna «Informe» es el mismo "
               "motor corriendo sobre las hipótesis del informe oficial, así que la diferencia "
               "aísla el efecto de lo que haya cambiado en el panel izquierdo. Puede separarse "
               "uno o dos céntimos de las cifras impresas: el informe redondea cada base "
               "actualizada antes de sumarlas.");
    ImGui::Spacing();

    if (ImGui::BeginTable("##waterfall", 4,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                              ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Paso", ImGuiTableColumnFlags_WidthStretch, 2.3f);
        ImGui::TableSetupColumn("Escenario", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Informe", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Diferencia", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableHeadersRow();

        auto step = [&](const char* label, double now, double then, const char* unit,
                        bool emphasise = false) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (emphasise)
                ImGui::TextColored(kAccent, "%s", label);
            else
                ImGui::TextUnformatted(label);
            ImGui::TableNextColumn();
            ImGui::Text("%s %s", eur(now).c_str(), unit);
            ImGui::TableNextColumn();
            ImGui::TextColored(kDim, "%s %s", eur(then).c_str(), unit);
            ImGui::TableNextColumn();
            double d = now - then;
            if (std::fabs(d) < 0.005)
                ImGui::TextColored(kDim, "—");
            else
                ImGui::TextColored(d > 0 ? kGood : kBad, "%s%s", d > 0 ? "+" : "",
                                   eur(d).c_str());
        };

        step("Base reguladora", r.baseReguladora, b.baseReguladora, "€");
        step("× Porcentaje por años cotizados", r.percentByYears, b.percentByYears, "%");
        step("= Pensión inicial", r.pensionInitial, b.pensionInitial, "€");
        if (r.reductionPct > 0 || b.reductionPct > 0)
            step("− Coeficiente reductor por anticipación", r.reductionPct, b.reductionPct, "%");
        if (r.delayPct > 0 || b.delayPct > 0)
            step("+ Porcentaje por demora", r.delayPct, b.delayPct, "%");
        step("= Cálculo inicial", r.calcInitial, b.calcInitial, "€");
        step("Límite pensión máxima", r.effectiveCap, b.effectiveCap, "€");
        step("− Exceso sobre el límite", r.excess, b.excess, "€");
        if (r.delaySupplement > 0 || b.delaySupplement > 0)
            step("+ Complemento por demora", r.delaySupplement, b.delaySupplement, "€");
        step("= PENSIÓN TOTAL", r.pension, b.pension, "€/mes", true);
        step("   en 14 pagas", r.pensionAnnual, b.pensionAnnual, "€/año");
        step("   en euros de 2026", r.pensionInTodayEuros, b.pensionInTodayEuros, "€/mes");
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::SeparatorText(r.capBinding ? "Lo que manda aquí es el tope"
                                      : "El cálculo cabe bajo el tope");
    if (r.capBinding) {
        wrapped("El cálculo inicial sale a %s €, pero el tope de pensión pública de %d es de "
                "%s €. Se recortan %s €/mes, un %s de lo calculado.",
                eur(r.calcInitial).c_str(), p.retirement.year, eur(r.effectiveCap).c_str(),
                eur(r.excess).c_str(), pct(100.0 * r.excess / r.calcInitial, 1).c_str());
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, kWarn);
        wrapped("Consecuencia práctica: mientras el tope muerda, cotizar más no le sube la "
                "pensión ni un céntimo. Los únicos diales que la mueven son retrasar la "
                "jubilación (que añade un 4 %% por año completo y se cobra como complemento "
                "aparte cuando ya está en el tope) y la evolución del propio tope.");
        ImGui::PopStyleColor();
        ImGui::Spacing();
        double headroom = r.effectiveCap / (r.percentByYears / 100.0);
        wrapped("Dicho de otro modo: le bastaría una base reguladora de %s € para cobrar lo "
                "mismo. Tiene %s € de margen tirado, un %s por encima de lo necesario.",
                eur(headroom).c_str(), eur(r.baseReguladora - headroom).c_str(),
                pct(100.0 * (r.baseReguladora / headroom - 1.0), 1).c_str());
    } else {
        wrapped("El cálculo inicial (%s €) queda por debajo del tope de %d (%s €), así que aquí "
                "cada euro de base reguladora sí llega a la pensión. Le quedan %s € de margen "
                "antes de tocar el límite.",
                eur(r.calcInitial).c_str(), p.retirement.year, eur(r.effectiveCap).c_str(),
                eur(r.effectiveCap - r.calcInitial).c_str());
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Carrera de cotización");
    if (beginFigures("##career")) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed,
                               ImGui::GetFontSize() * 20.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed,
                               ImGui::GetFontSize() * 8.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
        row("Días cotizados (vida laboral)", intThousands(r.daysContributed), nullptr,
            r.daysContributed >= app.person.report.daysWholeLife
                ? ""
                : "por debajo del informe");
        row("Años computables", eur(r.yearsComputable, 2), nullptr,
            "para el porcentaje se cuentan meses completos");
        row("Días en los últimos 15 años", intThousands(r.daysLast15Years), nullptr,
            "carencia específica: bastan 730");
        row("Porcentaje por años cotizados", pct(r.percentByYears),
            r.percentByYears >= 100.0 ? &kGood : &kWarn,
            r.percentByYears >= 100.0 ? "tope alcanzado (37 años)"
                                      : "aún sube cotizando más meses");
        row("Edad ordinaria de jubilación", formatDate(r.ordinaryRetirement), nullptr,
            r.daysContributed >= (long)(38.5 * 365) ? "65 años: carrera de 38 años y 6 meses"
                                                    : "67 años: no llega a 38 años y 6 meses");
        row("Base máxima de cotización en " + std::to_string(p.retirement.year),
            eur(r.maximumBase) + " €", nullptr, "IPC + 1,2 pp al año");
        row("Tope de pensión en " + std::to_string(p.retirement.year),
            eur(r.maxPension) + " €", nullptr, "IPC + 0,115 pp al año");
        ImGui::EndTable();
    }
}

// ---------------------------------------------------------------------------

void drawInformeTab(App& app) {
    const auto& id = app.person.identity;
    const auto& rep = app.person.report;
    ImGui::SeparatorText("Información general");
    if (beginFigures("##gen")) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed,
                               ImGui::GetFontSize() * 21.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed,
                               ImGui::GetFontSize() * 13.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
        row("Titular", id.name);
        row("DNI/NIE", id.nationalId);
        row("Fecha de nacimiento", formatDate(app.person.params.birth));
        row("Fecha de jubilación", formatDate(app.person.params.retirement));
        row("Edad real en esa fecha", "65 años 0 meses 0 días");
        row("Régimen que resuelve", id.regime);
        row("Modalidad", id.modality);
        row("Fecha del cálculo", id.calcDate + "  " + id.version);
        row("Importe de la pensión", eur(rep.pension) + " €/mes", &kGood);
        ImGui::EndTable();
    }
    ImGui::Spacing();
    dimWrapped("El informe advierte que el periodo estimado representa un 22,27 % de la "
               "simulación: 3.201 de los 14.372 días cotizados son proyección, no cotización "
               "registrada. Son los que el panel izquierdo deja cambiar.");

    ImGui::Spacing();
    ImGui::SeparatorText("Cotizaciones acreditadas y bonificaciones");
    if (beginFigures("##cot")) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed,
                               ImGui::GetFontSize() * 25.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed,
                               ImGui::GetFontSize() * 8.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
        row("Durante toda la vida laboral", intThousands(rep.daysWholeLife) + " días");
        row("Últimos 15 años (carencia específica)",
            intThousands(rep.daysLast15Years) + " días");
        row("Días superpuestos", std::to_string(app.person.data.overlappingDays) + " días");
        row("A la carencia por partos", "0 días");
        row("A la edad (trabajos penosos, discapacidad)", "0 días");
        row("A la cotización (partos, cuidado de hijos)", "0 días");
        row("Por condición de mutualista", "NO — 0 días");
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Base reguladora y porcentajes del informe");
    if (beginFigures("##br")) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed,
                               ImGui::GetFontSize() * 25.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed,
                               ImGui::GetFontSize() * 10.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
        row("Periodo de bases para el cálculo", "jun-2010 a may-2035", nullptr, "300 meses");
        row("Base reguladora", eur(rep.baseReguladora) + " €", nullptr,
            (eur(rep.baseReguladoraSum) + " / 350").c_str());
        row("Por años computables", pct(rep.percentTotal), nullptr,
            (eur(rep.yearsComputable, 2) + " años").c_str());
        row("Reducción por anticipación", "—", nullptr, "no aplicable");
        row("Porcentaje por demora", "No aplicable", nullptr, "0 años de demora");
        row("Porcentaje total", pct(rep.percentTotal));
        row("Complemento por demora", eur(0.0) + " €");
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Importe de la pensión según el informe");
    if (beginFigures("##imp")) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed,
                               ImGui::GetFontSize() * 25.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed,
                               ImGui::GetFontSize() * 10.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
        row("Pensión inicial", eur(rep.baseReguladora) + " €", nullptr,
            "base reguladora × porcentaje");
        row("Cálculo inicial", eur(rep.calcInitial) + " €");
        row("Límite pensión máxima", eur(rep.maxPension) + " €", &kWarn);
        row("Límite máximo anticipada", "No aplicable");
        row("Exceso límite pensión", eur(rep.excess) + " €", &kBad);
        row("Importe pensión inicial", eur(rep.pension) + " €");
        row("Importe pensión total", eur(rep.pension) + " €", &kGood);
        row("Valor real en la fecha de jubilación", eur(rep.pensionDeflated) + " €", nullptr,
            "deflactado al 2,00 % anual");
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Observaciones del informe");
    dimWrapped(
        "A efectos del cálculo de la base reguladora se ha calculado la opción más favorable "
        "entre dividir entre 350 la suma de las bases de cotización de los 300 meses "
        "inmediatamente anteriores al mes previo al del hecho causante, y dividir entre 373,33 "
        "la suma de las 320 bases de cotización de mayor importe comprendidas dentro del "
        "periodo de los 340 meses inmediatamente anteriores.");
    ImGui::Spacing();
    dimWrapped(
        "La pensión resultante supera el límite máximo estimado para las pensiones públicas del "
        "año 2035. Por ello la pensión se limita a esa cantidad, fijada en 4.055,96 €. Este "
        "importe ha sido calculado incrementando el límite máximo actual en un 2,00 % anual (de "
        "acuerdo con el objetivo de inflación del Banco Central Europeo) más un incremento "
        "adicional del 0,115 % anual hasta el año 2050. En todo caso, llegada la fecha de "
        "jubilación, resultará de aplicación el tope máximo para las pensiones públicas que "
        "esté en vigor el año en que se cause la pensión.");
    ImGui::Spacing();
    dimWrapped(
        "Las bases de cotización futuras empleadas para el cálculo han sido incrementadas "
        "anualmente en un 2,00 %. El cálculo se ha realizado en euros actuales; teniendo en "
        "cuenta la evolución del coste de la vida, el valor real de la pensión en la fecha de "
        "jubilación equivaldría a 3.393,85 €.");
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, kWarn);
    wrapped("Esta simulación tiene carácter exclusivamente informativo: no genera derechos ni "
            "expectativas a favor del titular ni de terceros.");
    ImGui::PopStyleColor();
}

// ---------------------------------------------------------------------------

void drawVidaLaboralTab(App& app) {
    const Result& r = app.result;

    ImGui::SeparatorText("Periodos registrados");
    dimWrapped("Tal y como los recoge el informe. Los días impresos son los que manda el INSS: "
               "ya descuentan coeficientes de parcialidad y solapes.");
    ImGui::Spacing();

    if (ImGui::BeginTable("##periods", 5,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                              ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY,
                          ImVec2(0, ImGui::GetFontSize() * 17.0f))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Empresa", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn("Situación", ImGuiTableColumnFlags_WidthStretch, 2.2f);
        ImGui::TableSetupColumn("F. alta", ImGuiTableColumnFlags_WidthStretch, 0.8f);
        ImGui::TableSetupColumn("F. baja", ImGuiTableColumnFlags_WidthStretch, 0.8f);
        ImGui::TableSetupColumn("Días", ImGuiTableColumnFlags_WidthStretch, 0.6f);
        ImGui::TableHeadersRow();
        for (const auto& w : app.person.data.workPeriods) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (w.estimated)
                ImGui::TextColored(kDim, "%s", w.employer.c_str());
            else
                ImGui::TextUnformatted(w.employer.c_str());
            ImGui::TableNextColumn();
            ImGui::TextColored(kDim, "%s", w.detail.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(formatDate(w.from).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(formatDate(w.to).c_str());
            ImGui::TableNextColumn();
            if (w.estimated)
                ImGui::TextColored(kWarn, "%s", intThousands(w.days).c_str());
            else
                ImGui::TextUnformatted(intThousands(w.days).c_str());
        }
        ImGui::EndTable();
    }
    ImGui::TextColored(kWarn, "La última fila es la proyección del propio informe.");
    ImGui::SameLine();
    ImGui::TextColored(kDim, "El escenario la sustituye por los periodos del panel izquierdo.");

    ImGui::Spacing();
    ImGui::SeparatorText("Periodos del escenario");
    if (ImGui::BeginTable("##scen", 6,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                              ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Etiqueta", ImGuiTableColumnFlags_WidthStretch, 1.4f);
        ImGui::TableSetupColumn("Desde", ImGuiTableColumnFlags_WidthStretch, 0.9f);
        ImGui::TableSetupColumn("Hasta", ImGuiTableColumnFlags_WidthStretch, 0.9f);
        ImGui::TableSetupColumn("Situación", ImGuiTableColumnFlags_WidthStretch, 1.3f);
        ImGui::TableSetupColumn("Base inicial", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Días", ImGuiTableColumnFlags_WidthStretch, 0.7f);
        ImGui::TableHeadersRow();
        for (const auto& sp : app.periods) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(kAccent, "%s", sp.label.empty() ? "Periodo" : sp.label.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(formatDate(sp.from).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(formatDate(sp.to).c_str());
            ImGui::TableNextColumn();
            switch (sp.kind) {
                case PeriodKind::Contributing: ImGui::TextUnformatted("Cotizando"); break;
                case PeriodKind::Unemployment: ImGui::TextUnformatted("Desempleo"); break;
                case PeriodKind::Gap: ImGui::TextColored(kWarn, "Sin cotizar"); break;
            }
            ImGui::TableNextColumn();
            if (sp.kind == PeriodKind::Gap)
                ImGui::TextColored(kDim, "—");
            else if (sp.followMaximumBase)
                ImGui::TextColored(kDim, "base máxima legal");
            else
                ImGui::Text("%s €  (+%s/año)", eur(sp.baseAtStart).c_str(),
                            pct(sp.annualGrowthPct, 2).c_str());
            ImGui::TableNextColumn();
            long days = sp.kind == PeriodKind::Gap
                            ? 0
                            : daysInclusive(sp.from, compare(sp.to, app.params.retirement) > 0
                                                         ? app.params.retirement
                                                         : sp.to);
            ImGui::TextUnformatted(intThousands(days).c_str());
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    if (beginFigures("##days")) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed,
                               ImGui::GetFontSize() * 24.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed,
                               ImGui::GetFontSize() * 9.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
        long recorded = 0;
        for (const auto& w : app.person.data.workPeriods)
            if (!w.estimated) recorded += w.days;
        recorded -= app.person.data.overlappingDays;
        row("Días registrados hasta 04/10/2026", intThousands(recorded), nullptr,
            "ya netos de solapes");
        row("Días aportados por el escenario", intThousands(r.daysContributed - recorded));
        row("Total días cotizados", intThousands(r.daysContributed), &kAccent);
        row("Equivale a", eur(r.yearsComputable, 2) + " años");
        row("Meses que cuenta la escala", std::to_string(r.percentMonths), nullptr,
            "100 % a partir de 444 meses (37 años)");
        ImGui::EndTable();
    }
}

// ---------------------------------------------------------------------------

void drawBasesTab(App& app) {
    const Result& r = app.result;

    ImGui::SeparatorText("Bases de cotización");
    dimWrapped("BI: base introducida (nominal).  IA: índice de actualización.  "
               "BA: base actualizada = BI × IA.");
    ImGui::Spacing();
    ImGui::Checkbox("Sólo los meses del cómputo", &app.showOnlyWindow);
    ImGui::SameLine(0, 24);
    ImGui::TextColored(sourceColor(MonthSource::Report), "informe");
    ImGui::SameLine();
    ImGui::TextColored(sourceColor(MonthSource::Scenario), "escenario");
    ImGui::SameLine();
    ImGui::TextColored(sourceColor(MonthSource::Integrated), "laguna integrada");
    ImGui::SameLine();
    ImGui::TextColored(sourceColor(MonthSource::Estimated), "estimado");
    ImGui::Spacing();

    // Group the month list by year, newest first, the way the report prints it.
    std::vector<int> years;
    for (const auto& m : r.months)
        if (years.empty() || years.back() != m.ym.year) years.push_back(m.ym.year);
    std::reverse(years.begin(), years.end());

    if (ImGui::BeginTable("##bases", 13,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                              ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_ScrollX,
                          ImVec2(0, ImGui::GetContentRegionAvail().y - ImGui::GetFontSize() * 2))) {
        ImGui::TableSetupScrollFreeze(1, 1);
        ImGui::TableSetupColumn("Año");
        for (int m = 1; m <= 12; ++m) ImGui::TableSetupColumn(monthAbbrev(m));
        ImGui::TableHeadersRow();

        for (int year : years) {
            for (int which = 0; which < 3; ++which) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (which == 0)
                    ImGui::TextColored(kAccent, "%d  BI", year);
                else
                    ImGui::TextColored(kDim, "     %s", which == 1 ? "IA" : "BA");

                for (int mo = 1; mo <= 12; ++mo) {
                    ImGui::TableNextColumn();
                    const MonthDetail* d = r.find({year, mo});
                    bool inWindow = d && (d->inWindowA || (r.optionBWins && d->chosenInB));
                    if (!d || (app.showOnlyWindow && !inWindow)) {
                        ImGui::TextColored(kDim, "--");
                        continue;
                    }
                    if (d->source == MonthSource::None) {
                        ImGui::TextColored(kDim, "--");
                        continue;
                    }
                    ImVec4 col = which == 0 ? sourceColor(d->source) : kDim;
                    if (!d->inWindowA && !d->chosenInB)
                        col = ImVec4(col.x, col.y, col.z, 0.40f);
                    if (which == 0) {
                        ImGui::TextColored(col, "%s", eur(d->bi).c_str());
                        if (ImGui::BeginItemTooltip()) {
                            ImGui::Text("%s-%d", monthAbbrev(mo), year);
                            ImGui::Separator();
                            ImGui::Text("Origen: %s", sourceName(d->source));
                            ImGui::Text("BI: %s €", eur(d->bi).c_str());
                            ImGui::Text("IA: %.7f%s", d->ia, d->nominal ? "  (valor nominal)" : "");
                            ImGui::Text("BA: %s €", eur(d->ba).c_str());
                            ImGui::Separator();
                            ImGui::TextColored(d->inWindowA ? kGood : kDim,
                                               "%s en los 300 meses",
                                               d->inWindowA ? "Entra" : "No entra");
                            if (r.optionB.applicable)
                                ImGui::TextColored(d->chosenInB ? kGood : kDim,
                                                   "%s entre las %d mejores",
                                                   d->chosenInB ? "Elegido" : "No elegido",
                                                   r.optionB.basesTaken);
                            if (d->clamped)
                                ImGui::TextColored(kWarn, "Recortado a la base máxima");
                            ImGui::EndTooltip();
                        }
                    } else if (which == 1) {
                        if (d->nominal)
                            ImGui::TextColored(ImVec4(col.x, col.y, col.z, 0.55f), "1");
                        else
                            ImGui::TextColored(col, "%.5f", d->ia);
                    } else {
                        bool highlight = r.optionBWins ? d->chosenInB : d->inWindowA;
                        ImGui::TextColored(highlight ? ImVec4(0.92f, 0.94f, 0.98f, 1.0f) : col,
                                           "%s", eur(d->ba).c_str());
                    }
                }
            }
        }
        ImGui::EndTable();
    }
    ImGui::TextColored(kDim,
                       "Los 24 meses anteriores al mes previo al hecho causante llevan IA = 1: "
                       "entran a valor nominal.");
}

// ---------------------------------------------------------------------------

void drawBaseReguladoraTab(App& app) {
    const Result& r = app.result;
    const Params& p = app.params;

    ImGui::SeparatorText("Las dos formas de calcularla");
    dimWrapped("Desde 2026 la base reguladora se calcula por duplicado y se aplica la más "
               "favorable (art. 209 LGSS y disposición transitoria 40.ª, redacción del RDL "
               "2/2023). La segunda vía mira más años atrás pero descarta los peores meses, así "
               "que gana cuando la carrera tiene agujeros o tramos malos.");
    ImGui::Spacing();

    if (ImGui::BeginTable("##opts", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                              ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn("Opción A — últimos 300 meses",
                                ImGuiTableColumnFlags_WidthStretch, 1.3f);
        ImGui::TableSetupColumn("Opción B — mejores bases", ImGuiTableColumnFlags_WidthStretch,
                                1.3f);
        ImGui::TableHeadersRow();

        auto pair = [&](const char* label, const std::string& a, const std::string& b) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(label);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(a.c_str());
            ImGui::TableNextColumn();
            if (r.optionB.applicable)
                ImGui::TextUnformatted(b.c_str());
            else
                ImGui::TextColored(kDim, "no aplicable");
        };

        pair("Meses examinados", std::to_string(r.optionA.windowMonths),
             std::to_string(r.optionB.windowMonths));
        pair("Bases sumadas", std::to_string(r.optionA.basesTaken),
             std::to_string(r.optionB.basesTaken));
        pair("Divisor", eur(r.optionA.divisor), eur(r.optionB.divisor));
        pair("Suma de bases actualizadas", eur(r.optionA.sum), eur(r.optionB.sum));

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextColored(kAccent, "Base reguladora");
        ImGui::TableNextColumn();
        ImGui::TextColored(r.optionBWins ? kDim : kGood, "%s €", eur(r.optionA.value).c_str());
        ImGui::TableNextColumn();
        if (r.optionB.applicable)
            ImGui::TextColored(r.optionBWins ? kGood : kDim, "%s €", eur(r.optionB.value).c_str());
        else
            ImGui::TextColored(kDim, "—");
        ImGui::EndTable();
    }

    ImGui::Spacing();
    if (r.optionB.applicable) {
        if (r.optionBWins)
            ImGui::TextColored(kGood, "Gana la opción B por %s €/mes de base reguladora.",
                               eur(r.optionB.value - r.optionA.value).c_str());
        else
            ImGui::TextColored(kAccent,
                               "Gana la opción A por %s €/mes: la carrera está tan llena que "
                               "descartar meses no compensa alargar el periodo.",
                               eur(r.optionA.value - r.optionB.value).c_str());
    }
    if (r.optionA.monthsWithoutData > 0 || r.optionB.monthsWithoutData > 0)
        ImGui::TextColored(kWarn, "Meses estimados o sin datos: %d en A, %d en B.",
                           r.optionA.monthsWithoutData, r.optionB.monthsWithoutData);

    ImGui::Spacing();
    ImGui::SeparatorText("El periodo de cómputo de este escenario");
    if (beginFigures("##window")) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed,
                               ImGui::GetFontSize() * 24.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed,
                               ImGui::GetFontSize() * 11.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
        const MonthDetail *firstA = nullptr, *lastA = nullptr, *firstB = nullptr;
        for (const auto& m : r.months) {
            if (m.inWindowA) {
                if (!firstA) firstA = &m;
                lastA = &m;
            }
            if (m.inWindowB && !firstB) firstB = &m;
        }
        char buf[64];
        if (firstA && lastA) {
            std::snprintf(buf, sizeof buf, "%s-%d a %s-%d", monthAbbrev(firstA->ym.month),
                          firstA->ym.year, monthAbbrev(lastA->ym.month), lastA->ym.year);
            row("Opción A", buf, nullptr, "300 meses hasta el mes previo al anterior");
        }
        if (firstB && lastA) {
            std::snprintf(buf, sizeof buf, "%s-%d a %s-%d", monthAbbrev(firstB->ym.month),
                          firstB->ym.year, monthAbbrev(lastA->ym.month), lastA->ym.year);
            row("Opción B", buf, nullptr, "ventana más larga, se descartan los peores meses");
        }
        row("Hecho causante", formatDate(p.retirement));
        if (lastA) {
            std::snprintf(buf, sizeof buf, "%s-%d", monthAbbrev(lastA->ym.month), lastA->ym.year);
            row("Última base computada", buf);
        }
        int nominal = 0, integrated = 0, estimated = 0, scenario = 0;
        for (const auto& m : r.months) {
            if (!m.inWindowA) continue;
            if (m.nominal) ++nominal;
            if (m.source == MonthSource::Integrated) ++integrated;
            if (m.source == MonthSource::Estimated) ++estimated;
            if (m.source == MonthSource::Scenario) ++scenario;
        }
        row("Meses a valor nominal (IA = 1)", std::to_string(nominal), nullptr,
            "los 24 anteriores al mes previo");
        row("Meses del escenario", std::to_string(scenario), nullptr,
            "proyección, no cotización registrada");
        if (integrated)
            row("Lagunas integradas", std::to_string(integrated), &kWarn,
                "base mínima: 48 meses al 100 %, resto al 50 %");
        if (estimated)
            row("Meses estimados (antes de feb-2007)", std::to_string(estimated), &kWarn,
                "los informes no los detallan");
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Calendario de la disposición transitoria 40.ª");
    dimWrapped("El despliegue del cómputo dual, año a año, hasta su régimen definitivo en 2037. "
               "La fila del año de jubilación elegido va resaltada.");
    ImGui::Spacing();
    if (ImGui::BeginTable("##dt40", 5,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                              ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Año de jubilación");
        ImGui::TableSetupColumn("Opción A");
        ImGui::TableSetupColumn("Meses examinados");
        ImGui::TableSetupColumn("Bases sumadas");
        ImGui::TableSetupColumn("Divisor");
        ImGui::TableHeadersRow();
        for (int y = 2026; y <= 2038; ++y) {
            DualRule d = dualRuleFor(y);
            bool here = y == p.retirement.year;
            ImGui::TableNextRow();
            if (here)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(40, 86, 116, 190));
            ImGui::TableNextColumn();
            if (here)
                ImGui::TextColored(kAccent, "%d  ←", y);
            else
                ImGui::Text("%d", y);
            ImGui::TableNextColumn();
            ImGui::TextColored(kDim, "300 / 350");
            ImGui::TableNextColumn();
            ImGui::Text("%d", d.windowMonths);
            ImGui::TableNextColumn();
            ImGui::Text("%d", d.basesTaken);
            ImGui::TableNextColumn();
            ImGui::Text("%s", eur(d.divisor).c_str());
        }
        ImGui::EndTable();
    }
}

// ---------------------------------------------------------------------------

void drawProyeccionesTab(App& app) {
    const Params& base = app.params;

    ImGui::SeparatorText("Pensión según la fecha de jubilación");
    dimWrapped("Se recalcula todo para cada año: la carrera cotizada hasta esa fecha, la base "
               "reguladora, la edad ordinaria que le corresponda, el coeficiente por "
               "anticipación o el porcentaje por demora, y el tope de ese año.");
    ImGui::Spacing();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
    ImGui::DragInt("Desde", &app.sweepFromYear, 0.2f, 2031, 2044);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
    ImGui::DragInt("Hasta", &app.sweepToYear, 0.2f, 2032, 2050);
    app.sweepFromYear = std::clamp(app.sweepFromYear, 2031, 2044);
    app.sweepToYear = std::clamp(app.sweepToYear, app.sweepFromYear + 1, 2050);

    std::vector<double> xs;
    Series pensionSeries{"Pensión final", {}, kGood, false};
    Series calcSeries{"Cálculo inicial (sin tope)", {}, kAccent, false};
    Series capSeries{"Tope máximo", {}, kBad, true};
    Series realSeries{"Pensión en euros de 2026", {}, kWarn, false};
    int highlight = -1;

    std::vector<int> unavailable;
    for (int y = app.sweepFromYear; y <= app.sweepToYear; ++y) {
        Params p = base;
        p.retirement = {y, base.retirement.month, base.retirement.day};
        auto periods = app.periods;
        for (auto& sp : periods)
            if (compare(sp.to, p.retirement) > 0) sp.to = p.retirement;
        Result res = compute(p, periods);
        if (y == base.retirement.year) highlight = (int)xs.size();
        xs.push_back(y);
        // Years the law does not actually allow are left out of the curve
        // rather than drawn as if they were on offer.
        bool ok = res.eligible && !res.anticipationTooLong;
        if (!ok) unavailable.push_back(y);
        const double nan = std::nan("");
        pensionSeries.y.push_back(ok ? res.pension : nan);
        calcSeries.y.push_back(ok ? res.calcInitial : nan);
        capSeries.y.push_back(ok ? res.effectiveCap : nan);
        realSeries.y.push_back(ok ? res.pensionInTodayEuros : nan);
    }
    lineChart("##sweep", xs, {calcSeries, capSeries, pensionSeries, realSeries}, highlight, "€",
              ImGui::GetFontSize() * 15.0f);

    ImGui::Spacing();
    if (!unavailable.empty()) {
        std::string list;
        for (size_t i = 0; i < unavailable.size(); ++i)
            list += (i ? ", " : "") + std::to_string(unavailable[i]);
        ImGui::PushStyleColor(ImGuiCol_Text, kWarn);
        wrapped("Sin curva en %s: con la carrera que tendría en esos años la edad ordinaria "
                "pasa de 65 a 67, el adelanto se va por encima de los 24 meses que permite el "
                "art. 208 LGSS y la jubilación no se puede causar.",
                list.c_str());
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }
    dimWrapped("Donde la línea azul va por encima de la roja, el tope manda y la pensión verde "
               "se pega al límite. La línea naranja es la misma pensión medida en euros de hoy: "
               "si sube menos que la verde, lo que gana retrasándose se lo come la inflación.");

    ImGui::Spacing();
    ImGui::SeparatorText("Pensión según el crecimiento de las bases futuras");
    dimWrapped("Se mantiene la fecha de jubilación y se mueve sólo el crecimiento anual de la "
               "base de cotización del último periodo del escenario.");
    ImGui::Spacing();

    if (!app.periods.empty()) {
        std::vector<double> gx;
        Series gPension{"Pensión final", {}, kGood, false};
        Series gCalc{"Cálculo inicial (sin tope)", {}, kAccent, false};
        Series gCap{"Tope máximo", {}, kBad, true};
        int gHighlight = -1;
        double current = app.periods.back().annualGrowthPct;

        for (int i = 0; i <= 20; ++i) {
            double growth = -2.0 + i * 0.5;  // -2 % to +8 %
            Params p = base;
            auto periods = app.periods;
            periods.back().annualGrowthPct = growth;
            periods.back().followMaximumBase = false;
            Result res = compute(p, periods);
            if (gHighlight < 0 && growth >= current - 0.25 && growth <= current + 0.25)
                gHighlight = (int)gx.size();
            gx.push_back(growth);
            gPension.y.push_back(res.pension);
            gCalc.y.push_back(res.calcInitial);
            gCap.y.push_back(res.effectiveCap);
        }
        lineChart("##growth", gx, {gCalc, gCap, gPension}, gHighlight, "€",
                  ImGui::GetFontSize() * 12.0f);
        ImGui::TextColored(kDim, "Eje horizontal: crecimiento anual de la base, en %%.");
        ImGui::Spacing();
        double flat = gPension.y.front(), high = gPension.y.back();
        if (std::fabs(high - flat) < 0.01)
            ImGui::TextColored(kWarn,
                               "La pensión es completamente plana en todo el rango: desde una "
                               "base que cae un 2 %% al año hasta una que sube un 8 %%, cobra "
                               "exactamente lo mismo.");
        else
            wrapped("Entre el extremo bajo y el alto la pensión varía %s €/mes.",
                    eur(high - flat).c_str());
    }

    ImGui::Spacing();
    ImGui::SeparatorText("La tijera: base máxima contra tope de pensión");
    dimWrapped("La base máxima de cotización sube el IPC más 1,2 puntos al año; el tope de la "
               "pensión, el IPC más 0,115. Las dos líneas se separan año tras año: es el "
               "mecanismo por el que las carreras altas cotizan cada vez más para una pensión "
               "que crece cada vez menos en proporción.");
    ImGui::Spacing();
    {
        std::vector<double> sx;
        Series sBase{"Base máxima de cotización", {}, kAccent, false};
        Series sCap{"Tope de pensión", {}, kBad, false};
        int sHighlight = -1;
        for (int y = 2026; y <= 2050; ++y) {
            if (y == base.retirement.year) sHighlight = (int)sx.size();
            sx.push_back(y);
            sBase.y.push_back(maximumBaseFor(base, y));
            sCap.y.push_back(maxPensionFor(base, y));
        }
        lineChart("##scissors", sx, {sBase, sCap}, sHighlight, "€",
                  ImGui::GetFontSize() * 12.0f);
        double r26 = maxPensionFor(base, 2026) / maximumBaseFor(base, 2026);
        double r50 = maxPensionFor(base, 2050) / maximumBaseFor(base, 2050);
        wrapped("En 2026 el tope de pensión es el %s de la base máxima; en 2050 será el %s.",
                pct(r26 * 100.0, 1).c_str(), pct(r50 * 100.0, 1).c_str());
    }
}

// ---------------------------------------------------------------------------

void drawConvenioTab(App& app) {
    Params& p = app.params;
    const Result& r = app.result;

    ImGui::SeparatorText("Dejar de trabajar antes y pagarse la cotización");
    dimWrapped(
        "El convenio especial (Orden TAS/2865/2003) es un acuerdo por el que se deja de "
        "trabajar y se sigue cotizando de su bolsillo, eligiendo la base. Cuenta como situación "
        "asimilada al alta: los días suman para la carencia, para el porcentaje y para el "
        "requisito de 38 años y 6 meses igual que si siguiera trabajando. Y como aquí la "
        "pensión está topada, no hace falta cotizar por la base máxima para cobrar lo mismo: "
        "basta con la base que mantenga la base reguladora por encima de lo que exige el tope.");
    ImGui::Spacing();
    ImGui::TextColored(kDim, "Cuota mensual = base × %s × %s + base × MEI",
                       pct(p.convenioRatePct, 2).c_str(), eur(p.convenioCoefficient, 2).c_str());
    ImGui::SameLine();
    ImGui::TextColored(kDim, " (MEI %d: %s)", p.retirement.year,
                       pct(meiRatePct(p.retirement.year), 2).c_str());

    // --- the sweep, which is also where the headline answer comes from -----
    const double target = r.pension;
    struct Row {
        Date stop;
        ConvenioPlan plan;
    };
    std::vector<Row> rows;
    for (int y = 2027; y <= p.retirement.year; ++y) {
        for (int m = 1; m <= 12; m += 6) {
            Date stop{y, m, 10};
            if (compare(stop, Date{2026, 10, 5}) <= 0) continue;
            if (compare(stop, p.retirement) >= 0) continue;
            rows.push_back({stop, planConvenio(p, app.periods, stop, target, app.convenioGrowthPct)});
        }
    }

    const Row* earliestFloor = nullptr;   // soonest you can stop paying the minimum
    const Row* earliestNoConvenio = nullptr;  // soonest you can stop with nothing at all
    for (const auto& row : rows) {
        if (!earliestFloor && row.plan.feasible && row.plan.floorIsEnough) earliestFloor = &row;
        if (!earliestNoConvenio && row.plan.convenioUnnecessary) earliestNoConvenio = &row;
    }

    // The table steps every six months; walk back month by month inside that
    // step so the headline date is exact without sweeping every month.
    auto refine = [&](const Row* found, bool wantNoConvenio) -> Row {
        Row best = *found;
        for (int back = 1; back <= 5; ++back) {
            Date earlier = addMonthsToDate(found->stop, -back);
            if (compare(earlier, Date{2026, 10, 5}) <= 0) break;
            ConvenioPlan c =
                planConvenio(p, app.periods, earlier, target, app.convenioGrowthPct);
            bool ok = wantNoConvenio ? c.convenioUnnecessary : (c.feasible && c.floorIsEnough);
            if (!ok) break;
            best = {earlier, c};
        }
        return best;
    };
    Row refinedFloor{}, refinedNone{};
    if (earliestFloor) {
        refinedFloor = refine(earliestFloor, false);
        earliestFloor = &refinedFloor;
    }
    if (earliestNoConvenio) {
        refinedNone = refine(earliestNoConvenio, true);
        earliestNoConvenio = &refinedNone;
    }

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
    wrapped("El convenio no tiene por qué llegar hasta la jubilación. Sólo hace falta "
            "mantenerlo hasta que la carrera cruce los 38 años y 6 meses, porque a partir de "
            "ahí los días de más no compran nada: la pensión ya está en el tope. Y los meses "
            "que queden sueltos se rellenan con la base mínima por integración de lagunas — "
            "justo la cifra que pagaría el convenio más barato, pero gratis.");
    ImGui::PopStyleColor();

    ImGui::Spacing();
    ImGui::SeparatorText("La respuesta");
    if (earliestFloor) {
        const auto& c = earliestFloor->plan;
        ImGui::PushStyleColor(ImGuiCol_Text, kGood);
        wrapped("Puede dejar de trabajar el %s y suscribir un convenio especial por la base "
                "mínima, %s €/mes, que le costará %s €/mes. Cancelándolo el %s, el total son "
                "%s € y cobrará exactamente la misma pensión: %s €/mes.",
                formatDate(earliestFloor->stop).c_str(), eur(c.minimumBase).c_str(),
                eur(c.firstMonthlyCost).c_str(), formatDate(c.convenioUntil).c_str(),
                eur(c.totalCost, 0).c_str(), eur(c.pensionWithConvenio).c_str());
        ImGui::PopStyleColor();
        ImGui::Spacing();
        wrapped("Antes de esa fecha también puede dejarlo, pero entonces la base mínima ya no "
                "basta y hay que elegir una base más alta —y más cara— para compensar los meses "
                "que el convenio mete en la base reguladora. La tabla de abajo lo recorre.");
    } else {
        ImGui::TextColored(kWarn,
                           "Con los parámetros actuales no hay ninguna fecha en la que la base "
                           "mínima baste. Mire la tabla para ver qué base haría falta.");
    }
    if (earliestNoConvenio && compare(earliestNoConvenio->stop, p.retirement) < 0) {
        ImGui::Spacing();
        wrapped("A partir del %s ni siquiera necesita convenio: puede dejarlo todo sin pagar "
                "nada y la pensión no se mueve.",
                formatDate(earliestNoConvenio->stop).c_str());
    }

    // --- the chosen date in detail ----------------------------------------
    ImGui::Spacing();
    ImGui::SeparatorText("Una fecha concreta");
    ImGui::TextColored(kDim, "Último día de trabajo");
    ImGui::SameLine();
    {
        ImGui::PushID("convstop");
        float w = ImGui::GetFontSize() * 2.8f;
        ImGui::SetNextItemWidth(w);
        ImGui::DragInt("##d", &app.convenioStop.day, 0.15f, 1, 31, "%02d");
        ImGui::SameLine(0, 4);
        ImGui::SetNextItemWidth(w);
        ImGui::DragInt("##m", &app.convenioStop.month, 0.15f, 1, 12, "%02d");
        ImGui::SameLine(0, 4);
        ImGui::SetNextItemWidth(w * 1.8f);
        ImGui::DragInt("##y", &app.convenioStop.year, 0.3f, 2026, p.retirement.year, "%04d");
        ImGui::PopID();
        app.convenioStop.month = std::clamp(app.convenioStop.month, 1, 12);
        app.convenioStop.year = std::clamp(app.convenioStop.year, 2026, p.retirement.year);
        app.convenioStop.day =
            std::clamp(app.convenioStop.day, 1,
                       daysInMonth(app.convenioStop.year, app.convenioStop.month));
    }
    ImGui::SameLine(0, 20);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);
    ImGui::DragScalar("Subida anual de la base", ImGuiDataType_Double, &app.convenioGrowthPct,
                      0.02f, nullptr, nullptr, "%.2f %%");
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);
    ImGui::DragScalar("Base mínima elegible en 2026", ImGuiDataType_Double, &p.convenioFloorBase,
                      2.0f, nullptr, nullptr, "%.2f €");
    {
        // The field is an anchor in 2026 euros, like the others. Spell out what
        // it becomes in the chosen year, because typing a figure already
        // carried forward is an easy and expensive mistake.
        double floorThatYear =
            p.convenioFloorBase *
            std::pow(1.0 + app.convenioGrowthPct / 100.0, app.convenioStop.year - p.anchorYear);
        ImGui::SameLine();
        ImGui::TextColored(kDim, "→ %s € en %d", eur(floorThatYear).c_str(),
                           app.convenioStop.year);
    }
    helpMarkerT("Es un anclaje en euros de 2026, como los demás parámetros: el programa lo "
                "actualiza solo hasta el año en que empiece el convenio. No teclee aquí una "
                "cifra ya proyectada —si pone los 1.481,95 € que le ofrecen para 2028, el "
                "programa los volverá a subir dos años y saldrá 1.541,82 €.\n\n"
                "El art. 6.2.1 de la orden deja elegir entre cuatro bases: a) la base máxima de "
                "su grupo, si cotizó por ella 24 meses en los últimos cinco (aquí se cumple de "
                "sobra), b) la media de sus 12 últimas bases, c) el suelo y d) cualquiera "
                "intermedia. El suelo que aplica el INSS es la base mínima del Régimen General, "
                "1.424,40 € en 2026, proyectada al 2 % anual: el simulador oficial ofrece "
                "1.481,95 € para un convenio iniciado en 2028, que es 1.424,40 x 1,02². La "
                "letra c), tal como está redactada, remite a la tabla general del RETA "
                "(950,98 €), más baja.");

    ConvenioPlan chosen =
        planConvenio(p, app.periods, app.convenioStop, target, app.convenioGrowthPct);

    ImGui::Spacing();
    if (beginFigures("##chosen")) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 26.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 10.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
        row("Cancelar el convenio el", formatDate(chosen.convenioUntil), &kAccent,
            compare(chosen.convenioUntil, p.retirement) < 0
                ? "en cuanto la carrera llega a 38 años y 6 meses"
                : "hay que mantenerlo hasta la jubilación");
        row("Duración del convenio", std::to_string(chosen.durationMonths) + " meses", nullptr,
            (std::to_string(chosen.months) +
             " recibos: el primero y el último salen prorrateados").c_str());
        if (chosen.saving > 0.5)
            row("Ahorro por cancelarlo a tiempo", eur(chosen.saving, 0) + " €", &kGood,
                ("frente a los " + eur(chosen.costIfRunToRetirement, 0) +
                 " € de mantenerlo hasta el final").c_str());
        row("Base mínima que mantiene la pensión", eur(chosen.minimumBase) + " €", &kAccent,
            chosen.floorIsEnough
                ? "es el suelo de ese año: no se puede bajar más"
                : "por encima del suelo: estos meses sí pesan en la base reguladora");
        row("Cuota mensual el primer año", eur(chosen.firstMonthlyCost) + " €");
        row("Coste total hasta la jubilación", eur(chosen.totalCost, 0) + " €", &kWarn);
        row("Pensión con ese convenio", eur(chosen.pensionWithConvenio) + " €/mes",
            chosen.pensionWithConvenio >= target - 0.01 ? &kGood : &kBad,
            chosen.pensionWithConvenio >= target - 0.01 ? "intacta" : "por debajo del objetivo");
        row("Días cotizados al jubilarse", intThousands(chosen.daysWithConvenio), nullptr,
            chosen.retiresAt65WithConvenio ? "llega a 38 años y 6 meses: se jubila a los 65"
                                           : "no llega: la edad ordinaria pasa a 67");
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::TextColored(kDim, "Y si lo deja sin suscribir nada:");
    if (beginFigures("##bare")) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 26.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 10.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
        row("Días cotizados al jubilarse", intThousands(chosen.daysWithoutConvenio), nullptr,
            "los meses parados no suman días");
        row("Edad ordinaria que le quedaría", formatDate(chosen.ordinaryWithoutConvenio), nullptr,
            chosen.retiresAt65WithoutConvenio
                ? ""
                : "jubilarse en la fecha prevista sería anticipada, con su penalización");
        double loss = target - chosen.pensionWithoutConvenio;
        row("Pensión resultante", eur(chosen.pensionWithoutConvenio) + " €/mes",
            loss > 0.01 ? &kBad : &kGood,
            loss > 0.01 ? "" : "sin pérdida");
        if (loss > 0.01) {
            row("Lo que perdería", "-" + eur(loss) + " €/mes", &kBad,
                ("-" + eur(loss * 14.0, 0) + " € al año, de por vida").c_str());
            if (chosen.totalCost > 0)
                row("El convenio se paga solo en", eur(chosen.totalCost / (loss * 14.0), 1) +
                                                       " años de pensión", &kAccent,
                    "comparando su coste total con lo que evita perder");
        }
        ImGui::EndTable();
    }
    if (!chosen.retiresAt65WithoutConvenio) {
        ImGui::PushStyleColor(ImGuiCol_Text, kWarn);
        wrapped("Ojo: sin convenio la carrera se queda corta, la edad ordinaria sube a 67 y "
                "jubilarse en la fecha prevista pasa a ser anticipada voluntaria. Esa modalidad "
                "exige además 35 años cotizados y estar en alta o situación asimilada, así que "
                "la cifra de arriba es optimista: puede que esa jubilación no esté disponible en "
                "absoluto.");
        ImGui::PopStyleColor();
    }

    if (ImGui::Button("Aplicar este plan al escenario")) {
        app.periods = scenarioStoppingAt(app.periods, app.convenioStop, p.retirement, true,
                                         chosen.minimumBase, app.convenioGrowthPct,
                                         chosen.convenioUntil);
    }
    ImGui::SameLine();
    ImGui::TextColored(kDim, "sustituye los periodos futuros por trabajo + convenio");

    // --- the full sweep ----------------------------------------------------
    ImGui::Spacing();
    ImGui::SeparatorText("Cada fecha posible");
    if (ImGui::BeginTable("##sweep", 7,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                              ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Dejar de trabajar");
        ImGui::TableSetupColumn("Base mínima necesaria");
        ImGui::TableSetupColumn("Cuota/mes");
        ImGui::TableSetupColumn("Cancelarlo el");
        ImGui::TableSetupColumn("Coste total");
        ImGui::TableSetupColumn("Pensión con convenio");
        ImGui::TableSetupColumn("Pensión sin nada");
        ImGui::TableHeadersRow();
        for (const auto& rw : rows) {
            const auto& c = rw.plan;
            bool isChosen = compare(rw.stop, app.convenioStop) == 0;
            bool isEarliest = earliestFloor && compare(rw.stop, earliestFloor->stop) == 0;
            ImGui::TableNextRow();
            if (isChosen)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(40, 86, 116, 190));
            else if (isEarliest)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(38, 92, 52, 150));
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(formatDate(rw.stop).c_str());
            ImGui::TableNextColumn();
            ImGui::TextColored(c.floorIsEnough ? kGood : kWarn, "%s €%s",
                               eur(c.minimumBase).c_str(), c.floorIsEnough ? "  (suelo)" : "");
            ImGui::TableNextColumn();
            ImGui::Text("%s €", eur(c.firstMonthlyCost).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(formatDate(c.convenioUntil).c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%s €", eur(c.totalCost, 0).c_str());
            ImGui::TableNextColumn();
            ImGui::TextColored(c.pensionWithConvenio >= target - 0.01 ? kGood : kBad, "%s €",
                               eur(c.pensionWithConvenio).c_str());
            ImGui::TableNextColumn();
            ImGui::TextColored(kBad, "%s €", eur(c.pensionWithoutConvenio).c_str());
        }
        ImGui::EndTable();
    }
    ImGui::TextColored(kGood, "Verde (suelo):");
    ImGui::SameLine();
    ImGui::TextColored(kDim,
                       "la base mínima ya basta, y es la más barata que le dejan elegir ese "
                       "año. Sube con los años porque el propio suelo se actualiza, no porque "
                       "haga falta más base.");

    // --- cost curve --------------------------------------------------------
    ImGui::Spacing();
    ImGui::SeparatorText("Coste del convenio según cuándo lo deje");
    {
        std::vector<double> xs;
        Series costSeries{"Coste total del convenio", {}, kWarn, false};
        Series baseSeries{"Base mínima necesaria", {}, kAccent, false};
        int highlight = -1;
        for (size_t i = 0; i < rows.size(); ++i) {
            if (compare(rows[i].stop, app.convenioStop) == 0) highlight = (int)i;
            xs.push_back(rows[i].stop.year + (rows[i].stop.month - 1) / 12.0);
            costSeries.y.push_back(rows[i].plan.totalCost);
            baseSeries.y.push_back(rows[i].plan.minimumBase);
        }
        lineChart("##convcost", xs, {costSeries, baseSeries}, highlight, "€",
                  ImGui::GetFontSize() * 12.0f);
        dimWrapped("Cuanto antes lo deje, más meses de convenio paga; pero la base que necesita "
                   "sólo sube cuando esos meses empiezan a pesar en la base reguladora. Entre "
                   "las dos curvas está el punto que le convenga.");
    }
}
