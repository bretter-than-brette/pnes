/**
 * @file exporter_main.cpp
 * @brief `animator-export`: exporter::run plus argv, with no Qt and no display
 *        server, which is the whole reason it is a target of its own.
 */
#include "exporter.hpp"

int main(int argc, char** argv) {
    exporter::Options options;
    switch (exporter::parseArgs(argc, argv, options, /*exportImplied=*/true)) {
        case exporter::Parse::Export:
            return exporter::run(options);
        case exporter::Parse::Help:
            return 0;
        case exporter::Parse::Error:
        case exporter::Parse::NoExport:
            break;
    }
    return 1;
}
