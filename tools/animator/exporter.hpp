/**
 * @file exporter.hpp
 * @brief Headless project export -- what File > Export All Headers does, driven
 *        by argv, so the build can keep demo/animations/gen/ in step.
 */
#pragma once

#include <filesystem>

namespace exporter {

constexpr const char* kDefaultOutputDir = "gen";

struct Options {
    std::filesystem::path project;

    std::filesystem::path output;

    bool verbose = false;
};

enum class Parse {
    NoExport,  ///< argv does not ask for an export: carry on with the GUI.
    Export,    ///< Run ::run with the filled-in Options.
    Help,      ///< Usage was asked for and printed; exit 0.
    Error,     ///< Usage was wrong and the reason printed; exit 1.
};

Parse parseArgs(int argc, char** argv, Options& options, bool exportImplied,
                const char* extraUsage = nullptr);

void usage(const char* program, const char* extraUsage = nullptr);

int run(const Options& options);

} // namespace exporter
