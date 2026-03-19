#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace mil {

enum class ContentSection {
    Translations,
    ModsTools,
    Cheats,
    SaveGames,
    About,
};

enum class LanguageMode {
    Auto,
    PtBr,
    EnUs,
};

enum class InstalledTitleScanMode {
    Auto,
    Full,
    CatalogProbe,
    Disabled,
};

struct CompatibilityRule {
    std::string minGameVersion;
    std::string maxGameVersion;
    std::vector<std::string> exactGameVersions;
};

struct CatalogEntry {
    std::string id;
    std::string titleId;
    std::string name;
    std::string summary;
    std::string author;
    std::string packageVersion;
    std::string contentRevision;
    std::string language;
    std::string downloadUrl;
    std::string detailsUrl;
    std::vector<std::string> tags;
    CompatibilityRule compatibility;
    ContentSection section = ContentSection::Translations;
    bool featured = false;
};

struct CatalogIndex {
    std::string catalogName;
    std::string catalogRevision;
    std::string channel;
    std::string schemaVersion;
    std::string generatedAt;
    std::vector<CatalogEntry> entries;
};

struct InstalledTitle {
    std::uint64_t applicationId = 0;
    std::string titleIdHex;
    std::string name;
    std::string publisher;
    std::string displayVersion;
    bool metadataAvailable = false;
};

struct InstallReceipt {
    std::string packageId;
    std::string packageVersion;
    std::string titleId;
    std::string installRoot;
    std::string sourceUrl;
    std::string installedAt;
    std::string gameVersion;
    std::vector<std::string> files;
};

struct AppConfig {
    LanguageMode language = LanguageMode::Auto;
    InstalledTitleScanMode scanMode = InstalledTitleScanMode::Auto;
    std::vector<std::string> catalogUrls;
};

inline std::string ToLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

inline std::string Trim(std::string value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

inline std::string FormatTitleId(std::uint64_t value) {
    std::ostringstream stream;
    stream.setf(std::ios::hex, std::ios::basefield);
    stream.setf(std::ios::uppercase);
    stream.width(16);
    stream.fill('0');
    stream << value;
    return stream.str();
}

inline ContentSection ParseSection(const std::string& rawValue) {
    const std::string value = ToLowerAscii(rawValue);
    if (value == "translations" || value == "translation" || value == "dubs" || value == "dub" ||
        value == "traducao" || value == "traducoes") {
        return ContentSection::Translations;
    }
    if (value == "mods" || value == "mod" || value == "tools" || value == "mods-tools") {
        return ContentSection::ModsTools;
    }
    if (value == "cheats" || value == "cheat") {
        return ContentSection::Cheats;
    }
    if (value == "savegames" || value == "save" || value == "saves") {
        return ContentSection::SaveGames;
    }
    return ContentSection::About;
}

inline const char* SectionLabel(ContentSection section) {
    switch (section) {
        case ContentSection::Translations:
            return "Traducoes & Dublagens";
        case ContentSection::ModsTools:
            return "Mods & Tools";
        case ContentSection::Cheats:
            return "Cheats";
        case ContentSection::SaveGames:
            return "Save Games";
        case ContentSection::About:
        default:
            return "Sobre a M.I.L.";
    }
}

inline std::vector<int> ParseVersionTokens(const std::string& version) {
    std::vector<int> tokens;
    std::string current;
    for (char ch : version) {
        if (std::isdigit(static_cast<unsigned char>(ch))) {
            current.push_back(ch);
        } else if (!current.empty()) {
            tokens.push_back(std::stoi(current));
            current.clear();
        }
    }
    if (!current.empty()) {
        tokens.push_back(std::stoi(current));
    }
    return tokens;
}

inline int CompareGameVersion(const std::string& left, const std::string& right) {
    const auto leftTokens = ParseVersionTokens(left);
    const auto rightTokens = ParseVersionTokens(right);
    const std::size_t count = std::max(leftTokens.size(), rightTokens.size());
    for (std::size_t index = 0; index < count; ++index) {
        const int lhs = index < leftTokens.size() ? leftTokens[index] : 0;
        const int rhs = index < rightTokens.size() ? rightTokens[index] : 0;
        if (lhs < rhs) {
            return -1;
        }
        if (lhs > rhs) {
            return 1;
        }
    }
    return 0;
}

inline bool MatchesCompatibility(const CompatibilityRule& rule, const std::string& gameVersion) {
    if (gameVersion.empty()) {
        return true;
    }
    if (!rule.exactGameVersions.empty()) {
        return std::find(rule.exactGameVersions.begin(), rule.exactGameVersions.end(), gameVersion) != rule.exactGameVersions.end();
    }
    if (!rule.minGameVersion.empty() && CompareGameVersion(gameVersion, rule.minGameVersion) < 0) {
        return false;
    }
    if (!rule.maxGameVersion.empty() && CompareGameVersion(gameVersion, rule.maxGameVersion) > 0) {
        return false;
    }
    return true;
}

}  // namespace mil
