// Everything about the person, and how it gets to and from disk.
//
// Nothing about a particular pensioner belongs in the binary. The program reads
// `config.ini` to find the root directory, loads `<root>/<fichero>` -- a plain
// text .dat holding the identity, the working life, every contribution base on
// record, the published CPI, the figures the official report printed, and the
// scenario being simulated -- and writes it back when it changes.
//
// Nothing identifying is compiled in. When the configured file is missing the
// program falls back to the worked example shipped in examples/, and says so.
#pragma once

#include <string>
#include <vector>
#include "pension.h"

namespace pensioner {

struct Identity {
    std::string name;
    std::string nationalId;
    std::string regime;
    std::string modality;
    std::string calcDate;  // when the official simulation was drawn up
    std::string version;
};

// What the official report printed, kept so the UI can show it beside the
// scenario and so the engine can be checked against it.
struct Headline {
    long daysWholeLife = 0;
    long daysLast15Years = 0;
    double yearsComputable = 0;
    double baseReguladora = 0;
    double baseReguladoraSum = 0;
    double percentTotal = 0;
    double calcInitial = 0;
    double maxPension = 0;
    double excess = 0;
    double pension = 0;
    double pensionDeflated = 0;
};

struct Pensioner {
    Identity identity;
    Headline report;
    pension::Dataset data;
    pension::Params params;
    std::vector<pension::ScenarioPeriod> scenario;
};

// Plain text, section/key based. `error` is filled in on failure.
bool load(const std::string& path, Pensioner& out, std::string& error);
bool save(const std::string& path, const Pensioner& p, std::string& error);

// config.ini: where the data file lives.
struct Config {
    std::string root = "data";
    std::string dataFile = "pensionista.dat";

    // Where config.ini was found. A relative `root` is resolved against this,
    // not against the working directory, so the program finds its data whether
    // it is launched from the source tree, from build/, or from a desktop
    // shortcut.
    std::string baseDir;
    std::string source;  // the ini actually read, empty when none was

    std::string path() const;
};

// Missing or unreadable file yields the defaults above, which is not an error.
Config loadConfig(const std::string& iniPath);

// config.ini as looked for in order: the explicit path if given, the working
// directory, beside the executable, and one level above it -- which is where it
// sits when the binary lives in build/.
Config findConfig(const std::string& explicitPath, const std::string& exeDir);

// Where the shipped example lives, tried in turn so the program runs from the
// source tree, from a build directory, or from an install.
const std::vector<std::string>& exampleSearchPaths();
bool saveConfig(const std::string& iniPath, const Config& c, std::string& error);

}  // namespace pensioner
