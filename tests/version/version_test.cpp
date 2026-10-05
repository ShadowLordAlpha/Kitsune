#include "Kitsune/Version.h"

#include <cstdlib>
#include <iostream>

int main() {
    if (kitsune::kTargetMajor != 4 || kitsune::kTargetMinor != 10 ||
        kitsune::kTargetBuild != 75689) {
        std::cerr << "target build constants drifted\n";
        return EXIT_FAILURE;
    }
    if (kitsune::kTargetLabel != "4.10" || kitsune::targetLabel() != "4.10.0.75689") {
        std::cerr << "target label drifted\n";
        return EXIT_FAILURE;
    }
    if (kitsune::kDataBuild != 75689 || kitsune::kBaseBuild != 75689) {
        std::cerr << "data or base build drifted\n";
        return EXIT_FAILURE;
    }
    if (kitsune::kDataHash != "B89B5D6FA7CBF6452E721311BFBC6CB2" ||
        kitsune::kFixedHash != "2B2097DC4AD60A2D1E1F38691A1FF111" ||
        kitsune::kReplayHash != "6A60E59031A7DB1B272EE87E51E4C7CD") {
        std::cerr << "version hashes drifted\n";
        return EXIT_FAILURE;
    }
    if (kitsune::kLibraryVersion.empty()) {
        std::cerr << "library version empty\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
