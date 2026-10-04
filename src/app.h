// Shared state for the pension simulator UI.
#pragma once
#include <string>
#include <vector>
#include "pension.h"
#include "pensioner.h"

enum class Tab {
    Resumen, Informe, VidaLaboral, Bases, BaseReguladora, Convenio, Proyecciones
};

// A named starting point the user can jump back to.
struct Preset {
    const char* name;
    const char* blurb;
};

struct App {
    Tab tab = Tab::Resumen;

    // Everything about the person, as loaded from the data file. `params` and
    // `periods` are the live copies the UI edits; `person` keeps the rest and
    // the baseline the Resumen tab compares against.
    pensioner::Pensioner person;
    pensioner::Config config;
    std::string dataPath;
    std::string status;  // last load/save message, shown in the header

    pension::Params params;
    std::vector<pension::ScenarioPeriod> periods;

    // Recomputed every frame: the live scenario, and the report's own
    // assumptions to compare it against.
    pension::Result result;
    pension::Result baseline;

    int selectedPeriod = -1;
    int basesYearFrom = 2010;   // first year shown in the bases grid
    bool basesShowAll = true;
    bool showOnlyWindow = false;

    // Convenio especial tab: the date work stops and the convenio takes over.
    pension::Date convenioStop{2030, 7, 10};
    double convenioGrowthPct = 2.0;

    // Proyecciones tab
    int sweepFromYear = 2033;
    int sweepToYear = 2042;
    bool sweepHoldCareer = false;

    // debug/testing hooks (env-driven, see main.cpp)
    const char* shotPath = nullptr;
    int forceTab = -1;

    void resetToReport();
    void recompute();
    bool saveData();
};

void applyStyle();
void loadFonts();
void drawUI(App& app);

// Formatting helpers shared by the tabs: Spanish conventions, so 4.055,96.
std::string eur(double v, int decimals = 2);
std::string pct(double v, int decimals = 4);
std::string intThousands(long v);
