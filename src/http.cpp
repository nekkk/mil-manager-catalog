#include "mil/http.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>

#include <curl/curl.h>
#include <mbedtls/aes.h>
#include <mbedtls/base64.h>
#include <switch.h>

#include "picojson.h"

namespace mil {

namespace {

constexpr const char* kUserAgent = "MIL-Manager/0.1";
constexpr long kConnectTimeoutMs = 4000;
constexpr long kRequestTimeoutMs = 8000;

struct FileSink {
    FILE* file = nullptr;
    std::size_t bytesWritten = 0;
    bool decryptMega = false;
    std::unique_ptr<Aes128CtrContext, void (*)(Aes128CtrContext*)> aes{nullptr, [](Aes128CtrContext* ptr) {
        if (ptr != nullptr) {
            free(ptr);
        }
    }};
    std::string buffer;
};

struct StringSink {
    std::string* output = nullptr;
    bool decryptMega = false;
    std::unique_ptr<Aes128CtrContext, void (*)(Aes128CtrContext*)> aes{nullptr, [](Aes128CtrContext* ptr) {
        if (ptr != nullptr) {
            free(ptr);
        }
    }};
    std::string buffer;
};

struct MegaInfo {
    std::string directUrl;
    std::string key;
    std::string iv;
    bool valid = false;
};

struct MegaFolderEntry {
    std::string handle;
    std::string parentHandle;
    std::string attributes;
    std::string encryptedKey;
    bool isFolder = false;
};

void ConfigureCurlCommon(CURL* curl) {
    curl_easy_setopt(curl, CURLOPT_USERAGENT, kUserAgent);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, kConnectTimeoutMs);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, kRequestTimeoutMs);
    curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
}

size_t WriteStringCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    const std::size_t actualSize = size * nmemb;
    auto* output = reinterpret_cast<std::string*>(userp);
    output->append(reinterpret_cast<const char*>(contents), actualSize);
    return actualSize;
}

size_t WriteStringSinkCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    const std::size_t actualSize = size * nmemb;
    auto* sink = reinterpret_cast<StringSink*>(userp);
    if (!sink->output) {
        return 0;
    }

    if (sink->decryptMega && sink->aes) {
        sink->buffer.resize(actualSize);
        aes128CtrCrypt(sink->aes.get(), reinterpret_cast<std::uint8_t*>(sink->buffer.data()), contents, actualSize);
        sink->output->append(sink->buffer.data(), actualSize);
        return actualSize;
    }

    sink->output->append(reinterpret_cast<const char*>(contents), actualSize);
    return actualSize;
}

size_t WriteFileCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    const std::size_t actualSize = size * nmemb;
    auto* sink = reinterpret_cast<FileSink*>(userp);
    if (!sink->file) {
        return 0;
    }

    if (sink->decryptMega && sink->aes) {
        sink->buffer.resize(actualSize);
        aes128CtrCrypt(sink->aes.get(), reinterpret_cast<std::uint8_t*>(sink->buffer.data()), contents, actualSize);
        const std::size_t written = fwrite(sink->buffer.data(), 1, actualSize, sink->file);
        sink->bytesWritten += written;
        return written;
    }

    const std::size_t written = fwrite(contents, 1, actualSize, sink->file);
    sink->bytesWritten += written;
    return written;
}

bool IsMegaUrl(const std::string& url) {
    return url.find("mega.nz/") != std::string::npos || url.find("mega.co.nz/") != std::string::npos;
}

bool IsMegaFolderUrl(const std::string& url) {
    return url.find("/folder/") != std::string::npos || url.find("#F!") != std::string::npos;
}

std::string LowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string ExtractMegaId(const std::string& url) {
    const std::size_t filePos = url.find("/file/");
    if (filePos != std::string::npos) {
        const std::size_t idBegin = filePos + 6;
        const std::size_t separator = url.find('#', idBegin);
        return url.substr(idBegin, separator - idBegin);
    }

    const std::size_t legacyPos = url.find("#!");
    if (legacyPos != std::string::npos) {
        const std::size_t idBegin = legacyPos + 2;
        const std::size_t separator = url.find('!', idBegin);
        return url.substr(idBegin, separator - idBegin);
    }

    return {};
}

