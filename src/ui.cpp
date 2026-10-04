#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "app.h"
#include "imgui.h"
#include "tabs.h"

using namespace pension;

// ---------------------------------------------------------------------------
// Formatting, Spanish conventions: thousands with '.', decimals with ','.

std::string intThousands(long v) {
    char raw[32];
    std::snprintf(raw, sizeof raw, "%ld", v < 0 ? -v : v);
    std::string digits = raw, out;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i && (digits.size() - i) % 3 == 0) out += '.';
        out += digits[i];
    }
    return (v < 0 ? "-" : "") + out;
}

std::string eur(double v, int decimals) {
    if (!std::isfinite(v)) return "--";
    double rounded = std::round(v * std::pow(10, decimals)) / std::pow(10, decimals);
    long whole = (long)std::floor(std::fabs(rounded));
    double frac = std::fabs(rounded) - whole;
    char fracbuf[16];
    std::snprintf(fracbuf, sizeof fracbuf, "%.*f", decimals, frac);
    std::string out = (rounded < 0 ? "-" : "") + intThousands(whole);
    if (decimals > 0) out += "," + std::string(fracbuf + 2);
    return out;
}

std::string pct(double v, int decimals) { return eur(v, decimals) + " %"; }

namespace {

const ImVec4 kAccent(0.42f, 0.74f, 0.95f, 1.0f);
const ImVec4 kGood(0.46f, 0.82f, 0.52f, 1.0f);
const ImVec4 kWarn(0.96f, 0.76f, 0.33f, 1.0f);
const ImVec4 kBad(0.95f, 0.46f, 0.44f, 1.0f);
const ImVec4 kDim(0.62f, 0.65f, 0.72f, 1.0f);

const char* kPeriodKindNames[] = {"Cotizando (alta)", "Desempleo contributivo",
                                  "Sin cotizar (laguna)"};

ImFont* gBigFont = nullptr;  // for the headline figure, set up in loadFonts()

void helpMarker(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// A date as three compact fields on one line.
bool dateEditor(const char* id, Date& d, int yearMin, int yearMax) {
    bool changed = false;
    ImGui::PushID(id);
    float w = ImGui::GetFontSize() * 2.6f;
    ImGui::SetNextItemWidth(w);
    changed |= ImGui::DragInt("##d", &d.day, 0.15f, 1, 31, "%02d");
    ImGui::SameLine(0, 4);
    ImGui::TextUnformatted("/");
    ImGui::SameLine(0, 4);
    ImGui::SetNextItemWidth(w);
    changed |= ImGui::DragInt("##m", &d.month, 0.15f, 1, 12, "%02d");
    ImGui::SameLine(0, 4);
    ImGui::TextUnformatted("/");
    ImGui::SameLine(0, 4);
    ImGui::SetNextItemWidth(w * 1.7f);
    changed |= ImGui::DragInt("##y", &d.year, 0.3f, yearMin, yearMax, "%04d");
    ImGui::PopID();
    if (changed) {
        d.month = std::clamp(d.month, 1, 12);
        d.year = std::clamp(d.year, yearMin, yearMax);
        d.day = std::clamp(d.day, 1, daysInMonth(d.year, d.month));
    }
    return changed;
}

void drawScenarioPanel(App& app) {
    Params& p = app.params;
    const Result& r = app.result;

    // --- the live answer, kept at the top where it is always visible -------
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.13f, 0.16f, 0.21f, 1.0f));
    ImGui::BeginChild("##readout", ImVec2(0, ImGui::GetFontSize() * 8.8f), true);
    ImGui::TextColored(kDim, "PENSIÓN FINAL ESTIMADA");
    if (gBigFont) ImGui::PushFont(gBigFont);
    ImGui::TextColored(r.eligible ? kGood : kBad, "%s €/mes", eur(r.pension).c_str());
    if (gBigFont) ImGui::PopFont();
    ImGui::TextColored(kDim, "%s €/año en 14 pagas", eur(r.pensionAnnual).c_str());
    ImGui::TextColored(kDim, "%s € en euros de 2026", eur(r.pensionInTodayEuros).c_str());
    double delta = r.pension - app.baseline.pension;
    if (std::fabs(delta) >= 0.01)
        ImGui::TextColored(delta > 0 ? kGood : kBad, "%s%s € frente al informe",
                           delta > 0 ? "+" : "", eur(delta).c_str());
    else
        ImGui::TextColored(kDim, "igual que el informe oficial");
    ImGui::EndChild();
    ImGui::PopStyleColor();

