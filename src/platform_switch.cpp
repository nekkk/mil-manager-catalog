#include "mil/platform.hpp"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <vector>

#include <switch.h>

#include "mil/config.hpp"
#include "picojson.h"

namespace mil {

namespace {

constexpr u32 kFramebufferWidth = 1280;
constexpr u32 kFramebufferHeight = 720;

std::string SafeStringCopy(const char* source, std::size_t maxLength) {
    if (!source || maxLength == 0) {
        return {};
    }
    std::size_t length = 0;
    while (length < maxLength && source[length] != '\0') {
        ++length;
    }
    return std::string(source, length);
}

std::string GetLoaderInfoString() {
    const char* info = envGetLoaderInfo();
    if (info == nullptr) {
        return {};
    }

    const u64 size = envGetLoaderInfoSize();
    if (size == 0) {
        return std::string(info);
    }
    return std::string(info, static_cast<std::size_t>(size));
}

bool IsLikelyEmulatorLoader() {
    const std::string loaderInfo = ToLowerAscii(GetLoaderInfoString());
    return loaderInfo.find("ryujinx") != std::string::npos ||
           loaderInfo.find("yuzu") != std::string::npos ||
           loaderInfo.find("suyu") != std::string::npos ||
           loaderInfo.find("torzu") != std::string::npos;
}

bool HasImportedTitlesFile() {
    std::ifstream input(kEmulatorInstalledTitlesPath);
    return input.good();
}

std::string StripUtf8Bom(std::string value) {
    if (value.size() >= 3 &&
        static_cast<unsigned char>(value[0]) == 0xEF &&
        static_cast<unsigned char>(value[1]) == 0xBB &&
        static_cast<unsigned char>(value[2]) == 0xBF) {
        value.erase(0, 3);
    }
    return value;
}

std::vector<InstalledTitle> LoadInstalledTitlesFromImportedFile(std::string& note) {
    std::vector<InstalledTitle> titles;
    std::ifstream input(kEmulatorInstalledTitlesPath);
    if (!input.good()) {
        note = "Biblioteca do emulador nao fica visivel ao homebrew. Exporte os title IDs para " +
               std::string(kEmulatorInstalledTitlesPath);
        return titles;
    }

    std::string json((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    json = StripUtf8Bom(std::move(json));
    picojson::value root;
    const std::string parseError = picojson::parse(root, json);
    if (!parseError.empty() || !root.is<picojson::object>()) {
        note = "Arquivo emulator-installed.json invalido.";
        return {};
    }

    const auto& object = root.get<picojson::object>();
    const auto titlesIt = object.find("titles");
    if (titlesIt == object.end() || !titlesIt->second.is<picojson::array>()) {
        note = "Arquivo emulator-installed.json sem array titles.";
        return {};
    }

    for (const picojson::value& item : titlesIt->second.get<picojson::array>()) {
        if (!item.is<picojson::object>()) {
            continue;
        }
        const auto& titleObject = item.get<picojson::object>();
        const auto titleIdIt = titleObject.find("titleId");
        if (titleIdIt == titleObject.end() || !titleIdIt->second.is<std::string>()) {
            continue;
        }

        InstalledTitle title;
        title.titleIdHex = ToLowerAscii(titleIdIt->second.get<std::string>());
        title.applicationId = std::strtoull(title.titleIdHex.c_str(), nullptr, 16);
        title.name = "Title " + title.titleIdHex;

        const auto nameIt = titleObject.find("name");
        if (nameIt != titleObject.end() && nameIt->second.is<std::string>()) {
            title.name = nameIt->second.get<std::string>();
        }
        const auto versionIt = titleObject.find("displayVersion");
        if (versionIt != titleObject.end() && versionIt->second.is<std::string>()) {
            title.displayVersion = versionIt->second.get<std::string>();
        }
        title.metadataAvailable = !title.name.empty();
        titles.push_back(title);
    }

    note = "Titulos importados de emulator-installed.json.";
    return titles;
}

bool TryReadTitleMetadata(std::uint64_t applicationId, InstalledTitle& title) {
    NsApplicationControlData controlData{};
    u64 actualSize = 0;
    const Result controlResult = nsGetApplicationControlData(NsApplicationControlSource_Storage,
                                                             applicationId,
                                                             &controlData,
                                                             sizeof(controlData),
                                                             &actualSize);
    if (R_FAILED(controlResult) || actualSize < sizeof(controlData.nacp)) {
        return false;
    }

    title.applicationId = applicationId;
    title.titleIdHex = FormatTitleId(applicationId);

    NacpLanguageEntry* languageEntry = nullptr;
    if (R_SUCCEEDED(nacpGetLanguageEntry(&controlData.nacp, &languageEntry)) && languageEntry != nullptr) {
        title.name = SafeStringCopy(languageEntry->name, sizeof(languageEntry->name));
        title.publisher = SafeStringCopy(languageEntry->author, sizeof(languageEntry->author));
    }
    title.displayVersion = SafeStringCopy(controlData.nacp.display_version, sizeof(controlData.nacp.display_version));
    title.metadataAvailable = !title.name.empty();

    if (title.name.empty()) {
        title.name = "Title " + title.titleIdHex;
    }
    return true;
}

std::vector<InstalledTitle> LoadInstalledTitlesFull(std::string& note) {
    std::vector<InstalledTitle> titles;

    const Result initResult = nsInitialize();
    if (R_FAILED(initResult)) {
        note = "nsInitialize falhou. Servico NS indisponivel.";
        return titles;
    }

    std::vector<NsApplicationRecord> records(64);
    s32 offset = 0;
    s32 entryCount = 0;

    do {
        entryCount = 0;
        const Result listResult = nsListApplicationRecord(records.data(),
                                                          static_cast<s32>(records.size()),
                                                          offset,
                                                          &entryCount);
        if (R_FAILED(listResult)) {
            note = "nsListApplicationRecord falhou no modo full.";
            nsExit();
            return {};
        }

        for (s32 index = 0; index < entryCount; ++index) {
            InstalledTitle title;
            if (TryReadTitleMetadata(records[index].application_id, title)) {
                titles.push_back(title);
            }
        }

        offset += entryCount;
    } while (entryCount > 0);

    nsExit();
    note = "Titulos instalados carregados por scan completo.";
    return titles;
}

std::vector<InstalledTitle> LoadInstalledTitlesFromCatalog(const CatalogIndex& catalog, std::string& note) {
    std::vector<InstalledTitle> titles;
    std::set<std::string> uniqueTitleIds;

    for (const CatalogEntry& entry : catalog.entries) {
        if (!entry.titleId.empty()) {
            uniqueTitleIds.insert(ToLowerAscii(entry.titleId));
        }
    }

    if (uniqueTitleIds.empty()) {
        note = "Catalogo sem title IDs para sondagem local.";
        return titles;
    }

    const Result initResult = nsInitialize();
    if (R_FAILED(initResult)) {
        note = "nsInitialize falhou. Emulador ou servico NS indisponivel.";
        return titles;
    }

    for (const std::string& titleIdHex : uniqueTitleIds) {
        const std::uint64_t applicationId = std::strtoull(titleIdHex.c_str(), nullptr, 16);
        InstalledTitle title;
        if (TryReadTitleMetadata(applicationId, title)) {
            titles.push_back(title);
        }
    }

    nsExit();
    note = "Titulos detectados por sondagem do catalogo.";
    return titles;
}

}  // namespace

bool InitializePlatform(PlatformSession& session, std::string& note) {
    if (R_SUCCEEDED(nifmInitialize(NifmServiceType_User))) {
        session.nifmReady = true;
    }

    socketInitializeDefault();
    session.socketReady = true;

    if (R_SUCCEEDED(framebufferCreate(&session.framebuffer,
                                      nwindowGetDefault(),
                                      kFramebufferWidth,
                                      kFramebufferHeight,
                                      PIXEL_FORMAT_RGBA_8888,
                                      2)) &&
        R_SUCCEEDED(framebufferMakeLinear(&session.framebuffer))) {
        session.framebufferReady = true;
    }

    if (R_SUCCEEDED(romfsInit())) {
        session.romfsReady = true;
    }

#if defined(MIL_ENABLE_NXLINK)
    nxlinkStdio();
#endif

    if (session.nifmReady) {
        note = session.romfsReady ? "Rede, video, sockets e RomFS inicializados."
                                  : "Rede, video e sockets inicializados. RomFS indisponivel.";
    } else {
        note = session.romfsReady ? "Video, sockets e RomFS inicializados. nifm indisponivel."
                                  : "Video e sockets inicializados. RomFS e nifm indisponiveis.";
    }
    return true;
}

void ShutdownPlatform(PlatformSession& session) {
    if (session.framebufferReady) {
        framebufferClose(&session.framebuffer);
        session.framebufferReady = false;
    }
    if (session.romfsReady) {
        romfsExit();
        session.romfsReady = false;
    }
    if (session.socketReady) {
        socketExit();
        session.socketReady = false;
    }
    if (session.nifmReady) {
        nifmExit();
        session.nifmReady = false;
    }
}

std::vector<InstalledTitle> LoadInstalledTitles(const AppConfig& config, const CatalogIndex* catalog, std::string& note) {
    if (config.scanMode == InstalledTitleScanMode::Disabled) {
        note = "Scan de titulos desativado em settings.ini.";
        return {};
    }

    if (HasImportedTitlesFile()) {
        return LoadInstalledTitlesFromImportedFile(note);
    }

    if (IsLikelyEmulatorLoader()) {
        note = "Emulador detectado. A biblioteca host do Ryujinx nao e exposta ao homebrew; use emulator-installed.json.";
        return {};
    }

    if (config.scanMode == InstalledTitleScanMode::Full) {
        return LoadInstalledTitlesFull(note);
    }

    if (config.scanMode == InstalledTitleScanMode::CatalogProbe) {
        if (catalog == nullptr) {
            note = "Catalogo indisponivel. Sondagem local ignorada.";
            return {};
        }
        return LoadInstalledTitlesFromCatalog(*catalog, note);
    }

    std::vector<InstalledTitle> titles = LoadInstalledTitlesFull(note);
    if (!titles.empty()) {
        note = "Modo automatico. " + note;
        return titles;
    }

    if (catalog == nullptr) {
        note = "Catalogo indisponivel. Scan automatico sem sondagem local.";
        return {};
    }

    titles = LoadInstalledTitlesFromCatalog(*catalog, note);
    note = "Modo automatico com sondagem do catalogo. " + note;
    return titles;
}

std::string GetPreferredLanguageCode() {
    Result rc = setInitialize();
    if (R_FAILED(rc)) {
        return "en-US";
    }

    u64 languageCode = 0;
    rc = setGetSystemLanguage(&languageCode);
    setExit();
    if (R_FAILED(rc)) {
        return "en-US";
    }

    const char* languageChars = reinterpret_cast<const char*>(&languageCode);
    return std::string(languageChars, 4);
}

bool IsEmulatorEnvironment() {
    return IsLikelyEmulatorLoader();
}

}  // namespace mil