std::string ExtractMegaFolderId(const std::string& url) {
    const std::size_t folderPos = url.find("/folder/");
    if (folderPos != std::string::npos) {
        const std::size_t idBegin = folderPos + 8;
        const std::size_t separator = url.find('#', idBegin);
        return url.substr(idBegin, separator - idBegin);
    }

    const std::size_t legacyPos = url.find("#F!");
    if (legacyPos != std::string::npos) {
        const std::size_t idBegin = legacyPos + 3;
        const std::size_t separator = url.find('!', idBegin);
        return url.substr(idBegin, separator - idBegin);
    }

    return {};
}

std::string ExtractMegaNodeKey(const std::string& url) {
    const std::size_t modernPos = url.find('#');
    if (modernPos != std::string::npos && modernPos + 1 < url.size()) {
        return url.substr(modernPos + 1);
    }

    const std::size_t legacyPos = url.rfind('!');
    if (legacyPos != std::string::npos && legacyPos + 1 < url.size()) {
        return url.substr(legacyPos + 1);
    }

    return {};
}

bool Base64UrlDecode(std::string encodedValue, std::string& decodedOut) {
    std::replace(encodedValue.begin(), encodedValue.end(), '-', '+');
    std::replace(encodedValue.begin(), encodedValue.end(), '_', '/');
    while (encodedValue.size() % 4 != 0) {
        encodedValue.push_back('=');
    }

    decodedOut.assign((encodedValue.size() * 3) / 4 + 1, '\0');
    std::size_t outputSize = 0;
    const int base64Result = mbedtls_base64_decode(reinterpret_cast<unsigned char*>(decodedOut.data()),
                                                   decodedOut.size(),
                                                   &outputSize,
                                                   reinterpret_cast<const unsigned char*>(encodedValue.data()),
                                                   encodedValue.size());
    if (base64Result != 0) {
        decodedOut.clear();
        return false;
    }
    decodedOut.resize(outputSize);
    return true;
}

bool DecodeMegaNodeKey(const std::string& encodedKey, std::string& keyOut, std::string& ivOut) {
    std::string decoded;
    if (!Base64UrlDecode(encodedKey, decoded) || decoded.size() != 32) {
        return false;
    }

    keyOut.assign(16, '\0');
    ivOut.assign(16, '\0');
    for (int index = 0; index < 16; ++index) {
        keyOut[index] = decoded[index] ^ decoded[index + 16];
    }
    for (int index = 0; index < 8; ++index) {
        ivOut[index] = decoded[index + 16];
    }
    return true;
}

bool DecodeMegaFolderKey(const std::string& encodedKey, std::string& keyOut) {
    std::string decoded;
    if (!Base64UrlDecode(encodedKey, decoded) || decoded.size() != 16) {
        return false;
    }
    keyOut = std::move(decoded);
    return true;
}

bool DeriveMegaFileKeyFromFolderEntry(const std::string& folderKey,
                                      const std::string& entryKey,
                                      std::string& keyOut,
                                      std::string& ivOut) {
    const std::size_t separator = entryKey.find(':');
    const std::string encodedNodeKey = separator == std::string::npos ? entryKey : entryKey.substr(separator + 1);

    std::string encryptedNodeKey;
    if (!Base64UrlDecode(encodedNodeKey, encryptedNodeKey) ||
        (encryptedNodeKey.size() != 16 && encryptedNodeKey.size() != 32) ||
        encryptedNodeKey.size() % 16 != 0) {
        return false;
    }

    mbedtls_aes_context context;
    mbedtls_aes_init(&context);
    if (mbedtls_aes_setkey_dec(&context,
                               reinterpret_cast<const unsigned char*>(folderKey.data()),
                               static_cast<unsigned int>(folderKey.size() * 8)) != 0) {
        mbedtls_aes_free(&context);
        return false;
    }

    std::string decryptedNodeKey(encryptedNodeKey.size(), '\0');
    for (std::size_t offset = 0; offset < encryptedNodeKey.size(); offset += 16) {
        if (mbedtls_aes_crypt_ecb(&context,
                                  MBEDTLS_AES_DECRYPT,
                                  reinterpret_cast<const unsigned char*>(encryptedNodeKey.data() + offset),
                                  reinterpret_cast<unsigned char*>(decryptedNodeKey.data() + offset)) != 0) {
            mbedtls_aes_free(&context);
            return false;
        }
    }
    mbedtls_aes_free(&context);

    if (decryptedNodeKey.size() != 32) {
        return false;
    }

    keyOut.assign(16, '\0');
    ivOut.assign(16, '\0');
    for (int index = 0; index < 16; ++index) {
        keyOut[index] = decryptedNodeKey[index] ^ decryptedNodeKey[index + 16];
    }
    for (int index = 0; index < 8; ++index) {
        ivOut[index] = decryptedNodeKey[index + 16];
    }
    return true;
}

