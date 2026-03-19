#pragma once

#include <string>

#include "mil/models.hpp"

namespace mil {

constexpr const char* kDefaultCatalogUrl = "https://mega.nz/folder/XYch1SCA#v2FN9WflduOhK2ebVudzIw";
constexpr const char* kSettingsPath = "sdmc:/config/mil-manager/settings.ini";
constexpr const char* kReceiptsDir = "sdmc:/config/mil-manager/receipts";
constexpr const char* kCacheDir = "sdmc:/config/mil-manager/cache";
constexpr const char* kCatalogCachePath = "sdmc:/config/mil-manager/cache/index.json";
constexpr const char* kSwitchLocalIndexPath = "sdmc:/switch/mil_manager/index.json";
constexpr const char* kEmulatorInstalledTitlesPath = "sdmc:/switch/mil_manager/emulator-installed.json";

AppConfig LoadAppConfig(std::string& note);
bool SaveAppConfig(const AppConfig& config, std::string& error);
const char* LanguageModeLabel(LanguageMode mode);
const char* InstalledTitleScanModeLabel(InstalledTitleScanMode mode);

}  // namespace mil