    if (r.capBinding) {
        ImGui::TextColored(kWarn, "Tope máximo activo");
        helpMarker("El cálculo inicial supera la pensión máxima del año de jubilación, así que "
                   "la pensión se recorta hasta el tope. Mientras eso ocurra, subir las bases "
                   "futuras no cambia nada: lo único que mueve la cifra es retrasar la "
                   "jubilación o un tope más alto.");
    }
    if (r.anticipationTooLong) ImGui::TextColored(kBad, "Anticipación fuera del límite legal");
    if (!r.eligible) ImGui::TextColored(kBad, "No se cumple la carencia mínima.");

    ImGui::Spacing();

    // --- presets ----------------------------------------------------------
    if (ImGui::Button("Informe oficial", ImVec2(-FLT_MIN, 0))) app.resetToReport();
    helpMarker("Vuelve exactamente a lo que supuso el informe del INSS: un único periodo "
               "cotizando a la base máxima de 2026 creciendo al 2,00 % anual hasta el "
               "10/07/2035.");
    if (ImGui::Button("Base máxima real (IPC + 1,2 pp)", ImVec2(-FLT_MIN, 0))) {
        app.resetToReport();
        app.periods[0].followMaximumBase = true;
        app.periods[0].label = "Base máxima legal";
    }
    helpMarker("El informe proyecta su base al 2,00 %, pero la base máxima de cotización sube "
               "por ley al IPC más 1,2 puntos al año hasta 2050. Esto cotiza por la base "
               "máxima de verdad: pagará más y verá si le sirve de algo.");
    if (ImGui::Button("Dejar de cotizar ya", ImVec2(-FLT_MIN, 0))) {
        app.resetToReport();
        app.periods[0].kind = PeriodKind::Gap;
        app.periods[0].label = "Sin cotizar";
    }
    if (ImGui::Button("Demorar 2 años", ImVec2(-FLT_MIN, 0))) {
        app.resetToReport();
        p.retirement = {2037, 7, 10};
        app.periods[0].to = p.retirement;
        app.periods[0].label = "Demora hasta 2037";
    }

    ImGui::Spacing();
    ImGui::Separator();