bool DecryptMegaAttributes(const std::string& encodedAttributes, const std::string& key, std::string& nameOut) {
    std::string encryptedAttributes;
    if (!Base64UrlDecode(encodedAttributes, encryptedAttributes) ||
        encryptedAttributes.empty() ||
        encryptedAttributes.size() % 16 != 0) {
        return false;
    }

    mbedtls_aes_context context;
    mbedtls_aes_init(&context);
    if (mbedtls_aes_setkey_dec(&context,
                               reinterpret_cast<const unsigned char*>(key.data()),
                               static_cast<unsigned int>(key.size() * 8)) != 0) {
        mbedtls_aes_free(&context);
        return false;
    }

    std::string decryptedAttributes(encryptedAttributes.size(), '\0');
    unsigned char iv[16] = {};
    const int decryptResult = mbedtls_aes_crypt_cbc(&context,
                                                    MBEDTLS_AES_DECRYPT,
                                                    encryptedAttributes.size(),
                                                    iv,
                                                    reinterpret_cast<const unsigned char*>(encryptedAttributes.data()),
                                                    reinterpret_cast<unsigned char*>(decryptedAttributes.data()));
    mbedtls_aes_free(&context);
    if (decryptResult != 0) {
        return false;
    }

    while (!decryptedAttributes.empty() && decryptedAttributes.back() == '\0') {
        decryptedAttributes.pop_back();
    }
    if (decryptedAttributes.rfind("MEGA", 0) != 0) {
        return false;
    }

    picojson::value root;
    const std::string parseError = picojson::parse(root, decryptedAttributes.substr(4));
    if (!parseError.empty() || !root.is<picojson::object>()) {
        return false;
    }

    const auto& object = root.get<picojson::object>();
    const auto nameIt = object.find("n");
    if (nameIt == object.end() || !nameIt->second.is<std::string>()) {
        return false;
    }
    nameOut = nameIt->second.get<std::string>();
    return true;
}

bool ResolveMegaFile(const std::string& publicUrl, MegaInfo& info, std::string& error) {
    const std::string fileId = ExtractMegaId(publicUrl);
    const std::string encodedKey = ExtractMegaNodeKey(publicUrl);
    if (fileId.empty() || encodedKey.empty()) {
        error = "Link do MEGA invalido.";
        return false;
    }

    std::string key;
    std::string iv;
    if (!DecodeMegaNodeKey(encodedKey, key, iv)) {
        error = "Nao foi possivel decodificar a chave do MEGA.";
        return false;
    }

    picojson::array requestArray;
    picojson::object requestObject;
    requestObject["a"] = picojson::value("g");
    requestObject["g"] = picojson::value(1.0);
    requestObject["p"] = picojson::value(fileId);
    requestArray.emplace_back(requestObject);

    HttpResponse response;
    CURL* curl = curl_easy_init();
    if (!curl) {
        error = "curl_easy_init falhou.";
        return false;
    }

    const std::string body = picojson::value(requestArray).serialize();
    curl_easy_setopt(curl, CURLOPT_URL, "https://g.api.mega.co.nz/cs");
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteStringCallback);
    ConfigureCurlCommon(curl);

    const CURLcode performResult = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.statusCode);
    curl_easy_cleanup(curl);

    if (performResult != CURLE_OK) {
        error = std::string("Falha na consulta do MEGA: ") + curl_easy_strerror(performResult);
        return false;
    }
    if (response.statusCode < 200 || response.statusCode >= 300) {
        error = "Resposta HTTP invalida do MEGA: " + std::to_string(response.statusCode);
        return false;
    }

    picojson::value root;
    const std::string parseError = picojson::parse(root, response.body);
    if (!parseError.empty() || !root.is<picojson::array>() || root.get<picojson::array>().empty()) {
        error = "Resposta invalida do MEGA.";
        return false;
    }

    const picojson::value& first = root.get<picojson::array>().front();
    if (!first.is<picojson::object>()) {
        error = "Resposta do MEGA sem objeto de download.";
        return false;
    }

    const auto& object = first.get<picojson::object>();
    const auto directUrlIt = object.find("g");
    if (directUrlIt == object.end() || !directUrlIt->second.is<std::string>()) {
        error = "MEGA nao retornou a URL de download.";
        return false;
    }

    info.directUrl = directUrlIt->second.get<std::string>();
    info.key = key;
    info.iv = iv;
    info.valid = true;
    return true;
}

