#include "mil/installer.hpp"

#include <archive.h>
#include <archive_entry.h>

#include <cstdio>
#include <ctime>
#include <dirent.h>
#include <fstream>
#include <sys/stat.h>

#include "mil/config.hpp"
#include "mil/http.hpp"

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
        const bool nextIsSeparator = !isLast && path[index + 1] == '/';
        if (!isLast && !nextIsSeparator) {
            continue;
        }
        mkdir(current.c_str(), 0777);
    }
    return true;
}

std::string ParentDirectory(std::string path) {
    const std::size_t pos = path.find_last_of('/');
    if (pos == std::string::npos) {
        return {};
    }
    return path.substr(0, pos);
}

bool StartsWith(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

std::string SanitizeArchivePath(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    while (StartsWith(path, "./")) {
        path.erase(0, 2);
    }
    if (path.empty() || StartsWith(path, "/") || path.find("..") != std::string::npos) {
        return {};
    }
    return path;
}

std::string ReceiptPath(const std::string& packageId) {
    return std::string(kReceiptsDir) + "/" + packageId + ".ini";
}

std::string CurrentTimestamp() {
    const std::time_t now = std::time(nullptr);
    char buffer[64] = {};
    std::tm* timeInfo = std::gmtime(&now);
    if (!timeInfo) {
        return {};
    }
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", timeInfo);
    return buffer;
}

bool SaveReceipt(const InstallReceipt& receipt, std::string& error) {
    EnsureDirectory("sdmc:/config");
    EnsureDirectory("sdmc:/config/mil-manager");
    EnsureDirectory(kReceiptsDir);

    std::ofstream output(ReceiptPath(receipt.packageId), std::ios::trunc);
    if (!output.good()) {
        error = "Nao foi possivel gravar o recibo de instalacao.";
        return false;
    }

    output << "package_id=" << receipt.packageId << '\n';
    output << "package_version=" << receipt.packageVersion << '\n';
    output << "title_id=" << receipt.titleId << '\n';
    output << "install_root=" << receipt.installRoot << '\n';
    output << "source_url=" << receipt.sourceUrl << '\n';
    output << "installed_at=" << receipt.installedAt << '\n';
    output << "game_version=" << receipt.gameVersion << '\n';
    for (const std::string& file : receipt.files) {
        output << "file=" << file << '\n';
    }

    return output.good();
}

InstallReceipt ParseReceiptFile(const std::string& path) {
    InstallReceipt receipt;
    std::ifstream input(path);
    std::string line;
    while (std::getline(input, line)) {
        const std::size_t equalsPos = line.find('=');
        if (equalsPos == std::string::npos) {
            continue;
        }
        const std::string key = ToLowerAscii(Trim(line.substr(0, equalsPos)));
        const std::string value = Trim(line.substr(equalsPos + 1));
        if (key == "package_id") {
            receipt.packageId = value;
        } else if (key == "package_version") {
            receipt.packageVersion = value;
        } else if (key == "title_id") {
            receipt.titleId = value;
        } else if (key == "install_root") {
            receipt.installRoot = value;
        } else if (key == "source_url") {
            receipt.sourceUrl = value;
        } else if (key == "installed_at") {
            receipt.installedAt = value;
        } else if (key == "game_version") {
            receipt.gameVersion = value;
        } else if (key == "file") {
            receipt.files.push_back(value);
        }
    }
    return receipt;
}

bool RemoveEmptyParents(const std::string& path) {
    std::string current = ParentDirectory(path);
    while (!current.empty() && current != "sdmc:" && current != "sdmc:/") {
        DIR* dir = opendir(current.c_str());
        if (!dir) {
            return false;
        }
        int count = 0;
        while (dirent* entry = readdir(dir)) {
            const std::string name = entry->d_name;
            if (name != "." && name != "..") {
                ++count;
                break;
            }
        }
        closedir(dir);
        if (count > 0) {
            break;
        }
        rmdir(current.c_str());
        current = ParentDirectory(current);
    }
    return true;
}

bool ExtractZip(const std::string& zipPath, std::vector<std::string>& extractedFiles, std::string& error) {
    extractedFiles.clear();

    struct archive* reader = archive_read_new();
    archive_read_support_filter_all(reader);
    archive_read_support_format_zip(reader);

    if (archive_read_open_filename(reader, zipPath.c_str(), 10240) != ARCHIVE_OK) {
        error = archive_error_string(reader);
        archive_read_free(reader);
        return false;
    }

    struct archive_entry* entry = nullptr;
    while (archive_read_next_header(reader, &entry) == ARCHIVE_OK) {
        const char* rawPath = archive_entry_pathname(entry);
        const std::string relativePath = rawPath ? SanitizeArchivePath(rawPath) : std::string();
        if (relativePath.empty()) {
            archive_read_data_skip(reader);
            continue;
        }

        const std::string fullPath = "sdmc:/" + relativePath;
        if (archive_entry_filetype(entry) == AE_IFDIR) {
            EnsureDirectory(fullPath);
            archive_read_data_skip(reader);
            continue;
        }

        EnsureDirectory(ParentDirectory(fullPath));
        FILE* output = fopen(fullPath.c_str(), "wb");
        if (!output) {
            error = "Nao foi possivel criar " + fullPath;
            archive_read_free(reader);
            return false;
        }

        const void* buffer = nullptr;
        size_t size = 0;
        la_int64_t offset = 0;
        while (archive_read_data_block(reader, &buffer, &size, &offset) == ARCHIVE_OK) {
            if (fwrite(buffer, 1, size, output) != size) {
                fclose(output);
                error = "Falha ao escrever arquivo extraido.";
                archive_read_free(reader);
                return false;
            }
        }

        fclose(output);
        extractedFiles.push_back(fullPath);
    }

    archive_read_free(reader);
    return true;
}

}  // namespace