    // --- retirement date --------------------------------------------------
    if (ImGui::CollapsingHeader("Fecha de jubilación", ImGuiTreeNodeFlags_DefaultOpen)) {
        Date before = p.retirement;
        ImGui::TextColored(kDim, "Fecha elegida");
        dateEditor("retire", p.retirement, 2030, 2050);

        int ordinalDelta = monthOrdinal(monthOf(p.retirement)) -
                           monthOrdinal(monthOf(before));
        if (ordinalDelta != 0 || compare(before, p.retirement) != 0) {
            // Keep the last period running up to the retirement date.
            if (!app.periods.empty()) {
                auto& last = app.periods.back();
                if (compare(last.to, before) == 0) last.to = p.retirement;
            }
        }

        int ageMonths = monthOrdinal(monthOf(p.retirement)) - monthOrdinal(monthOf(p.birth));
        if (p.retirement.day < p.birth.day) --ageMonths;
        ImGui::Text("Edad: %d años %d meses", ageMonths / 12, ageMonths % 12);
        ImGui::TextColored(kDim, "Edad ordinaria: %s",
                           formatDate(r.ordinaryRetirement).c_str());
        helpMarker("La edad ordinaria son 67 años, o 65 si acredita 38 años y 6 meses "
                   "cotizados (escala final de la Ley 27/2011). Ojo: depende de lo cotizado "
                   "en esa misma fecha, así que recortar el futuro puede empujarla de 65 a 67 "
                   "y dejar la jubilación anticipada fuera de plazo.");

        if (r.monthsAnticipated > 0) {
            ImGui::TextColored(r.anticipationTooLong ? kBad : kWarn,
                               "Anticipación: %d meses", r.monthsAnticipated);
            ImGui::TextColored(r.anticipationTooLong ? kBad : kWarn,
                               "Coeficiente reductor: %s", pct(r.reductionPct, 2).c_str());
        } else if (r.monthsAnticipated < 0) {
            ImGui::TextColored(kGood, "Demora: %d meses (%d años completos)",
                               -r.monthsAnticipated, r.yearsDelayed);
            ImGui::TextColored(kGood, "Porcentaje por demora: +%s", pct(r.delayPct, 2).c_str());
        } else {
            ImGui::TextColored(kGood, "Justo en la edad ordinaria.");
        }
        ImGui::Checkbox("Anticipada voluntaria", &p.voluntaryEarlyRetirement);
        helpMarker("La anticipada voluntaria (art. 208 LGSS) aplica los coeficientes "
                   "completos, y desde 2033 también sobre el tope máximo. La involuntaria "
                   "(art. 207, por despido) es más benigna; aquí se modela como el 75 % del "
                   "coeficiente voluntario, que es una regla aproximada, no una tabla legal.");
    }

