#include "mil/app.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sys/stat.h>
#include <string>
#include <vector>

#include <switch.h>

#include "mil/catalog.hpp"
#include "mil/config.hpp"
#include "mil/graphics.hpp"
#include "mil/http.hpp"
#include "mil/installer.hpp"
#include "mil/platform.hpp"

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

struct AppState {
    AppConfig config;
    CatalogIndex catalog;
    std::vector<InstalledTitle> installedTitles;
    std::vector<InstallReceipt> receipts;
    ContentSection section = ContentSection::Translations;
    std::size_t selection = 0;
    std::string activeCatalogSource;
    std::string statusLine = "Carregando...";
    std::string platformNote;
    enum class FocusPane {
        Sections,
        Catalog,
    } focus = FocusPane::Sections;
};

const InstalledTitle* FindInstalledTitle(const std::vector<InstalledTitle>& titles, const std::string& titleId) {
    const std::string normalized = ToLowerAscii(titleId);
    for (const InstalledTitle& title : titles) {
        if (ToLowerAscii(title.titleIdHex) == normalized) {
            return &title;
        }
    }
    return nullptr;
}

std::vector<const CatalogEntry*> BuildVisibleEntries(const AppState& state) {
    std::vector<const CatalogEntry*> items;
    for (const CatalogEntry& entry : state.catalog.entries) {
        if (entry.section == state.section) {
            items.push_back(&entry);
        }
    }

    std::sort(items.begin(), items.end(), [&](const CatalogEntry* left, const CatalogEntry* right) {
        const bool leftSuggested = FindInstalledTitle(state.installedTitles, left->titleId) != nullptr;
        const bool rightSuggested = FindInstalledTitle(state.installedTitles, right->titleId) != nullptr;
        if (leftSuggested != rightSuggested) {
            return leftSuggested > rightSuggested;
        }
        if (left->featured != right->featured) {
            return left->featured > right->featured;
        }
        return left->name < right->name;
    });
    return items;
}

bool LoadCatalog(AppState& state) {
    EnsureDirectory("sdmc:/config");
    EnsureDirectory("sdmc:/config/mil-manager");
    EnsureDirectory(kCacheDir);
    const bool english = state.config.language == LanguageMode::EnUs;

    auto tryLoadLocalCatalog = [&](const char* path, const char* statusMessage) {
        std::string localError;
        CatalogIndex localCatalog;
        if (!LoadCatalogFromFile(path, localCatalog, localError)) {
            return false;
        }
        state.catalog = std::move(localCatalog);
        state.activeCatalogSource = path;
        state.statusLine = statusMessage;
        return true;
    };

    if (IsEmulatorEnvironment()) {
        if (tryLoadLocalCatalog(kSwitchLocalIndexPath,
                                english ? "Using local synchronized catalog." : "Usando catalogo local sincronizado.")) {
            return true;
        }
    }

    std::string error;
    for (const std::string& url : state.config.catalogUrls) {
        HttpResponse response;
        std::string candidateError;
        if (!HttpGetToString(url, response, candidateError)) {
            error = candidateError;
            continue;
        }
        if (response.statusCode < 200 || response.statusCode >= 300) {
            error = "HTTP " + std::to_string(response.statusCode) + " ao ler " + url;
            continue;
        }

        CatalogIndex remoteCatalog;
        std::string parseError;
        if (!LoadCatalogFromJsonString(response.body, remoteCatalog, parseError)) {
            error = parseError;
            continue;
        }

        std::ofstream output(kCatalogCachePath, std::ios::binary | std::ios::trunc);
        if (output.good()) {
            output.write(response.body.data(), static_cast<std::streamsize>(response.body.size()));
        }

        state.catalog = std::move(remoteCatalog);
        state.activeCatalogSource = url;
        return true;
    }

    if (tryLoadLocalCatalog(kCatalogCachePath,
                            english ? "Remote catalog unavailable. Using local cache."
                                    : "Catalogo remoto indisponivel. Usando cache local.")) {
        return true;
    }

    if (tryLoadLocalCatalog(kSwitchLocalIndexPath,
                            english ? "Using local synchronized catalog." : "Usando catalogo local sincronizado.")) {
        return true;
    }

    state.statusLine = "Falha ao carregar catalogo: " + error;
    return false;
}

