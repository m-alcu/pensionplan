#include "pensioner.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace pensioner {
namespace {

using pension::Date;
using pension::Month;

// --- tiny text helpers ----------------------------------------------------

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    std::istringstream in(s);
    while (std::getline(in, cur, sep)) out.push_back(trim(cur));
    return out;
}

double toDouble(const std::string& s) { return std::strtod(s.c_str(), nullptr); }
long toLong(const std::string& s) { return std::strtol(s.c_str(), nullptr, 10); }

// Dates go in and out as dd/mm/yyyy, months as yyyy-mm: readable, and the same
// shape the reports use.
std::string fmtDate(Date d) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%02d/%02d/%04d", d.day, d.month, d.year);
    return buf;
}

Date parseDate(const std::string& s) {
    Date d;
    auto parts = split(s, '/');
    if (parts.size() == 3) {
        d.day = (int)toLong(parts[0]);
        d.month = (int)toLong(parts[1]);
        d.year = (int)toLong(parts[2]);
    }
    return d;
}

std::string fmtMonth(Month m) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%04d-%02d", m.year, m.month);
    return buf;
}

Month parseMonth(const std::string& s) {
    Month m;
    auto parts = split(s, '-');
    if (parts.size() == 2) {
        m.year = (int)toLong(parts[0]);
        m.month = (int)toLong(parts[1]);
    }
    return m;
}

std::string fmtNum(double v, int decimals = 2) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    return buf;
}

const char* kindName(pension::PeriodKind k) {
    switch (k) {
        case pension::PeriodKind::Contributing: return "cotizando";
        case pension::PeriodKind::Unemployment: return "desempleo";
        case pension::PeriodKind::Gap: return "sin_cotizar";
        case pension::PeriodKind::Convenio: return "convenio";
    }
    return "cotizando";
}

pension::PeriodKind parseKind(const std::string& s) {
    if (s == "desempleo") return pension::PeriodKind::Unemployment;
    if (s == "sin_cotizar") return pension::PeriodKind::Gap;
    if (s == "convenio") return pension::PeriodKind::Convenio;
    return pension::PeriodKind::Contributing;
}

// A field written as `clave = valor`, one per line, inside `[seccion]` blocks.
// List sections instead hold one unnamed row per line, fields split by '|'.
struct Writer {
    std::ofstream out;
    void section(const char* name) { out << "\n[" << name << "]\n"; }
    void kv(const char* k, const std::string& v) { out << k << " = " << v << "\n"; }
    void kv(const char* k, double v, int dec = 2) { kv(k, fmtNum(v, dec)); }
    void kv(const char* k, long v) { kv(k, std::to_string(v)); }
    void kv(const char* k, bool v) { kv(k, std::string(v ? "si" : "no")); }
};

bool parseBool(const std::string& s) { return s == "si" || s == "1" || s == "true"; }

}  // namespace

std::string Config::path() const {
    namespace fs = std::filesystem;
    fs::path dir(root.empty() ? "." : root);
    if (dir.is_relative() && !baseDir.empty()) dir = fs::path(baseDir) / dir;
    return (dir / dataFile).lexically_normal().string();
}

Config loadConfig(const std::string& iniPath) {
    namespace fs = std::filesystem;
    Config c;
    std::ifstream in(iniPath);
    if (!in) return c;
    c.source = iniPath;
    c.baseDir = fs::path(iniPath).parent_path().string();
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#' || line[0] == '[') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));
        if (key == "root") c.root = val;
        else if (key == "fichero") c.dataFile = val;
    }
    return c;
}

Config findConfig(const std::string& explicitPath, const std::string& exeDir) {
    namespace fs = std::filesystem;
    std::vector<std::string> candidates;
    if (!explicitPath.empty()) candidates.push_back(explicitPath);
    candidates.push_back("config.ini");
    if (!exeDir.empty()) {
        candidates.push_back((fs::path(exeDir) / "config.ini").string());
        candidates.push_back((fs::path(exeDir) / ".." / "config.ini").lexically_normal().string());
    }
    for (const auto& candidate : candidates) {
        std::error_code ec;
        if (!fs::exists(candidate, ec)) continue;
        Config c = loadConfig(candidate);
        if (!c.source.empty()) return c;
    }
    return Config{};
}

bool saveConfig(const std::string& iniPath, const Config& c, std::string& error) {
    std::ofstream out(iniPath);
    if (!out) {
        error = "no se puede escribir " + iniPath;
        return false;
    }
    out << "; Configuracion del simulador de jubilacion.\n"
        << "; root:    directorio raiz donde esta el fichero del pensionista.\n"
        << "; fichero: nombre de ese fichero dentro de root.\n"
        << "\n[rutas]\n"
        << "root = " << c.root << "\n"
        << "fichero = " << c.dataFile << "\n";
    return (bool)out;
}

const std::vector<std::string>& exampleSearchPaths() {
    static const std::vector<std::string> paths = {
        "examples/pensionista-ejemplo.dat",
        "../examples/pensionista-ejemplo.dat",
        "/usr/share/pensionplan/pensionista-ejemplo.dat",
    };
    return paths;
}

