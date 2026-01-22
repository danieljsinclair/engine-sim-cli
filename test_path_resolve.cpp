#include <iostream>
#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;

int main() {
    // Test path resolution
    std::string scriptPath = "/Users/danielsinclair/vscode/engine-sim/assets/main.mr";
    std::string importRel = "../es/sound-library/impulse_responses.mr";
    
    fs::path script(scriptPath);
    fs::path rootDir = script.parent_path();
    fs::path importPath(importRel);
    fs::path fullPath = rootDir / importPath;
    
    std::cout << "Script: " << script << std::endl;
    std::cout << "Parent: " << rootDir << std::endl;
    std::cout << "Import: " << importPath << std::endl;
    std::cout << "Resolved: " << fullPath << std::endl;
    std::cout << "Normalized: " << fs::canonical(fullPath) << std::endl;
    std::cout << "Exists: " << (fs::exists(fullPath) ? "yes" : "no") << std::endl;
    
    return 0;
}