std::string MakeCompatibilitySummary(const CatalogEntry& entry, const InstalledTitle* title) {
    if (!title) {
        return "Jogo nao encontrado no console/emulador.";
    }
    if (title->displayVersion.empty()) {
        return "Versao do jogo indisponivel.";
    }
    if (MatchesCompatibility(entry.compatibility, title->displayVersion)) {
        return "Compativel com a versao instalada: " + title->displayVersion;
    }

    std::string message = "Atencao: pacote fora da faixa suportada para o jogo instalado (" + title->displayVersion + ").";
    if (!entry.compatibility.minGameVersion.empty()) {
        message += " Min: " + entry.compatibility.minGameVersion + ".";
    }
    if (!entry.compatibility.maxGameVersion.empty()) {
        message += " Max: " + entry.compatibility.maxGameVersion + ".";
    }
    if (!entry.compatibility.exactGameVersions.empty()) {
        message += " Exatas: ";
        for (std::size_t index = 0; index < entry.compatibility.exactGameVersions.size(); ++index) {
            if (index > 0) {
                message += ", ";
            }
            message += entry.compatibility.exactGameVersions[index];
        }
        message += '.';
    }
    return message;
}

void PrintLine(const std::string& text) {
    std::printf("%s\n", text.c_str());
}

bool UseEnglish(const AppState& state) {
    if (state.config.language == LanguageMode::EnUs) {
        return true;
    }
    if (state.config.language == LanguageMode::PtBr) {
        return false;
    }
    return ToLowerAscii(GetPreferredLanguageCode()).rfind("en", 0) == 0;
}

const char* UiText(const AppState& state, const char* ptBr, const char* enUs) {
    return UseEnglish(state) ? enUs : ptBr;
}

std::string GetCatalogSourceLabel(const AppState& state) {
    if (state.activeCatalogSource.empty()) {
        return UiText(state, "Nenhuma fonte ativa", "No active source");
    }
    if (state.activeCatalogSource == kSwitchLocalIndexPath) {
        return UiText(state, "Catalogo sincronizado do emulador", "Emulator synchronized catalog");
    }
    if (state.activeCatalogSource == kCatalogCachePath) {
        return UiText(state, "Cache local do catalogo", "Local catalog cache");
    }
    if (state.activeCatalogSource.rfind("http://", 0) == 0 || state.activeCatalogSource.rfind("https://", 0) == 0) {
        return UiText(state, "Catalogo online", "Online catalog");
    }
    return UiText(state, "Catalogo local", "Local catalog");
}

std::string SectionLabelLocalized(const AppState& state, ContentSection section) {
    if (!UseEnglish(state)) {
        return SectionLabel(section);
    }

    switch (section) {
        case ContentSection::Translations:
            return "Translations & Dubs";
        case ContentSection::ModsTools:
            return "Mods & Tools";
        case ContentSection::Cheats:
            return "Cheats";
        case ContentSection::SaveGames:
            return "Save Games";
        case ContentSection::About:
        default:
            return "About M.I.L.";
    }
}

std::string MakeCompatibilitySummaryLocalized(const AppState& state, const CatalogEntry& entry, const InstalledTitle* title) {
    if (!UseEnglish(state)) {
        return MakeCompatibilitySummary(entry, title);
    }
    if (!title) {
        return "Game not found on console/emulator.";
    }
    if (title->displayVersion.empty()) {
        return "Installed game version unavailable.";
    }
    if (MatchesCompatibility(entry.compatibility, title->displayVersion)) {
        return "Compatible with installed version: " + title->displayVersion;
    }

    std::string message = "Warning: package is outside the supported range for the installed game (" + title->displayVersion + ").";
    if (!entry.compatibility.minGameVersion.empty()) {
        message += " Min: " + entry.compatibility.minGameVersion + ".";
    }
    if (!entry.compatibility.maxGameVersion.empty()) {
        message += " Max: " + entry.compatibility.maxGameVersion + ".";
    }
    if (!entry.compatibility.exactGameVersions.empty()) {
        message += " Exact: ";
        for (std::size_t index = 0; index < entry.compatibility.exactGameVersions.size(); ++index) {
            if (index > 0) {
                message += ", ";
            }
            message += entry.compatibility.exactGameVersions[index];
        }
        message += '.';
    }
    return message;
}

[[noreturn]] void ForceExitEmulator(PlatformSession& session) {
    ShutdownPlatform(session);
    svcExitProcess();
}

std::string TruncateText(const std::string& text, std::size_t maxChars) {
    if (text.size() <= maxChars) {
        return text;
    }
    if (maxChars <= 3) {
        return text.substr(0, maxChars);
    }
    return text.substr(0, maxChars - 3) + "...";
}