    // --- future periods ---------------------------------------------------
    if (ImGui::CollapsingHeader("Periodos futuros", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextColored(kDim, "Desde el 05/10/2026 (día siguiente al informe)");
        ImGui::Spacing();

        for (size_t i = 0; i < app.periods.size(); ++i) {
            ScenarioPeriod& sp = app.periods[i];
            ImGui::PushID((int)i);
            char title[128];
            std::snprintf(title, sizeof title, "%s  %s → %s",
                          sp.label.empty() ? "Periodo" : sp.label.c_str(),
                          formatDate(sp.from).c_str(), formatDate(sp.to).c_str());
            if (ImGui::TreeNodeEx("##per", ImGuiTreeNodeFlags_DefaultOpen, "%s", title)) {
                ImGui::TextColored(kDim, "Desde");
                dateEditor("from", sp.from, 2026, 2050);
                ImGui::TextColored(kDim, "Hasta");
                dateEditor("to", sp.to, 2026, 2050);

                int kind = (int)sp.kind;
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::Combo("##kind", &kind, kPeriodKindNames, 3))
                    sp.kind = (PeriodKind)kind;
                helpMarker("Cotizando: los días cuentan para la carrera y la base entra en el "
                           "cálculo.\nDesempleo: los días siguen contando, con la base que "
                           "indique.\nSin cotizar: los días no cuentan, y los meses que caigan "
                           "dentro del periodo de cómputo se rellenan con la base mínima "
                           "(integración de lagunas).");

                if (sp.kind != PeriodKind::Gap) {
                    ImGui::Checkbox("Seguir la base máxima legal", &sp.followMaximumBase);
                    if (!sp.followMaximumBase) {
                        ImGui::TextColored(kDim, "Base mensual en %d", sp.from.year);
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        ImGui::DragScalar("##base", ImGuiDataType_Double, &sp.baseAtStart, 5.0f,
                                          nullptr, nullptr, "%.2f €");
                        ImGui::TextColored(kDim, "Crecimiento anual");
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        ImGui::DragScalar("##growth", ImGuiDataType_Double, &sp.annualGrowthPct,
                                          0.02f, nullptr, nullptr, "%.2f %%");
                    }
                }

                long days = sp.kind == PeriodKind::Gap
                                ? 0
                                : daysInclusive(sp.from, compare(sp.to, p.retirement) > 0
                                                             ? p.retirement
                                                             : sp.to);
                ImGui::TextColored(kDim, "%s días cotizados", intThousands(days).c_str());

                if (app.periods.size() > 1 && ImGui::SmallButton("Eliminar")) {
                    app.periods.erase(app.periods.begin() + i);
                    ImGui::TreePop();
                    ImGui::PopID();
                    break;
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
            ImGui::Spacing();
        }

        if (ImGui::Button("Añadir periodo", ImVec2(-FLT_MIN, 0))) {
            ScenarioPeriod sp;
            Date start = app.periods.empty() ? Date{2026, 10, 5}
                                             : addMonthsToDate(app.periods.back().to, 1);
            sp.from = start;
            sp.to = p.retirement;
            sp.baseAtStart = maximumBaseFor(p, start.year);
            sp.annualGrowthPct = p.inflationPct;
            sp.label = "Nuevo";
            app.periods.push_back(sp);
        }
        if (ImGui::Button("Partir el último en dos", ImVec2(-FLT_MIN, 0)) &&
            !app.periods.empty()) {
            ScenarioPeriod& last = app.periods.back();
            int span = monthOrdinal(monthOf(last.to)) - monthOrdinal(monthOf(last.from));
            if (span >= 2) {
                Date mid = addMonthsToDate(last.from, span / 2);
                ScenarioPeriod tail = last;
                tail.from = addMonthsToDate(mid, 1);
                tail.from.day = 1;
                tail.baseAtStart = 0.0;
                tail.label = "Segundo tramo";
                last.to = mid;
                if (tail.baseAtStart <= 0.0)
                    tail.baseAtStart = maximumBaseFor(p, tail.from.year);
                app.periods.push_back(tail);
            }
        }
        helpMarker("Para simular un cambio a mitad de camino: una bajada de sueldo, una "
                   "excedencia, una reducción de jornada o un tramo en paro.");
    }

    // --- parameters -------------------------------------------------------
    if (ImGui::CollapsingHeader("Parámetros e hipótesis")) {
        ImGui::TextColored(kDim, "IPC proyectado");
        helpMarker("Mueve tres cosas a la vez: la actualización de las bases antiguas, la "
                   "subida del tope máximo y de la base máxima, y el valor en euros de hoy. "
                   "El informe usa el 2,00 %, el objetivo del BCE.");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::DragScalar("##infl", ImGuiDataType_Double, &p.inflationPct, 0.02f, nullptr,
                          nullptr, "%.3f %%");

        ImGui::TextColored(kDim, "Tope máximo: IPC + x pp/año");
        helpMarker("El RDL 2/2023 sube la pensión máxima el IPC más 0,115 puntos cada año "
                   "hasta 2050. Es el dial que decide su pensión.");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::DragScalar("##capx", ImGuiDataType_Double, &p.maxPensionExtraPct, 0.005f, nullptr,
                          nullptr, "%.3f pp");

        ImGui::TextColored(kDim, "Base máxima: IPC + x pp/año");
        helpMarker("La base máxima de cotización sube el IPC más 1,2 puntos al año hasta 2050, "
                   "diez veces más rápido que el tope de la pensión. Esa tijera es la razón de "
                   "que cotizar más no se traduzca en más pensión.");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::DragScalar("##basex", ImGuiDataType_Double, &p.maximumBaseExtraPct, 0.01f, nullptr,
                          nullptr, "%.3f pp");

        ImGui::Spacing();
        ImGui::TextColored(kDim, "Anclajes de %d", p.anchorYear);
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::DragScalar("##cap26", ImGuiDataType_Double, &p.maxPensionAnchor, 1.0f, nullptr,
                          nullptr, "Tope: %.2f €");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::DragScalar("##bmax26", ImGuiDataType_Double, &p.maximumBaseAnchor, 1.0f, nullptr,
                          nullptr, "Base máx.: %.2f €");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::DragScalar("##bmin26", ImGuiDataType_Double, &p.minimumBaseAnchor, 1.0f, nullptr,
                          nullptr, "Base mín.: %.2f €");
        helpMarker("La base mínima es la que se usa para rellenar las lagunas: los 48 meses "
                   "más recientes al 100 % y el resto al 50 %.");

        ImGui::Spacing();
        ImGui::TextColored(kDim, "Meses anteriores a feb-2007");
        helpMarker("Los dos informes, juntos, detallan todas las bases desde febrero de 2007. "
                   "Sólo si adelanta mucho la jubilación el periodo de cómputo llega más atrás, "
                   "y esos meses hay que estimarlos: se toman como este porcentaje de la base "
                   "máxima de cada año. Donde se pudo contrastar —2007, 2008 y 2009— esa "
                   "estimación acertó al céntimo.");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::DragScalar("##early", ImGuiDataType_Double, &p.unknownEarlyBasePct, 0.5f, nullptr,
                          nullptr, "%.1f %% de la base máxima");

        ImGui::Spacing();
        ImGui::Checkbox("Cómputo dual (DT 40.ª)", &p.applyDualBaseReguladora);
        ImGui::Checkbox("Integrar lagunas", &p.integrateGaps);
        ImGui::Checkbox("Recortar a la base máxima", &p.clampToMaximumBase);
    }
}

void drawNotes(const Result& r) {
    if (r.notes.empty()) return;
    ImGui::Spacing();
    ImGui::SeparatorText("Advertencias del cálculo");
    for (const auto& n : r.notes) {
        ImGui::Bullet();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kWarn, "%s", n.c_str());
        ImGui::PopTextWrapPos();
    }
}

}  // namespace