bool ResolveMegaFolderFile(const std::string& publicUrl, MegaInfo& info, std::string& error) {
    const std::string folderId = ExtractMegaFolderId(publicUrl);
    const std::string encodedFolderKey = ExtractMegaNodeKey(publicUrl);
    if (folderId.empty() || encodedFolderKey.empty()) {
        error = "Link de pasta do MEGA invalido.";
        return false;
    }

    std::string folderKey;
    if (!DecodeMegaFolderKey(encodedFolderKey, folderKey)) {
        error = "Nao foi possivel decodificar a chave da pasta do MEGA.";
        return false;
    }

    HttpResponse listResponse;
    CURL* curl = curl_easy_init();
    if (!curl) {
        error = "curl_easy_init falhou.";
        return false;
    }

    const std::string listBody = R"([{"a":"f","c":1,"ca":1,"r":1}])";
    curl_easy_setopt(curl, CURLOPT_URL, ("https://g.api.mega.co.nz/cs?id=0&n=" + folderId).c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, listBody.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &listResponse.body);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteStringCallback);
    ConfigureCurlCommon(curl);

    const CURLcode listResult = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &listResponse.statusCode);
    curl_easy_cleanup(curl);

    if (listResult != CURLE_OK) {
        error = std::string("Falha na consulta da pasta do MEGA: ") + curl_easy_strerror(listResult);
        return false;
    }
    if (listResponse.statusCode < 200 || listResponse.statusCode >= 300) {
        error = "Resposta HTTP invalida da pasta do MEGA: " + std::to_string(listResponse.statusCode);
        return false;
    }

    picojson::value root;
    const std::string parseError = picojson::parse(root, listResponse.body);
    if (!parseError.empty() || !root.is<picojson::array>() || root.get<picojson::array>().empty()) {
        error = "Resposta invalida da pasta do MEGA.";
        return false;
    }

    const picojson::value& first = root.get<picojson::array>().front();
    if (!first.is<picojson::object>()) {
        error = "Resposta da pasta do MEGA sem objeto raiz.";
        return false;
    }

    const auto& responseObject = first.get<picojson::object>();
    const auto filesIt = responseObject.find("f");
    if (filesIt == responseObject.end() || !filesIt->second.is<picojson::array>()) {
        error = "Pasta do MEGA sem lista de arquivos.";
        return false;
    }

    std::string rootHandle;
    std::vector<MegaFolderEntry> entries;
    for (const picojson::value& item : filesIt->second.get<picojson::array>()) {
        if (!item.is<picojson::object>()) {
            continue;
        }
        const auto& object = item.get<picojson::object>();
        MegaFolderEntry entry;

        const auto handleIt = object.find("h");
        if (handleIt != object.end() && handleIt->second.is<std::string>()) {
            entry.handle = handleIt->second.get<std::string>();
        }
        const auto parentIt = object.find("p");
        if (parentIt != object.end() && parentIt->second.is<std::string>()) {
            entry.parentHandle = parentIt->second.get<std::string>();
        }
        const auto attributesIt = object.find("a");
        if (attributesIt != object.end() && attributesIt->second.is<std::string>()) {
            entry.attributes = attributesIt->second.get<std::string>();
        }
        const auto keyIt = object.find("k");
        if (keyIt != object.end() && keyIt->second.is<std::string>()) {
            entry.encryptedKey = keyIt->second.get<std::string>();
        }
        const auto typeIt = object.find("t");
        if (typeIt != object.end() && typeIt->second.is<double>()) {
            entry.isFolder = static_cast<int>(typeIt->second.get<double>()) == 1;
        }

        if (entry.isFolder && rootHandle.empty()) {
            rootHandle = entry.handle;
        }
        entries.push_back(std::move(entry));
    }

    if (rootHandle.empty()) {
        error = "Nao foi possivel identificar a pasta raiz do link do MEGA.";
        return false;
    }

    MegaFolderEntry selectedEntry;
    bool foundFallback = false;
    for (const MegaFolderEntry& entry : entries) {
        if (entry.isFolder || entry.parentHandle != rootHandle || entry.encryptedKey.empty() || entry.attributes.empty()) {
            continue;
        }

        std::string fileKey;
        std::string fileIv;
        std::string fileName;
        if (!DeriveMegaFileKeyFromFolderEntry(folderKey, entry.encryptedKey, fileKey, fileIv) ||
            !DecryptMegaAttributes(entry.attributes, fileKey, fileName)) {
            continue;
        }

        if (!foundFallback) {
            selectedEntry = entry;
            info.key = std::move(fileKey);
            info.iv = std::move(fileIv);
            foundFallback = true;
        }
        if (LowerAscii(fileName) == "index.json") {
            selectedEntry = entry;
            info.key = std::move(fileKey);
            info.iv = std::move(fileIv);
            break;
        }
    }

    if (!foundFallback) {
        error = "Nenhum arquivo valido encontrado na pasta do MEGA.";
        return false;
    }

    HttpResponse fileResponse;
    curl = curl_easy_init();
    if (!curl) {
        error = "curl_easy_init falhou.";
        return false;
    }

    const std::string fileBody = "[{\"a\":\"g\",\"g\":1,\"n\":\"" + selectedEntry.handle + "\"}]";
    curl_easy_setopt(curl, CURLOPT_URL, ("https://g.api.mega.co.nz/cs?id=0&n=" + folderId).c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, fileBody.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &fileResponse.body);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteStringCallback);
    ConfigureCurlCommon(curl);

    const CURLcode fileResult = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &fileResponse.statusCode);
    curl_easy_cleanup(curl);

    if (fileResult != CURLE_OK) {
        error = std::string("Falha ao resolver arquivo da pasta do MEGA: ") + curl_easy_strerror(fileResult);
        return false;
    }
    if (fileResponse.statusCode < 200 || fileResponse.statusCode >= 300) {
        error = "Resposta HTTP invalida ao resolver arquivo da pasta do MEGA: " + std::to_string(fileResponse.statusCode);
        return false;
    }

    picojson::value fileRoot;
    const std::string fileParseError = picojson::parse(fileRoot, fileResponse.body);
    if (!fileParseError.empty() || !fileRoot.is<picojson::array>() || fileRoot.get<picojson::array>().empty()) {
        error = "Resposta invalida ao resolver arquivo da pasta do MEGA.";
        return false;
    }

    const picojson::value& fileFirst = fileRoot.get<picojson::array>().front();
    if (!fileFirst.is<picojson::object>()) {
        error = "Resposta do arquivo da pasta do MEGA sem objeto.";
        return false;
    }

    const auto& fileObject = fileFirst.get<picojson::object>();
    const auto directUrlIt = fileObject.find("g");
    if (directUrlIt == fileObject.end() || !directUrlIt->second.is<std::string>()) {
        error = "Pasta do MEGA nao retornou URL de download para o indice.";
        return false;
    }

    info.directUrl = directUrlIt->second.get<std::string>();
    info.valid = true;
    return true;
}