void DrawPanel(gfx::Canvas& canvas, int x, int y, int width, int height, std::uint32_t fill, std::uint32_t border) {
    gfx::FillRect(canvas, x, y, width, height, fill);
    gfx::DrawRect(canvas, x, y, width, height, border);
}

void DrawChip(gfx::Canvas& canvas, int x, int y, const std::string& text, std::uint32_t fill, std::uint32_t textColor) {
    const int width = gfx::MeasureTextWidth(text, 1) + 16;
    DrawPanel(canvas, x, y, width, 20, fill, fill);
    gfx::DrawText(canvas, x + 8, y + 6, text, textColor, 1);
}

void DrawSidebar(gfx::Canvas& canvas, const AppState& state, int x, int y, int width, int height) {
    const bool focused = state.focus == AppState::FocusPane::Sections;
    DrawPanel(canvas,
              x,
              y,
              width,
              height,
              gfx::Rgba(19, 24, 34),
              focused ? gfx::Rgba(124, 174, 255) : gfx::Rgba(54, 66, 92));

    const std::vector<ContentSection> sections = {
        ContentSection::Translations,
        ContentSection::ModsTools,
        ContentSection::Cheats,
        ContentSection::SaveGames,
        ContentSection::About,
    };

    gfx::DrawText(canvas,
                  x + 18,
                  y + 18,
                  UseEnglish(state) ? "Sections" : "Secoes",
                  focused ? gfx::Rgba(245, 249, 255) : gfx::Rgba(231, 238, 255),
                  2);
    int itemY = y + 60;
    for (ContentSection section : sections) {
        const bool selected = state.section == section;
        const std::uint32_t fill = selected ? gfx::Rgba(50, 92, 170) : gfx::Rgba(28, 36, 50);
        const std::uint32_t border = selected ? gfx::Rgba(120, 173, 255) : gfx::Rgba(44, 54, 74);
        DrawPanel(canvas, x + 14, itemY, width - 28, 48, fill, border);
        gfx::DrawText(canvas,
                      x + 26,
                      itemY + 16,
                      TruncateText(SectionLabelLocalized(state, section), 24),
                      selected ? gfx::Rgba(255, 255, 255) : gfx::Rgba(196, 207, 230),
                      1);
        itemY += 58;
    }
}

void DrawEmptyState(gfx::Canvas& canvas, const AppState& state, int x, int y, int width, int height) {
    DrawPanel(canvas, x, y, width, height, gfx::Rgba(16, 21, 31), gfx::Rgba(47, 58, 79));
    gfx::DrawText(canvas, x + 22, y + 22, UiText(state, "Nenhum item", "No items"), gfx::Rgba(238, 244, 255), 2);
    gfx::DrawTextWrapped(canvas,
                         x + 22,
                         y + 62,
                         width - 44,
                         UiText(state,
                                "Ainda nao ha conteudo disponivel para esta secao no catalogo atual.",
                                "There is no content available for this section in the current catalog."),
                         gfx::Rgba(170, 182, 208),
                         1,
                         4);
}