void App::resetToReport() {
    // The baseline is whatever the data file says the official report assumed,
    // not a hard-coded one.
    params = person.params;
    periods = person.scenario;
    selectedPeriod = -1;
    recompute();
}

void App::recompute() {
    result = compute(params, periods);
    baseline = compute(person.params, person.scenario);
}

bool App::saveData() {
    pensioner::Pensioner snapshot = person;
    snapshot.params = params;
    snapshot.scenario = periods;
    std::string error;
    if (!pensioner::save(dataPath, snapshot, error)) {
        status = "No se pudo guardar: " + error;
        return false;
    }
    person = snapshot;
    status = "Guardado en " + dataPath;
    return true;
}

void applyStyle() {
    ImGui::StyleColorsDark();
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding = 4.0f;
    st.ChildRounding = 5.0f;
    st.FrameRounding = 3.0f;
    st.GrabRounding = 3.0f;
    st.ScrollbarRounding = 3.0f;
    st.FramePadding = ImVec2(6, 4);
    st.ItemSpacing = ImVec2(8, 6);
    st.CellPadding = ImVec2(7, 3);
    ImVec4* c = st.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.085f, 0.095f, 0.125f, 1.00f);
    c[ImGuiCol_ChildBg] = ImVec4(0.105f, 0.118f, 0.150f, 1.00f);
    c[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.11f, 0.14f, 0.98f);
    c[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.18f, 0.22f, 1.00f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.24f, 0.30f, 1.00f);
    c[ImGuiCol_Header] = ImVec4(0.18f, 0.38f, 0.52f, 0.55f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.24f, 0.52f, 0.70f, 0.70f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.24f, 0.52f, 0.70f, 0.90f);
    c[ImGuiCol_Tab] = ImVec4(0.13f, 0.15f, 0.19f, 1.00f);
    c[ImGuiCol_TabHovered] = ImVec4(0.24f, 0.52f, 0.70f, 0.80f);
    c[ImGuiCol_TabSelected] = ImVec4(0.19f, 0.42f, 0.57f, 1.00f);
    c[ImGuiCol_Button] = ImVec4(0.18f, 0.38f, 0.52f, 0.65f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.24f, 0.52f, 0.70f, 0.85f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.24f, 0.52f, 0.70f, 1.00f);
    c[ImGuiCol_SliderGrab] = ImVec4(0.30f, 0.65f, 0.85f, 1.00f);
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.40f, 0.75f, 0.95f, 1.00f);
    c[ImGuiCol_PlotLines] = ImVec4(0.30f, 0.65f, 0.85f, 1.00f);
    c[ImGuiCol_TableHeaderBg] = ImVec4(0.15f, 0.17f, 0.21f, 1.00f);
    c[ImGuiCol_TableBorderStrong] = ImVec4(0.26f, 0.29f, 0.35f, 1.00f);
    c[ImGuiCol_TableBorderLight] = ImVec4(0.19f, 0.21f, 0.26f, 1.00f);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.022f);
}

