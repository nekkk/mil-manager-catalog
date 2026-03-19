#pragma once

#include <string>
#include <vector>

#include <switch.h>

#include "mil/models.hpp"

namespace mil {

struct PlatformSession {
    Framebuffer framebuffer{};
    bool framebufferReady = false;
    bool nifmReady = false;
    bool socketReady = false;
    bool romfsReady = false;
};

bool InitializePlatform(PlatformSession& session, std::string& note);
void ShutdownPlatform(PlatformSession& session);
std::vector<InstalledTitle> LoadInstalledTitles(const AppConfig& config, const CatalogIndex* catalog, std::string& note);
std::string GetPreferredLanguageCode();
bool IsEmulatorEnvironment();

}  // namespace mil