void DrawEntryList(gfx::Canvas& canvas,
                   const AppState& state,
                   const std::vector<const CatalogEntry*>& items,
                   int x,
                   int y,
                   int width,
                   int height) {
    const bool focused = state.focus == AppState::FocusPane::Catalog;
    DrawPanel(canvas,
              x,
              y,
              width,
              height,
              gfx::Rgba(16, 21, 31),
              focused ? gfx::Rgba(124, 174, 255) : gfx::Rgba(47, 58, 79));
    gfx::DrawText(canvas,
                  x + 20,
                  y + 18,
                  UiText(state, "Catalogo", "Catalog"),
                  focused ? gfx::Rgba(245, 249, 255) : gfx::Rgba(238, 244, 255),
                  2);

    if (items.empty()) {
        DrawEmptyState(canvas, state, x + 18, y + 54, width - 36, height - 72);
        return;
    }

    const std::size_t clampedSelection = std::min(state.selection, items.size() - 1);
    const std::size_t windowStart = clampedSelection > 2 ? clampedSelection - 2 : 0;
    const std::size_t windowEnd = std::min(items.size(), windowStart + 5);
    int cardY = y + 56;

    for (std::size_t index = windowStart; index < windowEnd; ++index) {
        const CatalogEntry& entry = *items[index];
        const bool selected = index == clampedSelection;
        const bool suggested = FindInstalledTitle(state.installedTitles, entry.titleId) != nullptr;
        const bool installed = FindReceiptForPackage(state.receipts, entry.id, nullptr);

        const std::uint32_t fill = selected ? gfx::Rgba(29, 56, 108) : gfx::Rgba(24, 31, 44);
        const std::uint32_t border = selected ? gfx::Rgba(124, 174, 255) : gfx::Rgba(58, 72, 98);
        DrawPanel(canvas, x + 18, cardY, width - 36, 88, fill, border);

        gfx::DrawText(canvas, x + 34, cardY + 14, TruncateText(entry.name, 42), gfx::Rgba(242, 246, 255), 1);
        gfx::DrawText(canvas,
                      x + 34,
                      cardY + 34,
                      TruncateText(entry.summary.empty() ? entry.id : entry.summary, 58),
                      gfx::Rgba(168, 180, 206),
                      1);

        int chipX = x + 34;
        if (suggested) {
            DrawChip(canvas, chipX, cardY + 56, UseEnglish(state) ? "Suggested" : "Sugerido", gfx::Rgba(31, 108, 87), gfx::Rgba(225, 255, 247));
            chipX += 92;
        }
        if (installed) {
            DrawChip(canvas, chipX, cardY + 56, UseEnglish(state) ? "Installed" : "Instalado", gfx::Rgba(110, 84, 34), gfx::Rgba(255, 240, 208));
            chipX += 92;
        }
        DrawChip(canvas, chipX, cardY + 56, entry.contentRevision, gfx::Rgba(55, 63, 82), gfx::Rgba(210, 220, 242));

        cardY += 98;
    }
}

void DrawDetails(gfx::Canvas& canvas,
                 const AppState& state,
                 const std::vector<const CatalogEntry*>& items,
                 int x,
                 int y,
                 int width,
                 int height) {
    DrawPanel(canvas, x, y, width, height, gfx::Rgba(16, 21, 31), gfx::Rgba(47, 58, 79));
    gfx::DrawText(canvas, x + 20, y + 18, UiText(state, "Detalhes", "Details"), gfx::Rgba(238, 244, 255), 2);

    if (state.section == ContentSection::About) {
        gfx::DrawText(canvas, x + 20, y + 58, UiText(state, "Sobre a M.I.L.", "About M.I.L."), gfx::Rgba(240, 244, 255), 1);
        gfx::DrawTextWrapped(canvas,
                             x + 20,
                             y + 82,
                             width - 40,
                             UiText(state,
                                    "Gerenciador dedicado a traducoes, dublagens, mods, cheats e saves com foco em Switch real e Ryujinx.",
                                    "Manager dedicated to translations, dubs, mods, cheats and saves for both real Switch and Ryujinx."),
                             gfx::Rgba(176, 186, 210),
                             1,
                             8);
        return;
    }

    if (items.empty()) {
        gfx::DrawTextWrapped(canvas,
                             x + 20,
                             y + 58,
                             width - 40,
                             UiText(state,
                                    "Carregue um catalogo ou troque de secao para ver os detalhes do pacote selecionado.",
                                    "Load a catalog or change section to view package details."),
                             gfx::Rgba(176, 186, 210),
                             1,
                             6);
        return;
    }

    const CatalogEntry& entry = *items[std::min(state.selection, items.size() - 1)];
    const InstalledTitle* installedTitle = FindInstalledTitle(state.installedTitles, entry.titleId);

    int cursorY = y + 58;
    gfx::DrawTextWrapped(canvas, x + 20, cursorY, width - 40, entry.name, gfx::Rgba(245, 247, 255), 1, 3);
    cursorY += 42;
    gfx::DrawText(canvas, x + 20, cursorY, std::string(UiText(state, "Pacote: ", "Package: ")) + entry.id, gfx::Rgba(168, 180, 206), 1);
    cursorY += 18;
    gfx::DrawText(canvas, x + 20, cursorY, std::string(UiText(state, "Jogo: ", "Game: ")) + entry.titleId, gfx::Rgba(168, 180, 206), 1);
    cursorY += 18;
    if (!entry.author.empty()) {
        gfx::DrawText(canvas,
                      x + 20,
                      cursorY,
                      std::string(UiText(state, "Autor: ", "Author: ")) + entry.author,
                      gfx::Rgba(168, 180, 206),
                      1);
        cursorY += 18;
    }
    if (!entry.contentRevision.empty()) {
        gfx::DrawText(canvas,
                      x + 20,
                      cursorY,
                      std::string(UiText(state, "Revisao: ", "Revision: ")) + entry.contentRevision,
                      gfx::Rgba(168, 180, 206),
                      1);
        cursorY += 18;
    }
    if (!entry.packageVersion.empty()) {
        gfx::DrawText(canvas,
                      x + 20,
                      cursorY,
                      std::string(UiText(state, "Versao do pacote: ", "Package version: ")) + entry.packageVersion,
                      gfx::Rgba(168, 180, 206),
                      1);
        cursorY += 18;
    }
    if (installedTitle && !installedTitle->displayVersion.empty()) {
        gfx::DrawText(canvas,
                      x + 20,
                      cursorY,
                      std::string(UiText(state, "Versao do jogo: ", "Game version: ")) + installedTitle->displayVersion,
                      gfx::Rgba(168, 180, 206),
                      1);
        cursorY += 18;
    }

    cursorY += 8;
    cursorY += gfx::DrawTextWrapped(canvas, x + 20, cursorY, width - 40, entry.summary, gfx::Rgba(215, 223, 241), 1, 5);
    cursorY += 10;
    cursorY += gfx::DrawTextWrapped(canvas,
                                    x + 20,
                                    cursorY,
                                    width - 40,
                                    MakeCompatibilitySummaryLocalized(state, entry, installedTitle),
                                    gfx::Rgba(255, 214, 153),
                                    1,
                                    6);
}