void loadFonts() {
    ImGuiIO& io = ImGui::GetIO();
    static const char* candidates[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
    };
    // Latin-1 covers the Spanish accents, but not the euro sign, the dashes or
    // the arrows this UI leans on, so spell the ranges out. The array has to
    // outlive the fonts, hence static.
    static const ImWchar kRanges[] = {
        0x0020, 0x00FF,  // basic Latin and Latin-1 supplement
        0x2010, 0x2027,  // dashes, curly quotes, ellipsis
        0x2190, 0x2193,  // arrows
        0x2212, 0x2212,  // true minus sign
        0x20AC, 0x20AC,  // euro
        0,
    };
    ImFontConfig cfg;
    cfg.GlyphRanges = kRanges;

    for (const char* path : candidates) {
        if (FILE* f = fopen(path, "rb")) {
            fclose(f);
            io.Fonts->AddFontFromFileTTF(path, 16.0f, &cfg);
            gBigFont = io.Fonts->AddFontFromFileTTF(path, 30.0f, &cfg);
            return;
        }
    }
    io.Fonts->AddFontDefault();
}

void drawUI(App& app) {
    app.recompute();

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImGui::TextColored(kAccent, "Simulador de jubilación — %s", app.person.identity.name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("· informe INSS de %s %s · nacido el %s",
                        app.person.identity.calcDate.c_str(),
                        app.person.identity.version.c_str(),
                        formatDate(app.person.params.birth).c_str());
    ImGui::SameLine();
    float right = ImGui::GetContentRegionMax().x - ImGui::GetFontSize() * 7.5f;
    if (right > ImGui::GetCursorPosX()) ImGui::SetCursorPosX(right);
    if (ImGui::SmallButton("Guardar datos")) app.saveData();
    if (ImGui::BeginItemTooltip()) {
        ImGui::Text("Escribe el escenario y los parámetros actuales en:");
        ImGui::TextUnformatted(app.dataPath.c_str());
        ImGui::EndTooltip();
    }
    if (!app.status.empty()) ImGui::TextDisabled("%s", app.status.c_str());
    ImGui::Separator();

    float sidebar = std::min(ImGui::GetContentRegionAvail().x * 0.33f,
                             ImGui::GetFontSize() * 23.0f);
    ImGui::BeginChild("##sidebar", ImVec2(sidebar, 0), false);
    drawScenarioPanel(app);
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##main", ImVec2(0, 0), false);

    bool forcing = app.forceTab >= 0;
    if (forcing) app.tab = (Tab)app.forceTab;
    const Tab wanted = app.tab;

    if (ImGui::BeginTabBar("##tabs")) {
        struct TabDef {
            const char* name;
            Tab id;
            void (*draw)(App&);
        };
        static const TabDef defs[] = {
            {"Resumen", Tab::Resumen, drawResumenTab},
            {"Informe", Tab::Informe, drawInformeTab},
            {"Vida laboral", Tab::VidaLaboral, drawVidaLaboralTab},
            {"Bases de cotización", Tab::Bases, drawBasesTab},
            {"Base reguladora", Tab::BaseReguladora, drawBaseReguladoraTab},
            {"Convenio especial", Tab::Convenio, drawConvenioTab},
            {"Proyecciones", Tab::Proyecciones, drawProyeccionesTab},
        };
        for (const auto& d : defs) {
            ImGuiTabItemFlags flags = (forcing && wanted == d.id) ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem(d.name, nullptr, flags)) {
                app.tab = d.id;
                ImGui::Spacing();
                ImGui::BeginChild("##tabbody");
                d.draw(app);
                drawNotes(app.result);
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    if (forcing) app.forceTab = -1;

    ImGui::EndChild();
    ImGui::End();
}