bool save(const std::string& path, const Pensioner& p, std::string& error) {
    // The root directory from config.ini need not exist yet on a first run.
    std::error_code ec;
    if (auto parent = std::filesystem::path(path).parent_path(); !parent.empty())
        std::filesystem::create_directories(parent, ec);

    Writer w;
    w.out.open(path);
    if (!w.out) {
        error = "no se puede escribir " + path;
        return false;
    }
    w.out << "; Datos del pensionista. Generado por pensionplan; se puede editar a mano.\n"
          << "; Fechas dd/mm/aaaa, meses aaaa-mm, decimales con punto.\n"
          << "\n[formato]\nversion = 1\n";

    w.section("titular");
    w.kv("nombre", p.identity.name);
    w.kv("dni", p.identity.nationalId);
    w.kv("regimen", p.identity.regime);
    w.kv("modalidad", p.identity.modality);
    w.kv("fecha_calculo", p.identity.calcDate);
    w.kv("version_informe", p.identity.version);
    w.kv("nacimiento", fmtDate(p.params.birth));

    w.section("informe");
    w.out << "; Lo que imprimio la simulacion oficial, para comparar.\n";
    w.kv("dias_vida_laboral", p.report.daysWholeLife);
    w.kv("dias_ultimos_15_anos", p.report.daysLast15Years);
    w.kv("anos_computables", p.report.yearsComputable);
    w.kv("base_reguladora", p.report.baseReguladora);
    w.kv("suma_bases", p.report.baseReguladoraSum);
    w.kv("porcentaje_total", p.report.percentTotal, 4);
    w.kv("calculo_inicial", p.report.calcInitial);
    w.kv("pension_maxima", p.report.maxPension);
    w.kv("exceso", p.report.excess);
    w.kv("pension", p.report.pension);
    w.kv("pension_deflactada", p.report.pensionDeflated);

    const pension::Params& q = p.params;
    w.section("parametros");
    w.kv("jubilacion", fmtDate(q.retirement));
    w.kv("ipc", q.inflationPct, 3);
    w.kv("tope_extra_pp", q.maxPensionExtraPct, 3);
    w.kv("base_maxima_extra_pp", q.maximumBaseExtraPct, 3);
    w.kv("ano_ancla", (long)q.anchorYear);
    w.kv("tope_ancla", q.maxPensionAnchor);
    w.kv("base_maxima_ancla", q.maximumBaseAnchor);
    w.kv("base_minima_ancla", q.minimumBaseAnchor);
    w.kv("convenio_tipo_pct", q.convenioRatePct, 3);
    w.kv("convenio_coeficiente", q.convenioCoefficient, 3);
    w.kv("convenio_base_minima", q.convenioFloorBase);
    w.kv("base_estimada_pct", q.unknownEarlyBasePct, 2);
    w.kv("anticipada_voluntaria", q.voluntaryEarlyRetirement);
    w.kv("computo_dual", q.applyDualBaseReguladora);
    w.kv("integrar_lagunas", q.integrateGaps);
    w.kv("recortar_base_maxima", q.clampToMaximumBase);
    w.kv("cancelar_convenio_antes", q.trimConvenioWhenPossible);

    w.section("vida_laboral");
    w.out << "; empresa | detalle | alta | baja | dias | estimado\n";
    w.kv("dias_superpuestos", (long)p.data.overlappingDays);
    w.kv("traspaso", fmtMonth(p.data.handover));
    for (const auto& x : p.data.workPeriods)
        w.out << "periodo = " << x.employer << " | " << x.detail << " | " << fmtDate(x.from)
              << " | " << fmtDate(x.to) << " | " << x.days << " | " << (x.estimated ? "si" : "no")
              << "\n";

    w.section("escenario");
    w.out << "; desde | hasta | situacion | base | subida_anual | seguir_base_maxima | etiqueta\n";
    for (const auto& x : p.scenario)
        w.out << "periodo = " << fmtDate(x.from) << " | " << fmtDate(x.to) << " | "
              << kindName(x.kind) << " | " << fmtNum(x.baseAtStart) << " | "
              << fmtNum(x.annualGrowthPct, 3) << " | " << (x.followMaximumBase ? "si" : "no")
              << " | " << x.label << "\n";

    w.section("bases_cotizacion");
    w.out << "; Bases de cotizacion sobre las que hay registro, una por mes.\n";
    for (const auto& [m, v] : p.data.historicBases)
        w.out << fmtMonth(m) << " = " << fmtNum(v) << "\n";

    w.section("indice_precios");
    w.out << "; IPC mensual publicado, normalizado al ultimo mes de la serie.\n";
    for (const auto& [m, v] : p.data.priceIndex)
        w.out << fmtMonth(m) << " = " << fmtNum(v, 8) << "\n";

    return (bool)w.out;
}