void RenderUi(PlatformSession& session, const AppState& state) {
    if (!session.framebufferReady) {
        return;
    }

    gfx::Canvas canvas = gfx::BeginFrame(session.framebuffer);
    if (!canvas.pixels) {
        return;
    }

    gfx::ClearVerticalGradient(canvas, gfx::Rgba(8, 12, 20), gfx::Rgba(16, 22, 34));
    gfx::FillRect(canvas, 0, 0, canvas.width, 86, gfx::Rgba(12, 18, 30));
    gfx::FillRect(canvas, 0, 86, canvas.width, 2, gfx::Rgba(68, 90, 134));

    gfx::DrawText(canvas, 28, 26, UseEnglish(state) ? "MIL Translations Manager" : "Gerenciador MIL Traducoes", gfx::Rgba(248, 250, 255), 2);
    DrawChip(canvas, 520, 30, (UseEnglish(state) ? "Games " : "Jogos ") + std::to_string(state.installedTitles.size()), gfx::Rgba(34, 48, 74), gfx::Rgba(220, 230, 250));
    DrawChip(canvas, 640, 30, (UseEnglish(state) ? "Packages " : "Pacotes ") + std::to_string(state.receipts.size()), gfx::Rgba(34, 48, 74), gfx::Rgba(220, 230, 250));
    DrawChip(canvas, 786, 30, std::string(UseEnglish(state) ? "Lang " : "Idioma ") + LanguageModeLabel(state.config.language), gfx::Rgba(34, 48, 74), gfx::Rgba(220, 230, 250));
    DrawChip(canvas, 930, 30, std::string("Scan ") + InstalledTitleScanModeLabel(state.config.scanMode), gfx::Rgba(34, 48, 74), gfx::Rgba(220, 230, 250));

    gfx::DrawTextWrapped(canvas,
                         28,
                         58,
                         1220,
                         std::string(UiText(state, "Fonte: ", "Source: ")) + GetCatalogSourceLabel(state),
                         gfx::Rgba(156, 173, 204),
                         1,
                         2);

    const auto items = BuildVisibleEntries(state);
    DrawSidebar(canvas, state, 24, 108, 250, 520);
    DrawEntryList(canvas, state, items, 292, 108, 560, 520);
    DrawDetails(canvas, state, items, 870, 108, 386, 520);

    DrawPanel(canvas, 24, 640, 1232, 24, gfx::Rgba(16, 21, 31), gfx::Rgba(47, 58, 79));
    gfx::DrawTextWrapped(canvas,
                         34,
                         648,
                         1212,
                         TruncateText(state.statusLine + " | " + state.platformNote, 160),
                         gfx::Rgba(181, 193, 219),
                         1,
                         1);

    DrawPanel(canvas, 24, 670, 1232, 42, gfx::Rgba(10, 14, 22), gfx::Rgba(47, 58, 79));
    gfx::DrawTextWrapped(canvas,
                         34,
                         678,
                         1212,
                         UiText(state,
                                "LEFT/RIGHT muda foco  UP/DOWN navega  A instala/remove  X atualiza  L idioma",
                                "LEFT/RIGHT change focus  UP/DOWN navigate  A install/remove  X refresh  L language"),
                         gfx::Rgba(191, 201, 224),
                         1,
                         2);

    gfx::EndFrame(session.framebuffer);
}

