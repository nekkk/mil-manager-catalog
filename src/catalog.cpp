#include "mil/catalog.hpp"

#include <fstream>
#include <sstream>

#include "picojson.h"

namespace mil {

namespace {

std::string StripUtf8Bom(std::string value) {
    if (value.size() >= 3 &&
        static_cast<unsigned char>(value[0]) == 0xEF &&
        static_cast<unsigned char>(value[1]) == 0xBB &&
        static_cast<unsigned char>(value[2]) == 0xBF) {
        value.erase(0, 3);
    }
    return value;
}

std::string GetString(const picojson::object& object, const std::string& key) {
    const auto iterator = object.find(key);
    if (iterator == object.end() || !iterator->second.is<std::string>()) {
        return {};
    }
    return iterator->second.get<std::string>();
}

bool GetBool(const picojson::object& object, const std::string& key) {
    const auto iterator = object.find(key);
    if (iterator == object.end() || !iterator->second.is<bool>()) {
        return false;
    }
    return iterator->second.get<bool>();
}

std::vector<std::string> GetStringArray(const picojson::object& object, const std::string& key) {
    std::vector<std::string> result;
    const auto iterator = object.find(key);
    if (iterator == object.end() || !iterator->second.is<picojson::array>()) {
        return result;
    }
    for (const auto& item : iterator->second.get<picojson::array>()) {
        if (item.is<std::string>()) {
            result.push_back(item.get<std::string>());
        }
    }
    return result;
}

bool ParseEntry(const picojson::object& object, CatalogEntry& entry) {
    entry.id = GetString(object, "id");
    entry.titleId = ToLowerAscii(GetString(object, "titleId"));
    entry.name = GetString(object, "name");
    entry.summary = GetString(object, "summary");
    entry.author = GetString(object, "author");
    entry.packageVersion = GetString(object, "packageVersion");
    if (entry.packageVersion.empty()) {
        entry.packageVersion = GetString(object, "version");
    }
    entry.contentRevision = GetString(object, "contentRevision");
    entry.language = GetString(object, "language");
    entry.downloadUrl = GetString(object, "downloadUrl");
    entry.detailsUrl = GetString(object, "detailsUrl");
    entry.tags = GetStringArray(object, "tags");
    entry.section = ParseSection(GetString(object, "section"));
    entry.featured = GetBool(object, "featured");

    const auto compatibilityIt = object.find("compatibility");
    if (compatibilityIt != object.end() && compatibilityIt->second.is<picojson::object>()) {
        const auto& compatibility = compatibilityIt->second.get<picojson::object>();
        entry.compatibility.minGameVersion = GetString(compatibility, "minGameVersion");
        entry.compatibility.maxGameVersion = GetString(compatibility, "maxGameVersion");
        entry.compatibility.exactGameVersions = GetStringArray(compatibility, "exactGameVersions");
    }

    return !entry.id.empty() && !entry.name.empty() && !entry.downloadUrl.empty();
}

}  // namespace

bool LoadCatalogFromJsonString(const std::string& json, CatalogIndex& catalog, std::string& error) {
    picojson::value root;
    const std::string normalizedJson = StripUtf8Bom(json);
    const std::string parseError = picojson::parse(root, normalizedJson);
    if (!parseError.empty()) {
        error = "Erro ao ler catalogo JSON: " + parseError;
        return false;
    }
    if (!root.is<picojson::object>()) {
        error = "Catalogo invalido: raiz nao e um objeto.";
        return false;
    }

    const auto& object = root.get<picojson::object>();
    catalog = {};
    catalog.catalogName = GetString(object, "catalogName");
    catalog.catalogRevision = GetString(object, "catalogRevision");
    catalog.channel = GetString(object, "channel");
    catalog.schemaVersion = GetString(object, "schemaVersion");
    catalog.generatedAt = GetString(object, "generatedAt");

    const auto entriesIt = object.find("entries");
    if (entriesIt == object.end() || !entriesIt->second.is<picojson::array>()) {
        error = "Catalogo invalido: campo entries ausente.";
        return false;
    }

    for (const auto& item : entriesIt->second.get<picojson::array>()) {
        if (!item.is<picojson::object>()) {
            continue;
        }
        CatalogEntry entry;
        if (ParseEntry(item.get<picojson::object>(), entry)) {
            catalog.entries.push_back(entry);
        }
    }

    if (catalog.entries.empty()) {
        error = "Catalogo carregado, mas sem entradas validas.";
        return false;
    }

    return true;
}

bool LoadCatalogFromFile(const std::string& path, CatalogIndex& catalog, std::string& error) {
    std::ifstream input(path);
    if (!input.good()) {
        error = "Nao foi possivel abrir " + path;
        return false;
    }

    std::stringstream buffer;
    buffer << input.rdbuf();
    return LoadCatalogFromJsonString(buffer.str(), catalog, error);
}

}  // namespace mil