bool load(const std::string& path, Pensioner& out, std::string& error) {
    std::ifstream in(path);
    if (!in) {
        error = "no existe " + path;
        return false;
    }
    Pensioner p;
    pension::Params& q = p.params;
    std::string line, section;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[') {
            section = line.substr(1, line.find(']') - 1);
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(line.substr(0, eq));
        const std::string val = trim(line.substr(eq + 1));

        if (section == "titular") {
            if (key == "nombre") p.identity.name = val;
            else if (key == "dni") p.identity.nationalId = val;
            else if (key == "regimen") p.identity.regime = val;
            else if (key == "modalidad") p.identity.modality = val;
            else if (key == "fecha_calculo") p.identity.calcDate = val;
            else if (key == "version_informe") p.identity.version = val;
            else if (key == "nacimiento") q.birth = parseDate(val);
        } else if (section == "informe") {
            if (key == "dias_vida_laboral") p.report.daysWholeLife = toLong(val);
            else if (key == "dias_ultimos_15_anos") p.report.daysLast15Years = toLong(val);
            else if (key == "anos_computables") p.report.yearsComputable = toDouble(val);
            else if (key == "base_reguladora") p.report.baseReguladora = toDouble(val);
            else if (key == "suma_bases") p.report.baseReguladoraSum = toDouble(val);
            else if (key == "porcentaje_total") p.report.percentTotal = toDouble(val);
            else if (key == "calculo_inicial") p.report.calcInitial = toDouble(val);
            else if (key == "pension_maxima") p.report.maxPension = toDouble(val);
            else if (key == "exceso") p.report.excess = toDouble(val);
            else if (key == "pension") p.report.pension = toDouble(val);
            else if (key == "pension_deflactada") p.report.pensionDeflated = toDouble(val);
        } else if (section == "parametros") {
            if (key == "jubilacion") q.retirement = parseDate(val);
            else if (key == "ipc") q.inflationPct = toDouble(val);
            else if (key == "tope_extra_pp") q.maxPensionExtraPct = toDouble(val);
            else if (key == "base_maxima_extra_pp") q.maximumBaseExtraPct = toDouble(val);
            else if (key == "ano_ancla") q.anchorYear = (int)toLong(val);
            else if (key == "tope_ancla") q.maxPensionAnchor = toDouble(val);
            else if (key == "base_maxima_ancla") q.maximumBaseAnchor = toDouble(val);
            else if (key == "base_minima_ancla") q.minimumBaseAnchor = toDouble(val);
            else if (key == "convenio_tipo_pct") q.convenioRatePct = toDouble(val);
            else if (key == "convenio_coeficiente") q.convenioCoefficient = toDouble(val);
            else if (key == "convenio_base_minima") q.convenioFloorBase = toDouble(val);
            else if (key == "base_estimada_pct") q.unknownEarlyBasePct = toDouble(val);
            else if (key == "anticipada_voluntaria") q.voluntaryEarlyRetirement = parseBool(val);
            else if (key == "computo_dual") q.applyDualBaseReguladora = parseBool(val);
            else if (key == "integrar_lagunas") q.integrateGaps = parseBool(val);
            else if (key == "recortar_base_maxima") q.clampToMaximumBase = parseBool(val);
            else if (key == "cancelar_convenio_antes") q.trimConvenioWhenPossible = parseBool(val);
        } else if (section == "vida_laboral") {
            if (key == "dias_superpuestos") p.data.overlappingDays = (int)toLong(val);
            else if (key == "traspaso") p.data.handover = parseMonth(val);
            else if (key == "periodo") {
                auto f = split(val, '|');
                if (f.size() >= 6)
                    p.data.workPeriods.push_back({f[0], f[1], parseDate(f[2]), parseDate(f[3]),
                                                 (int)toLong(f[4]), parseBool(f[5])});
            }
        } else if (section == "escenario") {
            if (key == "periodo") {
                auto f = split(val, '|');
                if (f.size() >= 6) {
                    pension::ScenarioPeriod sp;
                    sp.from = parseDate(f[0]);
                    sp.to = parseDate(f[1]);
                    sp.kind = parseKind(f[2]);
                    sp.baseAtStart = toDouble(f[3]);
                    sp.annualGrowthPct = toDouble(f[4]);
                    sp.followMaximumBase = parseBool(f[5]);
                    sp.label = f.size() > 6 ? f[6] : "";
                    p.scenario.push_back(sp);
                }
            }
        } else if (section == "bases_cotizacion") {
            p.data.historicBases.push_back({parseMonth(key), toDouble(val)});
        } else if (section == "indice_precios") {
            p.data.priceIndex.push_back({parseMonth(key), toDouble(val)});
        }
    }

    if (p.data.historicBases.empty() || p.data.priceIndex.empty() ||
        p.data.workPeriods.empty()) {
        error = path + ": faltan las bases de cotizacion, el IPC o la vida laboral";
        return false;
    }
    if (p.scenario.empty()) {
        error = path + ": no hay ningun periodo en [escenario]";
        return false;
    }
    out = p;
    return true;
}

}  // namespace pensioner