void RenderAbout(const AppState& state) {
    PrintLine(UiText(state, "Objetivo", "Purpose"));
    PrintLine(UiText(state,
                     "Aplicativo homebrew para listar e instalar traducoes, mods, cheats e saves.",
                     "Homebrew app to list and install translations, mods, cheats and save games."));
    PrintLine("");
    PrintLine(UiText(state, "Arquitetura atual", "Current architecture"));
    PrintLine(UiText(state, "- Catalogo remoto em JSON com cache local automatico", "- Remote JSON catalog with automatic local cache"));
    PrintLine(UiText(state, "- Instalacao por ZIP em sdmc:/ com recibo para remocao limpa", "- ZIP installation to sdmc:/ with receipts for clean removal"));
    PrintLine(UiText(state, "- Compatibilidade console + emulador por degradacao de servicos", "- Console + emulator compatibility with service fallback"));
    PrintLine(UiText(state, "- Configuracao em sdmc:/config/mil-manager/settings.ini", "- Configuration in sdmc:/config/mil-manager/settings.ini"));
    PrintLine("");
    PrintLine(UiText(state, "Diretorios", "Directories"));
    PrintLine(UiText(state, "- Cache: sdmc:/config/mil-manager/cache", "- Cache: sdmc:/config/mil-manager/cache"));
    PrintLine(UiText(state, "- Cache do indice: sdmc:/config/mil-manager/cache/index.json", "- Catalog cache: sdmc:/config/mil-manager/cache/index.json"));
    PrintLine(UiText(state, "- Recibos: sdmc:/config/mil-manager/receipts", "- Receipts: sdmc:/config/mil-manager/receipts"));
    PrintLine(UiText(state, "- Conteudo instalado: sdmc:/", "- Installed content: sdmc:/"));
    if (IsEmulatorEnvironment()) {
        PrintLine(UiText(state,
                         "- Importacao do emulador: sdmc:/switch/mil_manager/emulator-installed.json",
                         "- Emulator import: sdmc:/switch/mil_manager/emulator-installed.json"));
    }
    PrintLine("");
    PrintLine(UiText(state, "Fonte ativa do catalogo", "Active catalog source"));
    PrintLine(GetCatalogSourceLabel(state));
    if (!state.catalog.catalogName.empty() || !state.catalog.catalogRevision.empty() || !state.catalog.channel.empty()) {
        PrintLine("");
        PrintLine(UiText(state, "Metadados do catalogo", "Catalog metadata"));
        if (!state.catalog.catalogName.empty()) {
            PrintLine(std::string(UiText(state, "Nome: ", "Name: ")) + state.catalog.catalogName);
        }
        if (!state.catalog.catalogRevision.empty()) {
            PrintLine(std::string(UiText(state, "Revisao: ", "Revision: ")) + state.catalog.catalogRevision);
        }
        if (!state.catalog.channel.empty()) {
            PrintLine(std::string(UiText(state, "Canal: ", "Channel: ")) + state.catalog.channel);
        }
        if (!state.catalog.generatedAt.empty()) {
            PrintLine(std::string(UiText(state, "Gerado em: ", "Generated at: ")) + state.catalog.generatedAt);
        }
        if (!state.catalog.schemaVersion.empty()) {
            PrintLine("Schema: " + state.catalog.schemaVersion);
        }
    }
}