std::vector<InstallReceipt> LoadInstallReceipts(std::string& note) {
    std::vector<InstallReceipt> receipts;
    DIR* dir = opendir(kReceiptsDir);
    if (!dir) {
        note = "Nenhum recibo de instalacao encontrado ainda.";
        return receipts;
    }

    while (dirent* entry = readdir(dir)) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..") {
            continue;
        }
        if (name.size() < 5 || name.substr(name.size() - 4) != ".ini") {
            continue;
        }
        receipts.push_back(ParseReceiptFile(std::string(kReceiptsDir) + "/" + name));
    }

    closedir(dir);
    note = "Recibos de instalacao carregados.";
    return receipts;
}

bool FindReceiptForPackage(const std::vector<InstallReceipt>& receipts, const std::string& packageId, InstallReceipt* receipt) {
    for (const InstallReceipt& item : receipts) {
        if (item.packageId == packageId) {
            if (receipt != nullptr) {
                *receipt = item;
            }
            return true;
        }
    }
    return false;
}

bool InstallPackage(const CatalogEntry& entry, const InstalledTitle* installedTitle, InstallReceipt& receipt, std::string& error) {
    EnsureDirectory("sdmc:/config");
    EnsureDirectory("sdmc:/config/mil-manager");
    EnsureDirectory(kCacheDir);

    const std::string zipPath = std::string(kCacheDir) + "/" + entry.id + ".zip";
    std::size_t bytesDownloaded = 0;
    if (!HttpDownloadToFile(entry.downloadUrl, zipPath, &bytesDownloaded, error)) {
        return false;
    }
    if (bytesDownloaded == 0) {
        remove(zipPath.c_str());
        error = "Download concluido, mas o arquivo estava vazio.";
        return false;
    }

    std::vector<std::string> extractedFiles;
    if (!ExtractZip(zipPath, extractedFiles, error)) {
        remove(zipPath.c_str());
        return false;
    }

    remove(zipPath.c_str());

    receipt = {};
    receipt.packageId = entry.id;
    receipt.packageVersion = entry.packageVersion;
    receipt.titleId = ToLowerAscii(entry.titleId);
    receipt.installRoot = "sdmc:/";
    receipt.sourceUrl = entry.downloadUrl;
    receipt.installedAt = CurrentTimestamp();
    receipt.gameVersion = installedTitle ? installedTitle->displayVersion : std::string();
    receipt.files = std::move(extractedFiles);

    if (!SaveReceipt(receipt, error)) {
        for (const std::string& file : receipt.files) {
            remove(file.c_str());
            RemoveEmptyParents(file);
        }
        return false;
    }

    return true;
}

bool UninstallPackage(const InstallReceipt& receipt, std::string& error) {
    for (const std::string& file : receipt.files) {
        remove(file.c_str());
        RemoveEmptyParents(file);
    }

    if (remove(ReceiptPath(receipt.packageId).c_str()) != 0) {
        error = "Arquivos removidos, mas o recibo nao foi apagado.";
        return false;
    }

    return true;
}

}  // namespace mil