bool PerformGetRequest(const std::string& url, HttpResponse& response, std::string& error) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        error = "curl_easy_init falhou.";
        return false;
    }

    response = {};
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteStringCallback);
    ConfigureCurlCommon(curl);

    const CURLcode result = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.statusCode);
    curl_easy_cleanup(curl);

    if (result != CURLE_OK) {
        error = std::string("Falha na requisicao HTTP: ") + curl_easy_strerror(result);
        return false;
    }
    return true;
}

}  // namespace

bool HttpGetToString(const std::string& url, HttpResponse& response, std::string& error) {
    if (!IsMegaUrl(url)) {
        return PerformGetRequest(url, response, error);
    }

    MegaInfo megaInfo;
    const bool resolved = IsMegaFolderUrl(url) ? ResolveMegaFolderFile(url, megaInfo, error)
                                               : ResolveMegaFile(url, megaInfo, error);
    if (!resolved) {
        return false;
    }

    response = {};
    StringSink sink;
    sink.output = &response.body;
    sink.decryptMega = megaInfo.valid;
    if (megaInfo.valid) {
        auto* context = static_cast<Aes128CtrContext*>(malloc(sizeof(Aes128CtrContext)));
        if (!context) {
            error = "Sem memoria para inicializar descriptografia do MEGA.";
            return false;
        }
        aes128CtrContextCreate(context, megaInfo.key.data(), megaInfo.iv.data());
        sink.aes.reset(context);
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        error = "curl_easy_init falhou.";
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, megaInfo.directUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteStringSinkCallback);
    ConfigureCurlCommon(curl);

    const CURLcode result = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.statusCode);
    curl_easy_cleanup(curl);

    if (result != CURLE_OK) {
        error = std::string("Falha na requisicao HTTP do MEGA: ") + curl_easy_strerror(result);
        return false;
    }
    return true;
}