void RenderEntries(const AppState& state, const std::vector<const CatalogEntry*>& items) {
    if (items.empty()) {
        PrintLine(UiText(state, "Nenhum item disponivel nesta secao.", "No items available in this section."));
        return;
    }

    const std::size_t clampedSelection = std::min(state.selection, items.size() - 1);
    const std::size_t windowStart = clampedSelection > 4 ? clampedSelection - 4 : 0;
    const std::size_t windowEnd = std::min(items.size(), windowStart + 10);

    PrintLine(UiText(state, "Lista", "List"));
    for (std::size_t index = windowStart; index < windowEnd; ++index) {
        const CatalogEntry& entry = *items[index];
        const bool selected = index == clampedSelection;
        const bool suggested = FindInstalledTitle(state.installedTitles, entry.titleId) != nullptr;
        const bool installed = FindReceiptForPackage(state.receipts, entry.id, nullptr);

        std::string prefix = selected ? "> " : "  ";
        std::string line = prefix + entry.name;
        if (suggested) {
            line += UseEnglish(state) ? " [SUGGESTED]" : " [SUGERIDO]";
        }
        if (installed) {
            line += UseEnglish(state) ? " [INSTALLED]" : " [INSTALADO]";
        }
        PrintLine(line);
    }

    const CatalogEntry& selectedEntry = *items[clampedSelection];
    const InstalledTitle* installedTitle = FindInstalledTitle(state.installedTitles, selectedEntry.titleId);

    PrintLine("");
    PrintLine(UiText(state, "Detalhes", "Details"));
    PrintLine(std::string(UiText(state, "Pacote: ", "Package: ")) + selectedEntry.id +
              std::string(UseEnglish(state) ? " | Revision: " : " | Revisao: ") + selectedEntry.contentRevision);
    PrintLine(std::string(UiText(state, "Jogo: ", "Game: ")) + selectedEntry.titleId +
              (installedTitle ? UiText(state, " | Instalado localmente", " | Installed locally")
                              : UiText(state, " | Nao instalado", " | Not installed")));
    if (!selectedEntry.packageVersion.empty()) {
        PrintLine(std::string(UiText(state, "Versao do pacote: ", "Package version: ")) + selectedEntry.packageVersion);
    }
    if (installedTitle != nullptr && !installedTitle->displayVersion.empty()) {
        PrintLine(std::string(UiText(state, "Versao do jogo: ", "Game version: ")) + installedTitle->displayVersion);
    }
    if (!selectedEntry.summary.empty()) {
        PrintLine(selectedEntry.summary);
    }
    PrintLine(MakeCompatibilitySummaryLocalized(state, selectedEntry, installedTitle));
}

void Render(const AppState& state) {
    consoleClear();

    PrintLine(UiText(state, "Gerenciador MIL Traducoes", "MIL Translations Manager"));
    PrintLine(std::string(UiText(state, "Secao: ", "Section: ")) + SectionLabelLocalized(state, state.section) +
              std::string(UseEnglish(state) ? " | Installed games: " : " | Jogos instalados: ") + std::to_string(state.installedTitles.size()) +
              std::string(UseEnglish(state) ? " | Installed packages: " : " | Pacotes instalados: ") + std::to_string(state.receipts.size()));
    PrintLine(std::string(UiText(state, "Idioma: ", "Language: ")) + std::string(LanguageModeLabel(state.config.language)) +
              " | Scan: " + std::string(InstalledTitleScanModeLabel(state.config.scanMode)));
    PrintLine(std::string(UiText(state, "Fonte: ", "Source: ")) + GetCatalogSourceLabel(state));
    PrintLine(state.statusLine);
    if (!state.platformNote.empty()) {
        PrintLine(state.platformNote);
    }
    PrintLine("");

    if (state.section == ContentSection::About) {
        RenderAbout(state);
    } else {
        RenderEntries(state, BuildVisibleEntries(state));
    }

    PrintLine("");
    PrintLine(UiText(state, "Controles", "Controls"));
    PrintLine(UiText(state,
                     "LEFT/RIGHT muda foco | UP/DOWN navega | A instala/remove | X atualiza catalogo",
                     "LEFT/RIGHT change focus | UP/DOWN navigate | A install/remove | X refresh catalog"));
    PrintLine(UiText(state, "L alterna idioma", "L switch language"));
}

void ReloadLocalState(AppState& state) {
    std::string configNote;
    state.config = LoadAppConfig(configNote);
    state.platformNote = configNote;

    std::string receiptsNote;
    state.receipts = LoadInstallReceipts(receiptsNote);
}

void RefreshInstalledTitles(AppState& state) {
    std::string titlesNote;
    state.installedTitles = LoadInstalledTitles(state.config, &state.catalog, titlesNote);
    if (!titlesNote.empty()) {
        state.platformNote = titlesNote;
    }
}

void CycleLanguage(AppState& state) {
    switch (state.config.language) {
        case LanguageMode::Auto:
            state.config.language = LanguageMode::PtBr;
            break;
        case LanguageMode::PtBr:
            state.config.language = LanguageMode::EnUs;
            break;
        case LanguageMode::EnUs:
        default:
            state.config.language = LanguageMode::Auto;
            break;
    }

    std::string saveError;
    if (SaveAppConfig(state.config, saveError)) {
        state.statusLine = UseEnglish(state) ? "Language saved to settings.ini" : "Idioma salvo em settings.ini";
    } else {
        state.statusLine = saveError;
    }
}

