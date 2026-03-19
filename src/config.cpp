#include "mil/config.hpp"

#include <fstream>
#include <sys/stat.h>

namespace mil {

namespace {

bool EnsureDirectory(const std::string& path) {
    if (path.empty()) {
        return false;
    }

    std::string current;
    for (std::size_t index = 0; index < path.size(); ++index) {
        current.push_back(path[index]);
        if (path[index] == '/' || path[index] == ':') {
            continue;
        }
        const bool isLast = index + 1 == path.size();
        const bool isSeparator = !isLast && path[index + 1] == '/';
        if (!isLast && !isSeparator) {
            continue;
        }
        mkdir(current.c_str(), 0777);
    }
    return true;
}

}  // namespace

AppConfig LoadAppConfig(std::string& note) {
    AppConfig config;
    config.catalogUrls.push_back(kDefaultCatalogUrl);

    std::ifstream input(kSettingsPath);
    if (!input.good()) {
        note = "Usando configuracao padrao.";
        return config;
    }

    config.catalogUrls.clear();
    std::string line;
    while (std::getline(input, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[') {
            continue;
        }
        const auto equalsPos = line.find('=');
        if (equalsPos == std::string::npos) {
            continue;
        }
        const std::string key = ToLowerAscii(Trim(line.substr(0, equalsPos)));
        const std::string value = Trim(line.substr(equalsPos + 1));
        if (key == "language") {
            const std::string language = ToLowerAscii(value);
            if (language == "pt-br" || language == "ptbr") {
                config.language = LanguageMode::PtBr;
            } else if (language == "en-us" || language == "enus") {
                config.language = LanguageMode::EnUs;
            } else {
                config.language = LanguageMode::Auto;
            }
        } else if (key == "scan_mode") {
            const std::string scanMode = ToLowerAscii(value);
            if (scanMode == "full") {
                config.scanMode = InstalledTitleScanMode::Full;
            } else if (scanMode == "catalog" || scanMode == "catalog_probe") {
                config.scanMode = InstalledTitleScanMode::CatalogProbe;
            } else if (scanMode == "off" || scanMode == "disabled") {
                config.scanMode = InstalledTitleScanMode::Disabled;
            } else {
                config.scanMode = InstalledTitleScanMode::Auto;
            }
        } else if (key == "catalog_url" || key == "url") {
            if (!value.empty()) {
                config.catalogUrls.push_back(value);
            }
        }
    }

    if (config.catalogUrls.empty()) {
        config.catalogUrls.push_back(kDefaultCatalogUrl);
    }

    note = "Configuracao carregada de sdmc:/config/mil-manager/settings.ini";
    return config;
}

bool SaveAppConfig(const AppConfig& config, std::string& error) {
    EnsureDirectory("sdmc:/config");
    EnsureDirectory("sdmc:/config/mil-manager");

    std::ofstream output(kSettingsPath, std::ios::trunc);
    if (!output.good()) {
        error = "Nao foi possivel salvar settings.ini";
        return false;
    }

    output << "# Gerenciador MIL Traducoes\n";
    output << "language=";
    switch (config.language) {
        case LanguageMode::PtBr:
            output << "pt-BR\n";
            break;
        case LanguageMode::EnUs:
            output << "en-US\n";
            break;
        case LanguageMode::Auto:
        default:
            output << "auto\n";
            break;
    }
    output << "scan_mode=";
    switch (config.scanMode) {
        case InstalledTitleScanMode::Full:
            output << "full\n";
            break;
        case InstalledTitleScanMode::CatalogProbe:
            output << "catalog\n";
            break;
        case InstalledTitleScanMode::Disabled:
            output << "off\n";
            break;
        case InstalledTitleScanMode::Auto:
        default:
            output << "auto\n";
            break;
    }

    for (const std::string& url : config.catalogUrls) {
        output << "catalog_url=" << url << '\n';
    }

    if (!output.good()) {
        error = "Falha ao escrever settings.ini";
        return false;
    }

    return true;
}

const char* LanguageModeLabel(LanguageMode mode) {
    switch (mode) {
        case LanguageMode::PtBr:
            return "pt-BR";
        case LanguageMode::EnUs:
            return "en-US";
        case LanguageMode::Auto:
        default:
            return "auto";
    }
}

const char* InstalledTitleScanModeLabel(InstalledTitleScanMode mode) {
    switch (mode) {
        case InstalledTitleScanMode::Full:
            return "full";
        case InstalledTitleScanMode::CatalogProbe:
            return "catalog";
        case InstalledTitleScanMode::Disabled:
            return "off";
        case InstalledTitleScanMode::Auto:
        default:
            return "auto";
    }
}

}  // namespace mil