bool HttpDownloadToFile(const std::string& url, const std::string& outputPath, std::size_t* downloadedBytes, std::string& error) {
    MegaInfo megaInfo;
    std::string effectiveUrl = url;
    if (IsMegaUrl(url)) {
        if (!ResolveMegaFile(url, megaInfo, error)) {
            return false;
        }
        effectiveUrl = megaInfo.directUrl;
    }

    FileSink sink;
    sink.file = fopen(outputPath.c_str(), "wb");
    if (!sink.file) {
        error = "Nao foi possivel abrir o arquivo temporario para escrita.";
        return false;
    }

    if (megaInfo.valid) {
        sink.decryptMega = true;
        auto* context = static_cast<Aes128CtrContext*>(malloc(sizeof(Aes128CtrContext)));
        if (!context) {
            fclose(sink.file);
            error = "Sem memoria para inicializar descriptografia do MEGA.";
            return false;
        }
        aes128CtrContextCreate(context, megaInfo.key.data(), megaInfo.iv.data());
        sink.aes.reset(context);
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        fclose(sink.file);
        error = "curl_easy_init falhou.";
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, effectiveUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteFileCallback);
    ConfigureCurlCommon(curl);

    const CURLcode result = curl_easy_perform(curl);
    long statusCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &statusCode);
    curl_easy_cleanup(curl);
    fclose(sink.file);

    if (result != CURLE_OK) {
        error = std::string("Falha no download: ") + curl_easy_strerror(result);
        return false;
    }
    if (statusCode < 200 || statusCode >= 400) {
        error = "Download retornou HTTP " + std::to_string(statusCode);
        return false;
    }

    if (downloadedBytes != nullptr) {
        *downloadedBytes = sink.bytesWritten;
    }

    return true;
}

bool HttpGetWithCache(const std::vector<std::string>& urls,
                      const std::string& cachePath,
                      HttpResponse& response,
                      std::string& source,
                      std::string& error) {
    for (const std::string& url : urls) {
        HttpResponse candidate;
        std::string candidateError;
        if (!HttpGetToString(url, candidate, candidateError)) {
            error = candidateError;
            continue;
        }
        if (candidate.statusCode < 200 || candidate.statusCode >= 300) {
            error = "HTTP " + std::to_string(candidate.statusCode) + " ao ler " + url;
            continue;
        }

        std::ofstream output(cachePath, std::ios::binary | std::ios::trunc);
        if (output.good()) {
            output.write(candidate.body.data(), static_cast<std::streamsize>(candidate.body.size()));
        }

        response = std::move(candidate);
        source = url;
        return true;
    }

    std::ifstream input(cachePath, std::ios::binary);
    if (!input.good()) {
        if (error.empty()) {
            error = "Cache local indisponivel.";
        }
        return false;
    }

    response = {};
    response.statusCode = 200;
    response.body.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    source = cachePath;
    return true;
}

}  // namespace mil