void HandleSelectionAction(AppState& state) {
    if (state.section == ContentSection::About) {
        return;
    }

    const auto items = BuildVisibleEntries(state);
    if (items.empty()) {
        return;
    }

    const CatalogEntry& entry = *items[std::min(state.selection, items.size() - 1)];
    InstallReceipt receipt;
    if (FindReceiptForPackage(state.receipts, entry.id, &receipt)) {
        std::string error;
        if (UninstallPackage(receipt, error)) {
            state.statusLine = std::string(UseEnglish(state) ? "Package removed: " : "Pacote removido: ") + entry.name;
            std::string note;
            state.receipts = LoadInstallReceipts(note);
        } else {
            state.statusLine = error;
        }
        return;
    }

    const InstalledTitle* installedTitle = FindInstalledTitle(state.installedTitles, entry.titleId);
    InstallReceipt newReceipt;
    std::string error;
    if (InstallPackage(entry, installedTitle, newReceipt, error)) {
        state.statusLine = std::string(UseEnglish(state) ? "Package installed: " : "Pacote instalado: ") + entry.name;
        std::string note;
        state.receipts = LoadInstallReceipts(note);
    } else {
        state.statusLine = std::string(UseEnglish(state) ? "Installation failed: " : "Falha na instalacao: ") + error;
    }
}

}  // namespace

int RunApplication() {
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);

    PadState pad;
    padInitializeDefault(&pad);

    PlatformSession session;
    AppState state;

    std::string platformInitNote;
    InitializePlatform(session, platformInitNote);
    state.platformNote = platformInitNote;

    ReloadLocalState(state);
    RefreshInstalledTitles(state);
    if (LoadCatalog(state)) {
        RefreshInstalledTitles(state);
        state.statusLine = UseEnglish(state) ? "Ready. Catalog loaded." : "Pronto. Catalogo carregado.";
    }

    const std::vector<ContentSection> sections = {
        ContentSection::Translations,
        ContentSection::ModsTools,
        ContentSection::Cheats,
        ContentSection::SaveGames,
        ContentSection::About,
    };

    while (appletMainLoop()) {
        padUpdate(&pad);
        const u64 buttonsDown = padGetButtonsDown(&pad);

        std::size_t currentSectionIndex = std::find(sections.begin(), sections.end(), state.section) - sections.begin();
        auto visibleEntries = BuildVisibleEntries(state);

        const u64 exitButtons = HidNpadButton_Minus | HidNpadButton_B;
        if (buttonsDown & exitButtons) {
            if (IsEmulatorEnvironment()) {
                ForceExitEmulator(session);
            }
            break;
        }
        if (buttonsDown & HidNpadButton_Right) {
            state.focus = AppState::FocusPane::Catalog;
        }
        if (buttonsDown & HidNpadButton_Left) {
            state.focus = AppState::FocusPane::Sections;
        }
        if (buttonsDown & HidNpadButton_Down) {
            if (state.focus == AppState::FocusPane::Sections) {
                currentSectionIndex = (currentSectionIndex + 1) % sections.size();
                state.section = sections[currentSectionIndex];
                state.selection = 0;
            } else if (!visibleEntries.empty()) {
                state.selection = std::min(state.selection + 1, visibleEntries.size() - 1);
            }
        }
        if (buttonsDown & HidNpadButton_Up) {
            if (state.focus == AppState::FocusPane::Sections) {
                currentSectionIndex = (currentSectionIndex + sections.size() - 1) % sections.size();
                state.section = sections[currentSectionIndex];
                state.selection = 0;
            } else if (state.selection > 0) {
                state.selection -= 1;
            }
        }
        if (buttonsDown & HidNpadButton_L) {
            CycleLanguage(state);
        }
        if (buttonsDown & HidNpadButton_X) {
            ReloadLocalState(state);
            RefreshInstalledTitles(state);
            if (LoadCatalog(state)) {
                RefreshInstalledTitles(state);
                state.statusLine = UseEnglish(state) ? "Catalog refreshed." : "Catalogo atualizado.";
            }
        }
        if (buttonsDown & HidNpadButton_A) {
            HandleSelectionAction(state);
            ReloadLocalState(state);
            RefreshInstalledTitles(state);
        }

        RenderUi(session, state);
    }

    ShutdownPlatform(session);
    return 0;
}

}  // namespace mil
